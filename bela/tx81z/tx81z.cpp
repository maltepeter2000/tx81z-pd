#include "tx81z.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

TX81Z::TX81Z()
	: m_opz(*this)
{
	m_rom.assign(0x10000, 0xff);

	m_cpu.mem_ctx = this;
	m_cpu.read_mem = [](void *ctx, uint16_t a) { return static_cast<TX81Z *>(ctx)->bus_read(a); };
	m_cpu.write_mem = [](void *ctx, uint16_t a, uint8_t d) { static_cast<TX81Z *>(ctx)->bus_write(a, d); };
	m_cpu.in_p2 = [this]() { return port2_in(); };
	m_cpu.in_p5 = [this]() { return port5_in(); };
	m_cpu.in_p6 = [this]() { return port6_in(); };
	m_cpu.out_p2 = [](uint8_t, uint8_t) {};
	m_cpu.out_p6 = [this](uint8_t data, uint8_t) {
		m_rombank = (data >> 3) & 1;   // P63: ROM bank
		m_leds = (data >> 4) & 0x0f;   // P64-P67: LED1-4
	};
	m_cpu.out_ser_tx = [this](int state) { m_tx_level = state; };
}

bool TX81Z::load_rom(const std::string &path, std::string &error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) { error = "cannot open " + path; return false; }
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (data.size() != 0x10000)
	{
		error = "expected a 64 KB (27512) image, got " + std::to_string(data.size()) + " bytes";
		return false;
	}
	m_rom = std::move(data);
	return true;
}

bool TX81Z::load_nvram(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	f.read(reinterpret_cast<char *>(m_ram.data()), m_ram.size());
	return f.gcount() == std::streamsize(m_ram.size());
}

bool TX81Z::save_nvram(const std::string &path) const
{
	std::ofstream f(path, std::ios::binary);
	if (!f) return false;
	f.write(reinterpret_cast<const char *>(m_ram.data()), m_ram.size());
	return bool(f);
}

void TX81Z::reset()
{
	m_rombank = 0;   // MAME: first configured bank entry
	m_leds = 0;
	m_timer[0] = m_timer[1] = -1;
	m_opz.reset();
	m_lcd.reset();
	m_cpu.rx_level = 1;
	m_rx_bit = -1;
	m_rx_head = m_rx_tail = 0;
	m_tx_bit = -1;
	m_cpu.reset();
}

// ---------------------------------------------------------------------------
// bus
// ---------------------------------------------------------------------------

uint8_t TX81Z::bus_read(uint16_t addr)
{
	switch (addr >> 13)
	{
	case 1: return m_opz.read(addr & 1);                 // $2000-$3FFF
	case 2: return m_lcd.read(addr & 1);                 // $4000-$5FFF
	case 3: return m_ram[addr & 0x1fff];                 // $6000-$7FFF
	case 4: case 5: case 6: case 7:
		return m_rom[(m_rombank << 15) | (addr & 0x7fff)];
	default: return 0xff;
	}
}

void TX81Z::bus_write(uint16_t addr, uint8_t data)
{
	switch (addr >> 13)
	{
	case 1:
		if (addr & 1)
		{
			if (on_opz_write) on_opz_write(m_opz_addr, data);
		}
		else
			m_opz_addr = data;
		m_opz.write(addr & 1, data);
		break;
	case 2: m_lcd.write(addr & 1, data); break;
	case 3: m_ram[addr & 0x1fff] = data; break;
	default: break;                                     // ROM / unmapped
	}
}

// ---------------------------------------------------------------------------
// ports (bit assignments from MAME ymtx81z.cpp, buttons active low)
// ---------------------------------------------------------------------------

uint8_t TX81Z::port2_in()
{
	uint8_t v = 0x12;                         // P21 cassette in idle, P24 high
	if (!m_buttons[BTN_CURSOR])    v |= 0x20;
	if (!m_buttons[BTN_VOL_RIGHT]) v |= 0x40;
	if (!m_buttons[BTN_VOL_LEFT])  v |= 0x80;
	v |= (m_cpu.rx_level & 1) << 3;           // P23 = MIDI In data
	return v;
}

uint8_t TX81Z::port5_in()
{
	uint8_t v = 0xff;                         // P50 = YM2414 IRQ, inserted by the CPU
	if (m_buttons[BTN_STORE])       v &= ~0x02;
	if (m_buttons[BTN_INC])         v &= ~0x10;
	if (m_buttons[BTN_DEC])         v &= ~0x20;
	if (m_buttons[BTN_PARAM_RIGHT]) v &= ~0x40;
	if (m_buttons[BTN_PARAM_LEFT])  v &= ~0x80;
	return v;
}

uint8_t TX81Z::port6_in()
{
	uint8_t v = 0x0f;
	if (m_buttons[BTN_PLAY])    v &= ~0x01;
	if (m_buttons[BTN_EDIT])    v &= ~0x02;
	if (m_buttons[BTN_UTILITY]) v &= ~0x04;
	return v;
}

bool TX81Z::button_from_name(const std::string &n, Button &b)
{
	static const struct { const char *name; Button b; } names[] = {
		{ "store", BTN_STORE }, { "utility", BTN_UTILITY }, { "edit", BTN_EDIT },
		{ "play", BTN_PLAY }, { "param-", BTN_PARAM_LEFT }, { "param+", BTN_PARAM_RIGHT },
		{ "dec", BTN_DEC }, { "inc", BTN_INC }, { "vol-", BTN_VOL_LEFT },
		{ "vol+", BTN_VOL_RIGHT }, { "cursor", BTN_CURSOR },
	};
	for (auto &e : names)
		if (n == e.name) { b = e.b; return true; }
	return false;
}

// ---------------------------------------------------------------------------
// OPZ timers / IRQ
// ---------------------------------------------------------------------------

void TX81Z::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks)
{
	m_timer[tnum] = duration_in_clocks < 0 ? -1 : duration_in_clocks;
}

void TX81Z::ymfm_update_irq(bool asserted)
{
	m_cpu.set_irq(hd6303::hd6303x::IRQ1_LINE, asserted);
}

// ---------------------------------------------------------------------------
// MIDI serial (31250 baud = 500 kHz / 16)
// ---------------------------------------------------------------------------

void TX81Z::midi_in(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		uint32_t next = (m_rx_head + 1) & RX_MASK;
		if (next == m_rx_tail) break;          // full: drop (like an overrun on hardware)
		m_rx_buf[m_rx_head] = data[i];
		m_rx_head = next;
	}
}

// called once per MIDI bit period (500 kHz / 16 = 31250 Hz)
void TX81Z::serial_clock()
{
	{

		// shift the next MIDI In bit onto P23
		if (m_rx_bit < 0 && m_rx_tail != m_rx_head)
		{
			m_rx_byte = m_rx_buf[m_rx_tail];
			m_rx_tail = (m_rx_tail + 1) & RX_MASK;
			m_rx_bit = 0;
		}
		if (m_rx_bit == 0) m_cpu.rx_level = 0;                       // start
		else if (m_rx_bit >= 1 && m_rx_bit <= 8) m_cpu.rx_level = (m_rx_byte >> (m_rx_bit - 1)) & 1;
		else m_cpu.rx_level = 1;                                     // stop / idle
		if (m_rx_bit >= 0 && ++m_rx_bit > 9) m_rx_bit = -1;
	}

	m_cpu.clock_serial_bit();

	{
		// decode MIDI Out from the SCI TX line
		if (m_tx_bit < 0)
		{
			if (m_tx_level == 0) { m_tx_bit = 1; m_tx_byte = 0; }
		}
		else if (m_tx_bit <= 8)
		{
			m_tx_byte |= uint8_t((m_tx_level & 1) << (m_tx_bit - 1));
			m_tx_bit++;
		}
		else
		{
			if (m_tx_level == 1 && on_midi_out) on_midi_out(m_tx_byte);
			m_tx_bit = -1;
		}
	}
}

// ---------------------------------------------------------------------------
// main loop: one OPZ sample = 32 CPU cycles
// ---------------------------------------------------------------------------

void TX81Z::render(float *left, float *right, int n)
{
	ymfm::ym2414::output_data out;
	const uint64_t serial_step = uint64_t(MIDI_CLOCK / 16) * 64; // bit ticks per sample, in OPZ-clock units

	for (int i = 0; i < n; i++)
	{
		m_cpu.run(CPU_CYCLES_PER_SAMPLE);

		m_serial_acc += serial_step;
		while (m_serial_acc >= OPZ_CLOCK)
		{
			m_serial_acc -= OPZ_CLOCK;
			serial_clock();
		}

		// OPZ timers advance 64 input clocks per sample
		for (int t = 0; t < 2; t++)
		{
			if (m_timer[t] < 0) continue;
			m_timer[t] -= 64;
			if (m_timer[t] <= 0)
			{
				int64_t overshoot = -m_timer[t];
				m_timer[t] = -1;
				m_engine->engine_timer_expired(t);
				if (m_timer[t] > 0) m_timer[t] -= overshoot;
			}
		}

		m_opz.generate(&out);
		// MAME routes both outputs at 0.60
		float l = float(out.data[0]) * (0.60f / 32768.0f);
		float r = float(out.data[1]) * (0.60f / 32768.0f);
		if (left) left[i] = l < -1.0f ? -1.0f : l > 1.0f ? 1.0f : l;
		if (right) right[i] = r < -1.0f ? -1.0f : r > 1.0f ? 1.0f : r;
		m_samples++;
	}
}
