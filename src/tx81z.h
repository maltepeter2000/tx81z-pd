// TX81Z machine: HD6303X + YM2414 (ymfm) + HD44780 + 8 KB battery RAM.
//
// Memory map and port wiring follow MAME src/mame/yamaha/ymtx81z.cpp
// (BSD-3-Clause, copyright-holders: AJR):
//   $0000-$001F  CPU internal registers, $0040-$00FF CPU internal RAM
//   $2000-$3FFF  YM2414 (A0 selects address/data), mirrored
//   $4000-$5FFF  HD44780 LCD (A0 selects command/data), mirrored
//   $6000-$7FFF  8 KB battery-backed RAM
//   $8000-$FFFF  32 KB window into the 64 KB ROM, bank = P6 bit 3
//   IRQ1 <- YM2414 IRQ, SCI clocked externally from a 500 kHz resonator
//
// Clocks (service manual + MAME): crystal 7.15909 MHz, CPU E = xtal/4,
// OPZ = xtal/2 (3.579545 MHz), OPZ sample rate = OPZ clock / 64 (~55.93 kHz).
// One OPZ sample = exactly 32 CPU cycles.
#pragma once

#include "cpu/hd6303x.h"
#include "hd44780.h"
#include "ymfm_opz.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class TX81Z : public ymfm::ymfm_interface
{
public:
	enum Button
	{
		BTN_STORE, BTN_UTILITY, BTN_EDIT, BTN_PLAY,
		BTN_PARAM_LEFT, BTN_PARAM_RIGHT, BTN_DEC, BTN_INC,
		BTN_VOL_LEFT, BTN_VOL_RIGHT, BTN_CURSOR,
		BTN_COUNT
	};

	static constexpr uint32_t OPZ_CLOCK = 3579545;            // 7.15909 MHz / 2
	static constexpr uint32_t CPU_CYCLES_PER_SAMPLE = 32;     // E = xtal/4, sample = OPZ/64
	static constexpr uint32_t MIDI_CLOCK = 500000;            // ceramic resonator CL1

	TX81Z();

	bool load_rom(const std::string &path, std::string &error);
	bool load_nvram(const std::string &path);
	bool save_nvram(const std::string &path) const;

	void reset();

	uint32_t sample_rate() const { return m_opz.sample_rate(OPZ_CLOCK); }

	// render n samples at the native OPZ rate; buffers may be null
	void render(float *left, float *right, int n);

	// MIDI in (raw bytes, queued and shifted in at 31250 baud)
	void midi_in(const uint8_t *data, size_t len);
	void midi_in(std::initializer_list<uint8_t> bytes) { midi_in(bytes.begin(), bytes.size()); }
	size_t midi_in_pending() const { return size_t((m_rx_head - m_rx_tail) & RX_MASK) + (m_rx_bit >= 0 ? 1 : 0); }

	void set_button(Button b, bool pressed) { m_buttons[b] = pressed; }
	static bool button_from_name(const std::string &name, Button &b);

	std::string lcd_line(int n) const { return m_lcd.line(n); }
	void lcd_line_chars(int n, char *out17) const { m_lcd.line_chars(n, out17); }   // real-time safe
	bool lcd_changed() { return m_lcd.dirty(); }
	bool lcd_changed_peek() const { return m_lcd.dirty_peek(); }
	uint8_t leds() const { return m_leds; }
	double time() const { return double(m_samples) / sample_rate(); }

	// hooks
	std::function<void(uint8_t reg, uint8_t val)> on_opz_write;
	std::function<void(uint8_t byte)> on_midi_out;

	hd6303::hd6303x &cpu() { return m_cpu; }

	// ymfm_interface
	void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;
	void ymfm_update_irq(bool asserted) override;

private:
	uint8_t bus_read(uint16_t addr);
	void bus_write(uint16_t addr, uint8_t data);
	uint8_t port2_in();
	uint8_t port5_in();
	uint8_t port6_in();
	void serial_clock();

	hd6303::hd6303x m_cpu;
	ymfm::ym2414 m_opz;
	HD44780 m_lcd;

	std::vector<uint8_t> m_rom;      // 64 KB
	std::array<uint8_t, 0x2000> m_ram{};
	int m_rombank = 0;
	uint8_t m_leds = 0;
	uint8_t m_opz_addr = 0;

	std::array<bool, BTN_COUNT> m_buttons{};

	// OPZ timers (in OPZ input clocks, -1 = stopped)
	int64_t m_timer[2] = { -1, -1 };

	// serial
	uint64_t m_serial_acc = 0;
	// fixed-size ring buffer: no allocation in the audio thread
	static constexpr uint32_t RX_SIZE = 4096, RX_MASK = RX_SIZE - 1;
	uint8_t m_rx_buf[RX_SIZE];
	uint32_t m_rx_head = 0, m_rx_tail = 0;
	int m_rx_bit = -1;              // -1 idle, 0 start, 1-8 data, 9 stop
	uint8_t m_rx_byte = 0;
	int m_tx_level = 1;
	int m_tx_bit = -1;
	uint8_t m_tx_byte = 0;

	uint64_t m_samples = 0;
};
