// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// Standalone Hitachi HD6303X CPU core.
// Ported from MAME src/devices/cpu/m6800/m6800.cpp and m6801.cpp
// (BSD-3-Clause, copyright-holders: Aaron Giles). Logic is kept as close to
// the MAME implementation as possible; only the device framework (address
// maps, devcb, timers, save states, logging) was replaced.

#include "hd6303x.h"

#include <algorithm>
#include <cstring>

namespace hd6303 {

#define m6800_cpu_device hd6303x
#include "6800macros.h"

/* include the opcode functions (verbatim MAME) */
#include "6800ops.hxx"

#include "6303tables.h"
#undef m6800_cpu_device

static inline int BIT(u32 x, int n) { return (x >> n) & 1; }

// ---------------------------------------------------------------------------
// constants (from m6801.cpp)
// ---------------------------------------------------------------------------

#define CT      m_counter.w.l
#define CTH     m_counter.w.h
#define CTD     m_counter.d
#define OC      m_output_compare[0].w.l
#define OCH     m_output_compare[0].w.h
#define OCD     m_output_compare[0].d
#define OC2     m_output_compare[1].w.l
#define OC2H    m_output_compare[1].w.h
#define OC2D    m_output_compare[1].d
#define TOH     m_timer_over.w.h
#define TOD     m_timer_over.d

#define M6801_RMCR_SS_MASK      0x03
#define M6801_TRCSR_RDRF        0x80
#define M6801_TRCSR_ORFE        0x40
#define M6801_TRCSR_TDRE        0x20
#define M6801_TRCSR_RIE         0x10
#define M6801_TRCSR_RE          0x08
#define M6801_TRCSR_TIE         0x04
#define M6801_TRCSR_TE          0x02
#define M6801_TRCSR_WU          0x01
#define M6801_PORT2_IO4         0x10

static const int M6801_RMCR_SS[] = { 16, 128, 1024, 4096 };

#define M6801_SERIAL_START      0
#define M6801_SERIAL_STOP       9

enum { M6801_TX_STATE_INIT = 0, M6801_TX_STATE_READY };

#define TCSR_OLVL   0x01
#define TCSR_IEDG   0x02
#define TCSR_ETOI   0x04
#define TCSR_EOCI   0x08
#define TCSR_EICI   0x10
#define TCSR_TOF    0x20
#define TCSR_OCF    0x40
#define TCSR_ICF    0x80

#define TCSR2_OE1   0x01
#define TCSR2_OE2   0x02
#define TCSR2_OLVL2 0x04
#define TCSR2_EOCI2 0x08
#define TCSR2_OCF2  0x20

// ---------------------------------------------------------------------------
// construction / reset
// ---------------------------------------------------------------------------

hd6303x::hd6303x()
{
	m_ppc.d = m_pc.d = m_s.d = m_x.d = m_d.d = m_ea.d = 0;
	m_cc = 0;
	m_wai_state = 0;
	m_nmi_state = m_nmi_pending = 0;
	m_irq_state[0] = m_irq_state[1] = 0;
	m_icount = 0;

	std::memset(m_iram, 0, sizeof(m_iram));
	m_port_ddr[0] = m_port_ddr[1] = 0;
	m_port_data[0] = m_port_data[1] = 0;
	m_portx_ddr[0] = m_portx_ddr[1] = 0;
	m_portx_data[0] = m_portx_data[1] = 0;
	m_tcsr = m_pending_tcsr = 0;
	m_tcsr2 = m_pending_tcsr2 = 0;
	m_ram_ctrl = 0;
	m_counter.d = 0;
	m_output_compare[0].d = m_output_compare[1].d = 0;
	m_input_capture = 0;
	m_port2_written = false;
	m_timer_over.d = 0;
	m_timer_next = 0;
	m_latch09 = 0;
	m_t2cnt = 0; m_tconr = 0; m_tcsr3 = 0;
	m_tout3 = false; m_t2cnt_written = false;

	m_trcsr = m_rmcr = m_rdr = m_tdr = m_rsr = m_tshr = 0;
	m_rxbits = m_txbits = m_txstate = 0;
	m_trcsr_read_tdre = m_trcsr_read_orfe = m_trcsr_read_rdrf = 0;
	m_tx = 0; m_sci_clocks = 0;
	m_use_ext_serclock = false;
	m_sclk_divider = 16;   // HD6301X/HD6303X
	m_sci_internal_divisor = 0;
	m_sci_internal_count = 0;
}

void hd6303x::reset()
{
	// m6800_cpu_device::device_reset
	m_cc = 0xc0;
	SEI;
	PCD = RM16(0xfffe);
	m_wai_state = 0;
	m_nmi_state = 0;
	m_nmi_pending = 0;

	// m6801_cpu_device::device_reset
	m_port_ddr[0] = m_port_ddr[1] = 0;
	m_port2_written = false;
	m_tcsr = 0x00;
	m_pending_tcsr = 0x00;
	CTD = 0x0000;
	OCD = 0xffff;
	TOD = 0xffff;
	m_timer_next = 0xffff;
	m_ram_ctrl |= 0x40;
	m_latch09 = 0;

	m_trcsr = M6801_TRCSR_TDRE;
	m_txstate = M6801_TX_STATE_INIT;
	m_txbits = m_rxbits = 0;
	m_tx = 1;
	m_trcsr_read_tdre = 0;
	m_trcsr_read_orfe = 0;
	m_trcsr_read_rdrf = 0;
	m_use_ext_serclock = false;
	set_rmcr(0); // as in MAME: a no-op if RMCR is already 0

	// hd6301x_cpu_device::device_reset
	m_portx_ddr[0] = m_portx_ddr[1] = 0;
	m_tcsr2 = 0x00;
	m_pending_tcsr2 = 0x00;
	OC2D = 0xffff;
	m_ram_ctrl = (m_ram_ctrl & 0x80) | 0x7c;
	m_t2cnt = 0x00;
	m_tconr = 0xff;
	m_tcsr3 = 0x00;
	m_tout3 = false;
	m_t2cnt_written = false;
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------

u8 hd6303x::RM(u16 addr)
{
	if (addr < 0x20)
	{
		switch (addr)
		{
		case 0x01: case 0x03: case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f: case 0x10: case 0x11:
		case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
			return internal_r(u8(addr));
		default:
			break; // falls through to the external bus
		}
	}
	else if (addr >= 0x40 && addr < 0x100)
		return m_iram[addr];

	return read_mem ? read_mem(mem_ctx, addr) : 0xff;
}

void hd6303x::WM(u16 addr, u8 data)
{
	if (addr < 0x20)
	{
		switch (addr)
		{
		case 0x01: case 0x03: case 0x08: case 0x09: case 0x0a: case 0x0b:
		case 0x0c: case 0x0d: case 0x0e: case 0x0f: case 0x10: case 0x11:
		case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d:
			internal_w(u8(addr), data);
			return;
		default:
			break;
		}
	}
	else if (addr >= 0x40 && addr < 0x100)
	{
		m_iram[addr] = data;
		return;
	}

	if (write_mem)
		write_mem(mem_ctx, addr, data);
}

u32 hd6303x::RM16(u32 Addr)
{
	u32 result = RM(Addr) << 8;
	return result | RM((Addr + 1) & 0xffff);
}

void hd6303x::WM16(u32 Addr, PAIR *p)
{
	WM(Addr, p->b.h);
	WM((Addr + 1) & 0xffff, p->b.l);
}

// ---------------------------------------------------------------------------
// internal registers (hd6303x_io map)
// ---------------------------------------------------------------------------

u8 hd6303x::internal_r(u8 reg)
{
	switch (reg)
	{
	case 0x01: return 0xff; // P2 DDR is write-only
	case 0x03: // p2_data_r
	{
		u8 ddr = m_port_ddr[1];
		if (ddr == 0xff)
			return m_port_data[1];
		u8 in = in_p2 ? in_p2() : 0xff;
		return (in & ~ddr) | (m_port_data[1] & ddr);
	}
	case 0x08: // tcsr_r
		m_pending_tcsr = 0;
		return m_tcsr;
	case 0x09: // ch_r
		if (!(m_pending_tcsr & TCSR_TOF))
			m_tcsr &= ~TCSR_TOF;
		return m_counter.b.h;
	case 0x0a: return m_counter.b.l;
	case 0x0b: return m_output_compare[0].b.h;
	case 0x0c: return m_output_compare[0].b.l;
	case 0x0d: // icrh_r
		if (!(m_pending_tcsr & TCSR_ICF))
			m_tcsr &= ~TCSR_ICF;
		return (m_input_capture >> 8) & 0xff;
	case 0x0e: return m_input_capture & 0xff;
	case 0x0f: // tcsr2_r
		m_pending_tcsr &= ~(TCSR_ICF | TCSR_OCF);
		m_pending_tcsr2 = 0;
		return m_tcsr2 | (m_tcsr & (TCSR_ICF | TCSR_OCF)) | 0x10;
	case 0x10: return m_rmcr;
	case 0x11: // sci_trcsr_r
		if (m_trcsr & M6801_TRCSR_TDRE) m_trcsr_read_tdre = 1;
		if (m_trcsr & M6801_TRCSR_ORFE) m_trcsr_read_orfe = 1;
		if (m_trcsr & M6801_TRCSR_RDRF) m_trcsr_read_rdrf = 1;
		return m_trcsr;
	case 0x12: // sci_rdr_r
		if (m_trcsr_read_orfe)
		{
			m_trcsr_read_orfe = 0;
			m_trcsr &= ~M6801_TRCSR_ORFE;
		}
		if (m_trcsr_read_rdrf)
		{
			m_trcsr_read_rdrf = 0;
			m_trcsr &= ~M6801_TRCSR_RDRF;
		}
		return m_rdr;
	case 0x13: return 0xff; // TDR write-only
	case 0x14: return m_ram_ctrl | 0x30;
	case 0x15: // p5_data_r
	{
		u8 data = in_p5 ? in_p5() : 0xff;
		if (m_irq_state[IRQ1_LINE]) data &= 0xfe;
		if (m_irq_state[IRQ2_LINE]) data &= 0xfd;
		return data;
	}
	case 0x16: return 0xff; // P6 DDR write-only
	case 0x17: // p6_data_r
	{
		u8 ddr = m_portx_ddr[1];
		if (ddr == 0xff)
			return m_portx_data[1];
		u8 in = in_p6 ? in_p6() : 0xff;
		return (in & ~ddr) | (m_portx_data[1] & ddr);
	}
	case 0x19: return m_output_compare[1].b.h;
	case 0x1a: return m_output_compare[1].b.l;
	case 0x1b: return m_tcsr3;
	case 0x1c: return 0xff; // TCONR write-only
	case 0x1d: return m_t2cnt;
	}
	return 0xff;
}

void hd6303x::internal_w(u8 reg, u8 data)
{
	switch (reg)
	{
	case 0x01: // p2_ddr_2bit_w: HD6301X DDR2 is 2-bit
	{
		u8 ddr = (BIT(data, 1) ? 0xfe : 0x00) | (data & 0x01);
		if (m_port_ddr[1] != ddr)
		{
			m_port_ddr[1] = ddr;
			write_port2();
		}
		break;
	}
	case 0x03: // p2_data_w
		m_port_data[1] = data;
		m_port2_written = true;
		write_port2();
		break;
	case 0x08: // tcsr_w
		data &= 0x1f;
		m_tcsr = data | (m_tcsr & 0xe0);
		m_pending_tcsr &= m_tcsr;
		check_irq2();
		break;
	case 0x09: // ch_w
		m_latch09 = data;
		CT = 0xfff8;
		TOH = CTH;
		modified_counters();
		break;
	case 0x0a: // cl_w
		CT = (m_latch09 << 8) | data;
		TOH = CTH;
		modified_counters();
		break;
	case 0x0b: // ocrh_w
		if (!(m_pending_tcsr & TCSR_OCF))
			m_tcsr &= ~TCSR_OCF;
		if (m_output_compare[0].b.h != data)
		{
			m_output_compare[0].b.h = data;
			modified_counters();
		}
		break;
	case 0x0c: // ocrl_w
		if (!(m_pending_tcsr & TCSR_OCF))
			m_tcsr &= ~TCSR_OCF;
		if (m_output_compare[0].b.l != data)
		{
			m_output_compare[0].b.l = data;
			modified_counters();
		}
		break;
	case 0x0f: // tcsr2_w
		data &= TCSR2_OE1 | TCSR2_OE2 | TCSR2_OLVL2 | TCSR2_EOCI2;
		m_tcsr2 = data | (m_tcsr2 & TCSR2_OCF2);
		m_pending_tcsr2 &= m_tcsr2;
		check_irq2();
		break;
	case 0x10: set_rmcr(data); break;
	case 0x11: // sci_trcsr_w
		if ((data & M6801_TRCSR_TE) && !(m_trcsr & M6801_TRCSR_TE))
		{
			m_txstate = M6801_TX_STATE_INIT;
			m_txbits = 0;
			m_tx = 1;
		}
		if ((data & M6801_TRCSR_RE) && !(m_trcsr & M6801_TRCSR_RE))
			m_rxbits = 0;
		m_trcsr = (m_trcsr & 0xe0) | (data & 0x1f);
		break;
	case 0x13: // sci_tdr_w
		if (m_trcsr_read_tdre)
		{
			m_trcsr_read_tdre = 0;
			m_trcsr &= ~M6801_TRCSR_TDRE;
		}
		m_tdr = data;
		break;
	case 0x14: // rcr_w
		m_ram_ctrl = data;
		check_irq_lines();
		break;
	case 0x16: // p6_ddr_w
		if (m_portx_ddr[1] != data)
		{
			m_portx_ddr[1] = data;
			write_port6();
		}
		break;
	case 0x17: // p6_data_w
		m_portx_data[1] = data;
		write_port6();
		break;
	case 0x19: // ocr2h_w
		if (!(m_pending_tcsr2 & TCSR2_OCF2))
			m_tcsr2 &= ~TCSR2_OCF2;
		if (m_output_compare[1].b.h != data)
		{
			m_output_compare[1].b.h = data;
			modified_counters();
		}
		break;
	case 0x1a: // ocr2l_w
		if (!(m_pending_tcsr2 & TCSR2_OCF2))
			m_tcsr2 &= ~TCSR2_OCF2;
		if (m_output_compare[1].b.l != data)
		{
			m_output_compare[1].b.l = data;
			modified_counters();
		}
		break;
	case 0x1b: // tcsr3_w
	{
		u8 tout3_last_enable = (m_tcsr3 & 0x0c) != 0;
		m_tcsr3 = data & (0x5f | (m_tcsr3 & 0x80));
		if (m_tout3 && !BIT(data, 4))
		{
			m_tout3 = false;
			write_port2();
		}
		else if (tout3_last_enable ? (data & 0x0c) == 0 : (data & 0x0c) != 0)
		{
			m_port2_written = true;
			write_port2();
		}
		check_irq2();
		break;
	}
	case 0x1c: m_tconr = data; break;
	case 0x1d:
		m_t2cnt = data;
		m_t2cnt_written = true;
		break;
	default:
		break;
	}
}

void hd6303x::write_port2()
{
	if (!m_port2_written) return;

	u8 ddr = m_port_ddr[1];
	if (m_tcsr2 & TCSR2_OE1) ddr |= 0x02;
	if (m_tcsr2 & TCSR2_OE2) ddr |= 0x20;

	u8 data = (m_port_data[1] & ddr) | (ddr ^ 0xff);

	if (m_trcsr & M6801_TRCSR_TE)
	{
		data = (data & 0xef) | (m_tx << 4);
		ddr |= 0x10;
	}
	if ((m_tcsr3 & 0x0c) != 0)
	{
		data = (data & 0xbf) | (m_tout3 << 6);
		ddr |= 0x40;
	}

	if (out_p2) out_p2(data, ddr);
}

void hd6303x::write_port6()
{
	if (out_p6)
		out_p6((m_portx_data[1] & m_portx_ddr[1]) | (m_portx_ddr[1] ^ 0xff), m_portx_ddr[1]);
}

// ---------------------------------------------------------------------------
// interrupts
// ---------------------------------------------------------------------------

void hd6303x::enter_interrupt(const char *, u16 irq_vector)
{
	int cycles_to_eat = 0;

	if (m_wai_state & M6800_WAI)
	{
		cycles_to_eat = 4;
		m_wai_state &= ~M6800_WAI;
	}
	else
	{
		PUSHWORD(pPC);
		PUSHWORD(pX);
		PUSHBYTE(A);
		PUSHBYTE(B);
		PUSHBYTE(CC);
		cycles_to_eat = 12;
	}
	SEI;
	PCD = RM16(irq_vector);

	increment_counter(cycles_to_eat);
}

void hd6303x::check_irq_lines()
{
	if (m_nmi_pending)
	{
		m_wai_state &= ~M6800_SLP;
		m_nmi_pending = false;
		enter_interrupt("NMI", 0xfffc);
	}
	else if (check_irq1_enabled())
	{
		m_wai_state &= ~M6800_SLP;
		if (!(CC & 0x10))
			enter_interrupt("IRQ1", 0xfff8);
	}
	else
		check_irq2();
}

bool hd6303x::check_irq1_enabled()
{
	return m_irq_state[IRQ1_LINE] && (m_ram_ctrl & 1);
}

void hd6303x::take_irq2(const char *message, u16 irq_vector)
{
	m_wai_state &= ~M6800_SLP;
	if (!(m_cc & 0x10))
		enter_interrupt(message, irq_vector);
}

void hd6303x::check_irq2()
{
	if ((m_tcsr & (TCSR_EICI | TCSR_ICF)) == (TCSR_EICI | TCSR_ICF))
		take_irq2("ICI", 0xfff6);
	else if ((m_tcsr & (TCSR_EOCI | TCSR_OCF)) == (TCSR_EOCI | TCSR_OCF) ||
			(m_tcsr2 & (TCSR2_EOCI2 | TCSR2_OCF2)) == (TCSR2_EOCI2 | TCSR2_OCF2))
		take_irq2("OCI", 0xfff4);
	else if ((m_tcsr & (TCSR_ETOI | TCSR_TOF)) == (TCSR_ETOI | TCSR_TOF))
		take_irq2("TOI", 0xfff2);
	else if ((m_tcsr3 & 0xc0) == 0xc0)
		take_irq2("CMI", 0xffec);
	else if (m_irq_state[IRQ2_LINE] && (m_ram_ctrl & 2))
		take_irq2("IRQ2", 0xffea);
	else if (((m_trcsr & (M6801_TRCSR_RIE | M6801_TRCSR_RDRF)) == (M6801_TRCSR_RIE | M6801_TRCSR_RDRF)) ||
			((m_trcsr & (M6801_TRCSR_RIE | M6801_TRCSR_ORFE)) == (M6801_TRCSR_RIE | M6801_TRCSR_ORFE)) ||
			((m_trcsr & (M6801_TRCSR_TIE | M6801_TRCSR_TDRE)) == (M6801_TRCSR_TIE | M6801_TRCSR_TDRE)))
		take_irq2("SCI", 0xfff0);
}

void hd6303x::take_trap()
{
	enter_interrupt("TRAP", 0xffee);
}

void hd6303x::set_irq(int line, bool asserted)
{
	m_irq_state[line] = asserted ? 1 : 0;
}

void hd6303x::set_nmi(bool asserted)
{
	if (!m_nmi_state && asserted)
		m_nmi_pending = true;
	m_nmi_state = asserted;
}

// ---------------------------------------------------------------------------
// timers
// ---------------------------------------------------------------------------

void hd6303x::set_timer_event()
{
	m_timer_next = (OCD < TOD) ? OCD : TOD;
	if (OC2D < m_timer_next)
		m_timer_next = OC2D;
}

void hd6303x::modified_counters()
{
	OCH = (OC >= CT) ? CTH : CTH + 1;
	OC2H = (OC2 >= CT) ? CTH : CTH + 1;
	set_timer_event();
}

void hd6303x::check_timer_event()
{
	// OCI
	if (CTD >= OCD)
	{
		OCH++;
		m_tcsr |= TCSR_OCF;
		m_pending_tcsr |= TCSR_OCF;

		if (m_tcsr2 & TCSR2_OE1)
		{
			m_port_data[1] &= ~2;
			m_port_data[1] |= (m_tcsr & TCSR_OLVL) << 1;
			m_port2_written = true;
			write_port2();
		}
	}
	if (CTD >= OC2D)
	{
		OC2H++;
		m_tcsr2 |= TCSR2_OCF2;
		m_pending_tcsr2 |= TCSR2_OCF2;

		if (m_tcsr2 & TCSR2_OE2)
		{
			if (m_tcsr2 & TCSR2_OLVL2)
				m_port_data[1] |= 0x20;
			else
				m_port_data[1] &= ~0x20;
			m_port2_written = true;
			write_port2();
		}
	}

	// TOI
	if (CTD >= TOD)
	{
		TOH++;
		m_tcsr |= TCSR_TOF;
		m_pending_tcsr |= TCSR_TOF;
	}

	check_irq2();
	set_timer_event();
}

void hd6303x::increment_counter(int amount)
{
	m_icount -= amount;

	if (m_t2cnt_written)
		m_t2cnt_written = false;
	else if (BIT(m_tcsr3, 4))
	{
		switch (m_tcsr3 & 0x03)
		{
		case 0x00: increment_t2cnt(amount); break;
		case 0x01: increment_t2cnt((amount + (CTD & 0x0007)) >> 3); break;
		case 0x02: increment_t2cnt((amount + (CTD & 0x007f)) >> 7); break;
		case 0x03: break; // external Tclk
		}
	}

	// internally clocked SCI (replaces MAME's emu_timer)
	if (m_sci_internal_divisor)
	{
		m_sci_internal_count += amount;
		while (m_sci_internal_count >= m_sci_internal_divisor)
		{
			m_sci_internal_count -= m_sci_internal_divisor;
			sci_tick();
		}
	}

	CTD += amount;
	if (CTD >= m_timer_next || (m_tcsr3 & 0xc0) == 0xc0)
		check_timer_event();
}

void hd6303x::increment_t2cnt(int amount)
{
	while (amount--)
	{
		if (++m_t2cnt == ((m_tconr + 1) & 0xff))
		{
			m_t2cnt = 0;

			if (BIT(m_tcsr3, 3))
			{
				if (m_tout3 != bool(BIT(m_tcsr3, 2)))
				{
					m_tout3 = BIT(m_tcsr3, 2);
					m_port2_written = true;
					write_port2();
				}
			}
			else if (BIT(m_tcsr3, 2))
			{
				m_tout3 = !m_tout3;
				m_port2_written = true;
				write_port2();
			}

			if (BIT(m_rmcr, 5) && !m_use_ext_serclock)
				sci_clock_internal(32);

			m_tcsr3 |= 0x80;
			m_timer_next = 0; // HACK (as in MAME)
		}
	}
}

void hd6303x::eat_cycles()
{
	if (BIT(m_tcsr3, 4))
	{
		while (m_icount > 0 && (m_wai_state & (M6800_WAI | M6800_SLP)))
			increment_counter(1);
	}
	else
	{
		int cycles_to_eat = std::min(int(m_timer_next - CTD), m_icount);
		if (cycles_to_eat > 0)
			increment_counter(cycles_to_eat);
		else if (m_icount > 0 && int(m_timer_next - CTD) <= 0)
			increment_counter(1);
	}
}

void hd6303x::cleanup_counters()
{
	OC2H -= CTH;
	OCH -= CTH;
	TOH -= CTH;
	CTH = 0;
	set_timer_event();
	if (CTD >= m_timer_next)
		check_timer_event();
}

// ---------------------------------------------------------------------------
// SCI
// ---------------------------------------------------------------------------

void hd6303x::reset_sci_timer()
{
	m_sci_clocks = 0;
	m_sci_internal_divisor = 0;
	m_sci_internal_count = 0;
}

void hd6303x::set_rmcr(u8 data)
{
	if (m_rmcr == data) return;
	m_rmcr = data;

	switch ((m_rmcr & 0x1c) >> 2)
	{
	case 0: // TODO: clock sync
	case 3:
	case 7: // external clock
		reset_sci_timer();
		m_use_ext_serclock = true;
		break;

	case 1:
	case 2:
	case 4:
	case 5:
	case 6:
		reset_sci_timer();
		if (!BIT(m_rmcr, 5))
			m_sci_internal_divisor = M6801_RMCR_SS[m_rmcr & M6801_RMCR_SS_MASK];
		m_use_ext_serclock = false;
		break;
	}
}

int hd6303x::rx_line()
{
	return rx_level ? 1 : 0;
}

void hd6303x::serial_transmit()
{
	if (m_trcsr & M6801_TRCSR_TE)
	{
		int old_m_tx = m_tx;

		m_port_ddr[1] |= M6801_PORT2_IO4;

		switch (m_txstate)
		{
		case M6801_TX_STATE_INIT:
			m_tx = 1;
			m_txbits++;
			if (m_txbits == 10)
			{
				m_txstate = M6801_TX_STATE_READY;
				m_txbits = M6801_SERIAL_START;
			}
			break;

		case M6801_TX_STATE_READY:
			switch (m_txbits)
			{
			case M6801_SERIAL_START:
				if (m_trcsr & M6801_TRCSR_TDRE)
					return; // nothing to send
				m_tshr = m_tdr;
				m_trcsr |= M6801_TRCSR_TDRE;
				m_tx = 0;
				m_txbits++;
				break;

			case M6801_SERIAL_STOP:
				m_tx = 1;
				check_irq_lines();
				m_txbits = M6801_SERIAL_START;
				break;

			default:
				m_tx = m_tshr & 0x01;
				m_tshr >>= 1;
				m_txbits++;
				break;
			}
			break;
		}

		if (old_m_tx != m_tx && out_ser_tx)
			out_ser_tx(m_tx);
		m_port2_written = true;
		write_port2();
	}
}

void hd6303x::serial_receive()
{
	if (m_trcsr & M6801_TRCSR_RE)
	{
		if (m_trcsr & M6801_TRCSR_WU)
		{
			if (rx_line() == 1)
			{
				m_rxbits++;
				if (m_rxbits == 10)
				{
					m_trcsr &= ~M6801_TRCSR_WU;
					m_rxbits = M6801_SERIAL_START;
				}
			}
			else
				m_rxbits = M6801_SERIAL_START;
		}
		else
		{
			switch (m_rxbits)
			{
			case M6801_SERIAL_START:
				if (rx_line() == 0)
					m_rxbits++;
				break;

			case M6801_SERIAL_STOP:
				if (rx_line() == 1)
				{
					if (m_trcsr & M6801_TRCSR_RDRF)
					{
						m_trcsr |= M6801_TRCSR_ORFE; // overrun
						check_irq_lines();
					}
					else if (!(m_trcsr & M6801_TRCSR_ORFE))
					{
						m_rdr = m_rsr;
						m_trcsr |= M6801_TRCSR_RDRF;
						check_irq_lines();
					}
				}
				else
				{
					// framing error
					if (!(m_trcsr & M6801_TRCSR_ORFE))
						m_rdr = m_rsr;
					m_trcsr |= M6801_TRCSR_ORFE;
					m_trcsr &= ~M6801_TRCSR_RDRF;
					check_irq_lines();
				}
				m_rxbits = M6801_SERIAL_START;
				break;

			default:
				m_rsr >>= 1;
				m_rsr |= (rx_line() << 7);
				m_rxbits++;
				break;
			}
		}
	}
}

void hd6303x::sci_tick()
{
	serial_transmit();
	serial_receive();
}

void hd6303x::sci_clock_internal(u8 divider)
{
	m_sci_clocks++;
	if (m_sci_clocks >= divider)
	{
		m_sci_clocks = 0;
		sci_tick();
	}
}

void hd6303x::clock_serial()
{
	if (m_use_ext_serclock)
		sci_clock_internal(m_sclk_divider);
}

// ---------------------------------------------------------------------------
// execution
// ---------------------------------------------------------------------------

void hd6303x::execute_one()
{
	pPPC = pPC;
	u8 ireg = M_RDOP(PCD);
	PC++;
	(this->*hd63701_insn[ireg])();
	increment_counter(cycles_63701[ireg]);
}

int hd6303x::run(int cycles)
{
	m_icount += cycles;
	int start = m_icount;

	check_irq_lines();
	cleanup_counters();

	while (m_icount > 0)
	{
		if (m_wai_state & (M6800_WAI | M6800_SLP))
			eat_cycles();
		else
			execute_one();
	}
	return start - m_icount;
}

} // namespace hd6303
