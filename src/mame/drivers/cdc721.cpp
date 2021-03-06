// license:BSD-3-Clause
// copyright-holders:Robbbert,AJR
/************************************************************************************************************

Control Data Corporation CDC 721 Terminal (Viking)

2013-08-13 Skeleton


*************************************************************************************************************/

#include "emu.h"
#include "bus/rs232/rs232.h"
#include "cpu/z80/z80.h"
#include "machine/bankdev.h"
#include "machine/i8255.h"
#include "machine/input_merger.h"
#include "machine/ins8250.h"
#include "machine/nvram.h"
#include "machine/output_latch.h"
#include "machine/z80ctc.h"
#include "video/tms9927.h"
#include "emupal.h"
#include "screen.h"


class cdc721_state : public driver_device
{
public:
	cdc721_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_bank_16k(*this, {"block0", "block4", "block8", "blockc"})
		, m_rom_chargen(*this, "chargen")
		, m_ram_chargen(*this, "chargen")
		, m_videoram(*this, "videoram")
		, m_nvram(*this, "nvram")
	{ }

	void cdc721(machine_config &config);
private:
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
	DECLARE_PALETTE_INIT(cdc721);
	DECLARE_WRITE8_MEMBER(interrupt_mask_w);
	DECLARE_WRITE8_MEMBER(misc_w);
	DECLARE_WRITE8_MEMBER(lights_w);
	DECLARE_WRITE8_MEMBER(block_select_w);
	DECLARE_WRITE8_MEMBER(nvram_w);

	template<int Line> DECLARE_WRITE_LINE_MEMBER(int_w);
	TIMER_CALLBACK_MEMBER(update_interrupts);
	IRQ_CALLBACK_MEMBER(restart_cb);

	template<int Bit> DECLARE_WRITE_LINE_MEMBER(foreign_char_bank_w);

	void io_map(address_map &map);
	void mem_map(address_map &map);
	void block0_map(address_map &map);
	void block4_map(address_map &map);
	void block8_map(address_map &map);
	void blockc_map(address_map &map);

	virtual void machine_start() override;
	virtual void machine_reset() override;

	u8 m_flashcnt;
	u8 m_foreign_char_bank;

	u8 m_pending_interrupts;
	u8 m_active_interrupts;
	u8 m_interrupt_mask;

	required_device<cpu_device> m_maincpu;
	required_device_array<address_map_bank_device, 4> m_bank_16k;
	required_region_ptr<u8> m_rom_chargen;
	required_shared_ptr<u8> m_ram_chargen;
	required_shared_ptr<u8> m_videoram;
	required_shared_ptr<u8> m_nvram;
};

WRITE8_MEMBER(cdc721_state::interrupt_mask_w)
{
	m_interrupt_mask = data ^ 0xff;
	machine().scheduler().synchronize(timer_expired_delegate(FUNC(cdc721_state::update_interrupts), this));
}

template <int Line>
WRITE_LINE_MEMBER(cdc721_state::int_w)
{
	if (BIT(m_pending_interrupts, Line) == state)
		return;

	if (state)
		m_pending_interrupts |= 0x01 << Line;
	else
		m_pending_interrupts &= ~(0x01 << Line);

	machine().scheduler().synchronize(timer_expired_delegate(FUNC(cdc721_state::update_interrupts), this));
}

TIMER_CALLBACK_MEMBER(cdc721_state::update_interrupts)
{
	m_active_interrupts = m_pending_interrupts & m_interrupt_mask;
	m_maincpu->set_input_line(INPUT_LINE_IRQ0, m_active_interrupts != 0 ? ASSERT_LINE : CLEAR_LINE);
}

IRQ_CALLBACK_MEMBER(cdc721_state::restart_cb)
{
	// IM 2 vector is produced through a SN74LS148N priority encoder plus some buffers and a latch
	// The CTC vector is not even fetched
	u8 vector = 0x00;
	u8 active = m_active_interrupts;
	while (vector < 0x0e && !BIT(active, 0))
	{
		active >>= 1;
		vector += 0x02;
	}
	return vector;
}

WRITE8_MEMBER(cdc721_state::misc_w)
{
	// 7: Stop Refresh Operation
	// 6: Enable RAM Char Gen
	// 5: Enable Block Cursor
	// 4: Reverse Video
	// 3: 132/80
	// 2: Enable Blink
	// 1: Enable Blinking Cursor
	// 0: Alarm

	logerror("%s: %d-column display selected\n", machine().describe_context(), BIT(data, 3) ? 132 : 80);
}

WRITE8_MEMBER(cdc721_state::block_select_w)
{
	logerror("%s: Bank select = %02X\n", machine().describe_context(), data);
	for (int b = 0; b < 4; b++)
	{
		m_bank_16k[b]->set_bank(data & 3);
		data >>= 2;
	}
}

WRITE8_MEMBER(cdc721_state::nvram_w)
{
	m_nvram[offset] = data & 0x0f;
}

template<int Bit>
WRITE_LINE_MEMBER(cdc721_state::foreign_char_bank_w)
{
	if (state)
		m_foreign_char_bank |= 1 << Bit;
	else
		m_foreign_char_bank &= ~(1 << Bit);
}

void cdc721_state::mem_map(address_map &map)
{
	map(0x0000, 0x3fff).rw("block0", FUNC(address_map_bank_device::read8), FUNC(address_map_bank_device::write8));
	map(0x4000, 0x7fff).rw("block4", FUNC(address_map_bank_device::read8), FUNC(address_map_bank_device::write8));
	map(0x8000, 0xbfff).rw("block8", FUNC(address_map_bank_device::read8), FUNC(address_map_bank_device::write8));
	map(0xc000, 0xffff).rw("blockc", FUNC(address_map_bank_device::read8), FUNC(address_map_bank_device::write8));
}

void cdc721_state::block0_map(address_map &map)
{
	map(0x0000, 0x3fff).rom().region("resident", 0);
	map(0x8000, 0xbfff).ram();
}

void cdc721_state::block4_map(address_map &map)
{
	map(0x0000, 0x00ff).mirror(0x2700).ram().share("nvram").w(FUNC(cdc721_state::nvram_w));
	map(0x0800, 0x0bff).mirror(0x2400).ram().share("chargen"); // 2x P2114AL-2
	map(0x8000, 0xbfff).rom().region("16krom", 0);
	map(0xc000, 0xffff).ram();
}

void cdc721_state::block8_map(address_map &map)
{
	map(0x0000, 0x3fff).rom().region("rompack", 0);
	map(0x4000, 0x7fff).ram();
}

void cdc721_state::blockc_map(address_map &map)
{
	map(0x0000, 0x1fff).ram();
	map(0x2000, 0x3fff).ram().share("videoram");
	map(0x4000, 0x40ff).mirror(0x2700).ram().share("nvram").w(FUNC(cdc721_state::nvram_w));
	map(0x4800, 0x4bff).mirror(0x2400).ram().share("chargen");
}

void cdc721_state::io_map(address_map &map)
{
	map.global_mask(0xff);
	map(0x00, 0x03).rw("ctc", FUNC(z80ctc_device::read), FUNC(z80ctc_device::write));
	map(0x10, 0x1f).rw("crtc", FUNC(crt5037_device::read), FUNC(crt5037_device::write));
	map(0x20, 0x27).rw("kbduart", FUNC(ins8250_device::ins8250_r), FUNC(ins8250_device::ins8250_w));
	map(0x30, 0x33).rw("ppi", FUNC(i8255_device::read), FUNC(i8255_device::write));
	map(0x40, 0x47).rw("comuart", FUNC(ins8250_device::ins8250_r), FUNC(ins8250_device::ins8250_w));
	map(0x50, 0x50).w("ledlatch", FUNC(output_latch_device::bus_w));
	map(0x70, 0x70).w(FUNC(cdc721_state::block_select_w));
	map(0x80, 0x87).rw("pauart", FUNC(ins8250_device::ins8250_r), FUNC(ins8250_device::ins8250_w));
	map(0x90, 0x97).rw("pbuart", FUNC(ins8250_device::ins8250_r), FUNC(ins8250_device::ins8250_w));
}

static INPUT_PORTS_START( cdc721 )
INPUT_PORTS_END

void cdc721_state::machine_reset()
{
	for (auto &bank : m_bank_16k)
		bank->set_bank(0);
}

void cdc721_state::machine_start()
{
	m_pending_interrupts = 0;
	m_active_interrupts = 0;
	m_interrupt_mask = 0;
	m_foreign_char_bank = 0;

	save_item(NAME(m_pending_interrupts));
	save_item(NAME(m_active_interrupts));
	save_item(NAME(m_interrupt_mask));
	save_item(NAME(m_foreign_char_bank));
}

/* F4 Character Displayer */
static const gfx_layout cdc721_charlayout =
{
	8, 16,                  /* 8 x 16 characters */
	256,                    /* 256 characters */
	1,                  /* 1 bits per pixel */
	{ 0 },                  /* no bitplanes */
	/* x offsets */
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	/* y offsets */
	{ 0*8, 0x100*8, 0x200*8, 0x300*8, 0x400*8, 0x500*8, 0x600*8, 0x700*8,
	0x800*8, 0x900*8, 0xa00*8, 0xb00*8, 0xc00*8, 0xd00*8, 0xe00*8, 0xf00*8 },
	8                   /* every char takes 16 x 1 bytes */
};

static GFXDECODE_START( gfx_cdc721 )
	GFXDECODE_ENTRY( "chargen", 0x0000, cdc721_charlayout, 0, 1 )
GFXDECODE_END

PALETTE_INIT_MEMBER( cdc721_state, cdc721 )
{
	palette.set_pen_color(0, 0, 0, 0 ); /* Black */
	palette.set_pen_color(1, 0, 255, 0 );   /* Full */
	palette.set_pen_color(2, 0, 128, 0 );   /* Dimmed */
}

uint32_t cdc721_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	uint8_t y,ra,chr,gfx,attr,pen;
	uint16_t sy=0,x;
	m_flashcnt++;

	for (y = 0; y < 30; y++)
	{
		uint16_t ma = m_videoram[y * 2] | m_videoram[y * 2 + 1] << 8;

		for (ra = 0; ra < 16; ra++)
		{
			uint16_t *p = &bitmap.pix16(sy++);

			for (x = 0; x < 160; x+=2)
			{
				pen = 1;
				chr = m_videoram[(x + ma) & 0x1fff];
				attr = m_videoram[(x + ma + 1) & 0x1fff];
				gfx = m_rom_chargen[chr | (ra << 8) ];
				if (BIT(attr, 0))  // blank
					pen = 0;
				if (BIT(attr, 1) && (ra == 14)) // underline
					gfx = 0xff;
				if (BIT(attr, 4)) // dim
					pen = 2;
				if (BIT(attr, 2)) // rv
					gfx ^= 0xff;
				if (BIT(attr, 3) && BIT(m_flashcnt, 6)) // blink
					gfx = 0;

				/* Display a scanline of a character */
				*p++ = BIT(gfx, 0) ? pen : 0;
				*p++ = BIT(gfx, 1) ? pen : 0;
				*p++ = BIT(gfx, 2) ? pen : 0;
				*p++ = BIT(gfx, 3) ? pen : 0;
				*p++ = BIT(gfx, 4) ? pen : 0;
				*p++ = BIT(gfx, 5) ? pen : 0;
				*p++ = BIT(gfx, 6) ? pen : 0;
				*p++ = BIT(gfx, 7) ? pen : 0;
			}
		}
	}
	return 0;
}

MACHINE_CONFIG_START(cdc721_state::cdc721)
	// basic machine hardware
	MCFG_DEVICE_ADD("maincpu", Z80, 6_MHz_XTAL) // Zilog Z8400B (Z80B)
	MCFG_DEVICE_PROGRAM_MAP(mem_map)
	MCFG_DEVICE_IO_MAP(io_map)
	MCFG_DEVICE_IRQ_ACKNOWLEDGE_DRIVER(cdc721_state, restart_cb)

	ADDRESS_MAP_BANK(config, "block0").set_map(&cdc721_state::block0_map).set_options(ENDIANNESS_LITTLE, 8, 32, 0x4000);
	ADDRESS_MAP_BANK(config, "block4").set_map(&cdc721_state::block4_map).set_options(ENDIANNESS_LITTLE, 8, 32, 0x4000);
	ADDRESS_MAP_BANK(config, "block8").set_map(&cdc721_state::block8_map).set_options(ENDIANNESS_LITTLE, 8, 32, 0x4000);
	ADDRESS_MAP_BANK(config, "blockc").set_map(&cdc721_state::blockc_map).set_options(ENDIANNESS_LITTLE, 8, 32, 0x4000);

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0); // MCM51L01C45 (256x4) + battery

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
	MCFG_SCREEN_UPDATE_DRIVER(cdc721_state, screen_update)
	MCFG_SCREEN_SIZE(640, 480)
	MCFG_SCREEN_VISIBLE_AREA(0, 639, 0, 479)
	MCFG_SCREEN_PALETTE("palette")
	MCFG_PALETTE_ADD("palette", 3)
	MCFG_PALETTE_INIT_OWNER(cdc721_state, cdc721)
	MCFG_DEVICE_ADD("gfxdecode", GFXDECODE, "palette", gfx_cdc721)

	MCFG_DEVICE_ADD("crtc", CRT5037, 12.936_MHz_XTAL / 8)
	MCFG_TMS9927_CHAR_WIDTH(8)

	MCFG_DEVICE_ADD("ctc", Z80CTC, 6_MHz_XTAL) // Zilog Z8430B (M1 pulled up)
	MCFG_Z80CTC_INTR_CB(WRITELINE(*this, cdc721_state, int_w<6>))
	MCFG_Z80CTC_ZC1_CB(WRITELINE("ctc", z80ctc_device, trg2))
	//MCFG_Z80CTC_ZC2_CB(WRITELINE("comuart", ins8250_device, rclk_w))

	MCFG_DEVICE_ADD("ppi", I8255A, 0)
	MCFG_I8255_OUT_PORTB_CB(WRITE8(*this, cdc721_state, interrupt_mask_w))
	MCFG_I8255_OUT_PORTC_CB(WRITE8(*this, cdc721_state, misc_w))

	output_latch_device &ledlatch(OUTPUT_LATCH(config, "ledlatch"));
	ledlatch.bit_handler<0>().set_output("error").invert();
	ledlatch.bit_handler<1>().set_output("alert").invert();
	ledlatch.bit_handler<2>().set_output("lock").invert();
	ledlatch.bit_handler<3>().set_output("message").invert();
	ledlatch.bit_handler<4>().set_output("prog1").invert();
	ledlatch.bit_handler<5>().set_output("prog2").invert();
	ledlatch.bit_handler<6>().set_output("prog3").invert();
	ledlatch.bit_handler<7>().set_output("dsr").invert();

	MCFG_DEVICE_ADD("comuart", INS8250, 1.8432_MHz_XTAL)
	MCFG_INS8250_OUT_INT_CB(WRITELINE(*this, cdc721_state, int_w<0>))
	MCFG_INS8250_OUT_TX_CB(WRITELINE("comm", rs232_port_device, write_txd))
	MCFG_INS8250_OUT_DTR_CB(WRITELINE("comm", rs232_port_device, write_dtr))
	MCFG_INS8250_OUT_RTS_CB(WRITELINE("comm", rs232_port_device, write_rts))

	MCFG_DEVICE_ADD("comm", RS232_PORT, default_rs232_devices, nullptr)
	MCFG_RS232_RXD_HANDLER(WRITELINE("comuart", ins8250_device, rx_w))
	MCFG_RS232_DSR_HANDLER(WRITELINE("comuart", ins8250_device, dsr_w))
	MCFG_RS232_DCD_HANDLER(WRITELINE("comuart", ins8250_device, dcd_w))
	MCFG_RS232_CTS_HANDLER(WRITELINE("comuart", ins8250_device, cts_w))
	MCFG_RS232_RI_HANDLER(WRITELINE("comuart", ins8250_device, ri_w))

	MCFG_DEVICE_ADD("kbduart", INS8250, 1.8432_MHz_XTAL)
	MCFG_INS8250_OUT_INT_CB(WRITELINE(*this, cdc721_state, int_w<5>))
	MCFG_INS8250_OUT_DTR_CB(WRITELINE(*this, cdc721_state, foreign_char_bank_w<2>))
	//MCFG_INS8250_OUT_RTS_CB(WRITELINE(*this, cdc721_state, alarm_high_low_w))
	MCFG_INS8250_OUT_OUT1_CB(WRITELINE(*this, cdc721_state, foreign_char_bank_w<1>))
	MCFG_INS8250_OUT_OUT2_CB(WRITELINE(*this, cdc721_state, foreign_char_bank_w<0>))

	MCFG_DEVICE_ADD("pauart", INS8250, 1.8432_MHz_XTAL)
	MCFG_INS8250_OUT_INT_CB(WRITELINE("int2", input_merger_device, in_w<1>))
	MCFG_INS8250_OUT_TX_CB(WRITELINE("cha", rs232_port_device, write_txd))
	MCFG_INS8250_OUT_DTR_CB(WRITELINE("cha", rs232_port_device, write_dtr))
	MCFG_INS8250_OUT_RTS_CB(WRITELINE("cha", rs232_port_device, write_rts))

	MCFG_DEVICE_ADD("cha", RS232_PORT, default_rs232_devices, nullptr)
	MCFG_RS232_RXD_HANDLER(WRITELINE("pauart", ins8250_device, rx_w))
	MCFG_RS232_DSR_HANDLER(WRITELINE("pauart", ins8250_device, dsr_w))
	MCFG_RS232_DCD_HANDLER(WRITELINE("pauart", ins8250_device, dcd_w))
	MCFG_RS232_CTS_HANDLER(WRITELINE("pauart", ins8250_device, cts_w))
	MCFG_RS232_RI_HANDLER(WRITELINE("pauart", ins8250_device, ri_w))

	MCFG_DEVICE_ADD("pbuart", INS8250, 1.8432_MHz_XTAL)
	MCFG_INS8250_OUT_INT_CB(WRITELINE("int2", input_merger_device, in_w<0>))
	MCFG_INS8250_OUT_TX_CB(WRITELINE("chb", rs232_port_device, write_txd))
	MCFG_INS8250_OUT_DTR_CB(WRITELINE("chb", rs232_port_device, write_dtr))
	MCFG_INS8250_OUT_RTS_CB(WRITELINE("chb", rs232_port_device, write_rts))

	MCFG_DEVICE_ADD("chb", RS232_PORT, default_rs232_devices, nullptr)
	MCFG_RS232_RXD_HANDLER(WRITELINE("pbuart", ins8250_device, rx_w))
	MCFG_RS232_DSR_HANDLER(WRITELINE("pbuart", ins8250_device, dsr_w))
	MCFG_RS232_DCD_HANDLER(WRITELINE("pbuart", ins8250_device, dcd_w))
	MCFG_RS232_CTS_HANDLER(WRITELINE("pbuart", ins8250_device, cts_w))
	MCFG_RS232_RI_HANDLER(WRITELINE("pbuart", ins8250_device, ri_w))

	MCFG_INPUT_MERGER_ANY_HIGH("int2") // 74S05 (open collector)
	MCFG_INPUT_MERGER_OUTPUT_HANDLER(WRITELINE(*this, cdc721_state, int_w<2>))
MACHINE_CONFIG_END

ROM_START( cdc721 )
	ROM_REGION( 0x4000, "resident", 0 )
	ROM_LOAD( "66315359", 0x0000, 0x2000, CRC(20ff3eb4) SHA1(5f15cb14893d75a46dc66d3042356bb054d632c2) )
	ROM_LOAD( "66315361", 0x2000, 0x2000, CRC(21d59d09) SHA1(9c087537d68c600ddf1eb9b009cf458231c279f4) )

	ROM_REGION( 0x4000, "16krom", 0 )
	ROM_LOAD( "66315360", 0x0000, 0x1000, CRC(feaa0fc5) SHA1(f06196553a1f10c07b2f7e495823daf7ea26edee) )
	//ROM_FILL(0x0157,1,0xe0)

	ROM_REGION( 0x4000, "rompack", ROMREGION_ERASE00 )

	ROM_REGION( 0x2000, "chargen", 0 )
	ROM_LOAD( "66315039", 0x0000, 0x1000, CRC(5c9aa968) SHA1(3ec7c5f25562579e6ed3fda7562428ff5e6b9550) )
	ROM_LOAD( "66307828", 0x1000, 0x1000, CRC(ac97136f) SHA1(0d280e1aa4b9502bd390d260f83af19bf24905cd) ) // foreign character ROM

	// Graphics Firmware pack
	ROM_REGION( 0x4000, "gfxfw", 0 ) // load at 0x8000
	ROM_LOAD( "66315369.bin", 0x0000, 0x2000, CRC(224d3368) SHA1(e335ef6cd56d77194235f5a2a7cf2af9ebf42342) )
	ROM_LOAD( "66315370.bin", 0x2000, 0x2000, CRC(2543bf32) SHA1(1ac73a0e475d9fd86fba054e1a7a443d5bad1987) )
ROM_END

COMP( 1981, cdc721, 0, 0, cdc721, cdc721, cdc721_state, empty_init, "Control Data Corporation", "721 Display Terminal", MACHINE_NOT_WORKING | MACHINE_NO_SOUND_HW )
