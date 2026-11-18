// txnative: TX81Z sound engine without the HD6303X - drives the YM2414 (ymfm) directly and writes the
// same register values as the v1.6 firmware (single mode).
//
// The engine keeps the firmware's own RAM variables at their CPU addresses (internal RAM $00-$FF,
// external RAM $6000-$7FFF) and reads its tables from the ROM image at $8000-$FFFF (bank 1), so every
// routine is a direct port of the firmware routine named in its comment (reference ports with tests:
// tools/native/fwport.py, test_port.py; notes: docs/native-engine.md). Nothing of the ROM is copied
// into the source: the user's ROM file is loaded at init.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "opz/opzcore.h"   // YM2414 core, bit-identical to ymfm

namespace txn {

class Engine
{
public:
	static constexpr uint32_t OPZ_CLOCK = 3579545;
	static constexpr int PHASE = 292;   // samples between the two alternating halves of the timer task
	static constexpr int VOICES = 8;

	Engine();
	bool init(const uint8_t *rom, size_t len, std::string &error);
	uint32_t sample_rate() const { return OPZ_CLOCK / 64; }

	// VCED (93 bytes) and ACED (23 bytes) parameter blocks, as in the SysEx voice dumps
	void load_voice(const uint8_t *vced, const uint8_t *aced);
	// one voice parameter as by a SysEx parameter change (group 'V' VCED 0..92 / 'A' ACED 0..22);
	// sounding notes go on unless the firmware itself resets the voices for that parameter
	void set_param(char group, int num, int value);
	void note_on(int key, int vel);
	void note_off(int key);
	void control_change(int cc, int value);
	void pitch_bend(int value14);          // 0..16383, centre 8192 (the firmware uses the MSB)
	void master_tune(int mt);              // 0..127, 64 = centre

	void render(float *left, float *right, int n);

	// every register write (for comparison with the firmware)
	std::function<void(uint8_t reg, uint8_t val)> on_write;
	uint8_t peek(uint16_t addr) const { return m_mem[addr]; }
	// chip output of the last rendered sample (14-bit, before scaling), for comparisons
	int32_t last_raw(int ch) const { return m_last_raw[ch]; }
	// test hooks: replace the RAM ($00-$FF, $6000-$7FFF) and run one half of the timer task
	void test_set_ram(const uint8_t *iram256, const uint8_t *xram8k)
	{
		std::copy(iram256, iram256 + 0x100, m_mem.begin());
		std::copy(xram8k, xram8k + 0x2000, m_mem.begin() + 0x6000);
	}
	void test_phase(bool b) { if (b) phase_b(); else phase_a(); }
	// test hook: samples until the next timer phase and which phase comes next
	void set_phase(int samples_to_next, bool next_is_b) { m_phase_pos = PHASE - samples_to_next; m_phase_b = next_is_b; }

private:
	// memory with the firmware's addresses
	uint8_t r(uint32_t a) const { return m_mem[a & 0xffff]; }
	void w(uint32_t a, int v) { m_mem[a & 0xffff] = uint8_t(v); }
	uint16_t r16(uint32_t a) const { return uint16_t(r(a) << 8 | r(a + 1)); }
	void w16(uint32_t a, int v) { w(a, v >> 8); w(a + 1, v); }

	void write(uint8_t reg, uint8_t val);
	void power_on();
	void reset_controllers();
	void build_levels();
	int vp(int i) const;
	int op_v(int oi, int f) const;
	int op_a(int oi, int f) const;
	void clean_voice(bool load);
	int mul_field(int oi, int &dt2) const;
	void write_80();
	void write_a0();
	void write_c0();
	void write_20();
	void write_40();
	void write_40_alt();
	void write_c0_alt();
	void write_38();
	void write_lfo();
	void write_e0();
	void damp_all();
	void voice_ram();

	// timer task
	void phase_a();
	void phase_b();
	void bend_all();
	int bc_bias(int ch);
	void soft_lfo();
	void pitch_depth();
	void keycode();
	void lfo_delay();
	void channel_volume(int ch);
	void tl_refresh();
	void am_depth();
	int lfo_pick(int k) const;

	// notes
	int note_level(int oi, int key, int vel) const;
	void key_off_voice(int c);
	void damp(int c);
	int note_pitch(int key, int c) const;
	void start_voice(int c, int key, int vel);
	void mono_note_on(int key, int vel);
	void mono_note_off(int key);
	void mono_key_off(int c);
	void mono_off_slot(int sl);
	void all_notes_off();
	void set_mono(bool mono);
	void reset_voices();
	void set_flag(int c, int f);
	void note_keycode(int c);
	void write_keycode(int c, int d, int cur);

	OpzCore m_opz;
	std::vector<uint8_t> m_mem;
	bool m_ready = false;
	int32_t m_last_raw[2]{};

	uint8_t m_c_alt[32]{};      // C0-DF alt shadow (EG shift | 0x28 | reverb), RAM $77EE
	uint8_t m_r20 = 0x80;

	// voices: round robin and note-on order queue ($82BA); flags as RAM $7CE3 (bit 1 held, bit 0 sustained)
	int m_next = 0;
	int m_key[VOICES];
	uint8_t m_flag[VOICES]{};
	int m_queue[VOICES];
	int m_qlen = 0;

	int m_phase_pos = 0;
	bool m_phase_b = true;
};

} // namespace txn
