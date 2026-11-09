// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// Standalone Hitachi HD6303X CPU core.
//
// Ported from MAME's m6800/m6801 cores (src/devices/cpu/m6800, BSD-3-Clause,
// copyright-holders: Aaron Giles) with the MAME device framework removed.
// The opcode implementations (6800ops.hxx) are used verbatim.
//
// What is emulated (HD6303X only):
//   - full HD6301/6303 instruction set incl. AIM/OIM/EIM/TIM/XGDX/SLP, TRAP
//   - internal registers $00-$1F, internal RAM $40-$FF
//   - port 2, 5, 6 (port 1, 3, 4 and 7 are the external bus in expanded mode)
//   - timer 1 (FRC, OCR1, OCR2, ICR), timer 2 (8-bit, TCONR)
//   - SCI, bit-level, internal or external clock (clock_serial())
//   - IRQ1, IRQ2, NMI and all internal interrupt sources
//
// External memory is accessed through the read/write callbacks.

#pragma once

#include <cstdint>
#include <functional>

namespace hd6303 {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;

// MAME's PAIR (little-endian host assumed)
union PAIR
{
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	u32 d;
};

#include "6800ops_list.h"

class hd6303x
{
public:
	typedef void (hd6303x::*op_func)();

	enum { IRQ1_LINE = 0, IRQ2_LINE = 1 };

	hd6303x();

	// ---- external connections -------------------------------------------
	// external bus (plain function pointers: called for every memory access)
	u8 (*read_mem)(void *ctx, u16 addr) = nullptr;
	void (*write_mem)(void *ctx, u16 addr, u8 data) = nullptr;
	void *mem_ctx = nullptr;
	std::function<u8()> in_p2, in_p5, in_p6;    // port inputs
	std::function<void(u8 data, u8 ddr)> out_p2, out_p6;
	std::function<void(int state)> out_ser_tx;  // SCI TX line (P24)

	// ---- control ---------------------------------------------------------
	void reset();
	// run for (at least) the given number of E-clock cycles; returns cycles run
	int run(int cycles);
	void set_irq(int line, bool asserted);
	void set_nmi(bool asserted);
	// one tick of the external serial clock (P22/SCLK); 16 ticks = 1 bit
	void clock_serial();
	// one full bit period of the external serial clock (= 16 clock_serial calls)
	void clock_serial_bit() { if (m_use_ext_serclock) sci_tick(); }

	// debug access
	u16 pc() const { return m_pc.w.l; }
	u8 internal_ram(u8 addr) const { return m_iram[addr & 0xff]; }

	// internal RAM survives in standby on the real chip; expose for NVRAM
	u8 *internal_ram_ptr() { return &m_iram[0x40]; }

private:
	enum
	{
		M6800_WAI = 8,    // set when WAI is waiting for an interrupt
		M6800_SLP = 0x10  // HD63701 only
	};

	// ---- 6800 core state -------------------------------------------------
	PAIR m_ppc, m_pc, m_s, m_x, m_d, m_ea;
	u8 m_cc;
	u8 m_wai_state;
	u8 m_nmi_state;
	u8 m_nmi_pending;
	u8 m_irq_state[2];
	int m_icount;

	static const u8 flags8i[256];
	static const u8 flags8d[256];
	static const u8 cycles_63701[256];
	static const op_func hd63701_insn[256];

	// ---- 6801 / 6301X peripherals ---------------------------------------
	u8 m_iram[256];               // internal RAM ($40-$FF used)
	u8 m_port_ddr[2];             // [1] = port 2
	u8 m_port_data[2];
	u8 m_portx_ddr[2];            // [1] = port 6
	u8 m_portx_data[2];
	u8 m_tcsr, m_pending_tcsr;
	u8 m_tcsr2, m_pending_tcsr2;
	u8 m_ram_ctrl;
	PAIR m_counter;
	PAIR m_output_compare[2];
	u16 m_input_capture;
	bool m_port2_written;
	PAIR m_timer_over;
	u32 m_timer_next;
	u8 m_latch09;

	u8 m_t2cnt, m_tconr, m_tcsr3;
	bool m_tout3, m_t2cnt_written;

	// SCI
	u8 m_trcsr, m_rmcr, m_rdr, m_tdr, m_rsr, m_tshr;
	int m_rxbits, m_txbits, m_txstate, m_trcsr_read_tdre, m_trcsr_read_orfe, m_trcsr_read_rdrf, m_tx, m_sci_clocks;
	bool m_use_ext_serclock;
	int m_sclk_divider;
	int m_sci_internal_divisor;   // E cycles per SCI tick when internally clocked (0 = off)
	int m_sci_internal_count;

	// ---- memory ----------------------------------------------------------
	u8 RM(u16 addr);
	void WM(u16 addr, u8 data);
	u32 RM16(u32 addr);
	void WM16(u32 addr, PAIR *p);
	u8 internal_r(u8 reg);
	void internal_w(u8 reg, u8 data);

	// ---- execution -------------------------------------------------------
	void execute_one();
	void enter_interrupt(const char *message, u16 irq_vector);
	void check_irq_lines();
	bool check_irq1_enabled();
	void check_irq2();
	void take_irq2(const char *message, u16 irq_vector);
	void take_trap();
	void increment_counter(int amount);
	void eat_cycles();
	void cleanup_counters();

	void set_timer_event();
	void modified_counters();
	void check_timer_event();
	void increment_t2cnt(int amount);

	void set_rmcr(u8 data);
	void write_port2();
	void write_port6();
	int rx_line();
	void serial_transmit();
	void serial_receive();
	void sci_tick();
	void sci_clock_internal(u8 divider);
	void reset_sci_timer();

	void logerror(const char *, ...) {}

	// opcode handlers (MAME 6800ops.hxx)
#define HD6303X_DECLARE_OP(name) void name();
	HD6303X_OPS(HD6303X_DECLARE_OP)
#undef HD6303X_DECLARE_OP

public:
	// SCI receive line level as seen on P23 (1 = idle)
	int rx_level = 1;
};

} // namespace hd6303
