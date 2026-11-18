// txnative: TX81Z sound engine without the HD6303X - drives the YM2414 (ymfm) directly and writes the
// same register values as the v1.6 firmware (single mode). Reference: tools/native/model.py and
// docs/native-engine.md. Tables are read from the user's ROM image at init (nothing copied here).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "ymfm_opz.h"

namespace txn {

struct RomTables
{
	uint8_t out_att[20];     // $9FA8 output level 0..19 -> attenuation
	uint8_t ls_curve[29];    // $9FBC level scaling curve shape
	uint8_t key_group[96];   // $A2F0 semitone 0..95 -> key code
	uint8_t vel[128];        // $DF94 MIDI velocity -> velocity factor
	uint8_t mod_att[256];    // $A82B amount -> attenuation
	bool load(const uint8_t *rom, size_t len, std::string &error);
};

class Engine : public ymfm::ymfm_interface
{
public:
	static constexpr uint32_t OPZ_CLOCK = 3579545;
	static constexpr int TICK = 584;   // samples between periodic updates (firmware timer task)
	static constexpr int VOICES = 8;

	Engine();
	bool init(const uint8_t *rom, size_t len, std::string &error);
	uint32_t sample_rate() const { return m_opz.sample_rate(OPZ_CLOCK); }

	// VCED (93 bytes) and ACED (23 bytes) parameter blocks, as in the SysEx voice dumps
	void load_voice(const uint8_t *vced, const uint8_t *aced);
	void note_on(int key, int vel);
	void note_off(int key);

	void render(float *left, float *right, int n);

	// every register write (for comparison with the firmware)
	std::function<void(uint8_t reg, uint8_t val)> on_write;

private:
	struct Op { int ar, d1r, d2r, rr, d1l, ls, rs, ebs, ame, kvs, out, crs, det, fix, fixr, fin, osw, sh; };

	void write(uint8_t reg, uint8_t val);
	void tick();
	Op op(int k) const;                     // operator k = 1..4
	int base_att(const Op &o, int k) const; // half TL steps
	void build_curves();
	int note_level(int oi, int key, int vel) const;

	ymfm::ym2414 m_opz;
	RomTables m_t{};
	bool m_ready = false;

	uint8_t m_v[93]{}, m_a[23]{};
	uint8_t m_c_alt[32]{};                  // C0-DF alt shadow (EG shift | 0x28 | reverb)
	uint8_t m_r20 = 0x80;                   // 20-27 without key on
	std::array<std::array<uint8_t, 29>, 4> m_curve{};   // level scaling curve per operator index

	// voices
	int m_next = 0;
	int m_key[VOICES];
	int m_queue[VOICES];
	int m_qlen = 0;
	uint8_t m_level[32]{};                  // per-note level, half TL steps (RAM $78A3)
	int16_t m_pitch[VOICES];                // semitone << 8 | fraction (RAM $7CF3)

	// periodic task
	int m_tick_pos = 0;
	int m_half = 0;                         // TL refresh: channels 7..4 or 3..0
};

} // namespace txn
