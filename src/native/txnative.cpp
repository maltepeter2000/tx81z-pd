#include "txnative.h"

#include <algorithm>
#include <cstring>

namespace txn {

namespace {

// coarse 0..63 -> (MUL, DT2): all MUL x DT2 ratios sorted, duplicates removed (see model.py)
struct Crs { uint8_t mul, dt2; };
std::array<Crs, 64> make_crs()
{
	static const int mul10[16] = { 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150 };
	static const int dt2[4] = { 100, 141, 157, 173 };
	struct E { int r; int m, d; };
	E all[64];
	int n = 0;
	for (int m = 0; m < 16; m++)
		for (int d = 0; d < 4; d++)
			all[n++] = { mul10[m] * dt2[d], m, d };
	std::sort(all, all + 64, [](const E &a, const E &b) {
		return a.r != b.r ? a.r < b.r : (a.m != b.m ? a.m < b.m : a.d < b.d);
	});
	std::array<Crs, 64> out{};
	int k = 0, last = -1;
	for (auto &e : all)
	{
		if (e.r == last) continue;
		last = e.r;
		if (k < 64) out[k++] = { uint8_t(e.m), uint8_t(e.d) };
	}
	return out;
}
const std::array<Crs, 64> CRS = make_crs();
const uint8_t DT1[7] = { 7, 6, 5, 0, 1, 2, 3 };

inline int sat8(int x) { return x > 255 ? 255 : x; }
inline int hi(int a, int b) { return (a * b) >> 8; }
inline int slot(int ch, int oi) { return ch + 8 * oi; }   // oi: 0 = OP4 ... 3 = OP1
inline int scale(int x) { return ((x * 0xA5) << 2 >> 8) & 0xff; }   // $908B

uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; i++)
	{
		c ^= p[i];
		for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

// ROM tables and RAM variables (CPU addresses)
enum : uint32_t
{
	T_OUT_ATT = 0x9FA8, T_LS_CURVE = 0x9FBC, T_KEY = 0xA2F0, T_VEL = 0xDF94, T_ATT = 0xA82B, T_VOLDIFF = 0xA92B,
	T_EBS = 0x9D89, T_LFO16 = 0x9B6D, T_PORT = 0xA06D, T_PBR = 0xA0E4, T_BEND = 0xA4B8, T_BCPB = 0xA485,

	VOICE = 0x6A67,
};

} // namespace

Engine::Engine()
	: m_opz(*this), m_mem(0x10000, 0)
{
	for (int c = 0; c < VOICES; c++) m_key[c] = -1;
}

bool Engine::init(const uint8_t *rom, size_t len, std::string &error)
{
	if (len != 0x10000) { error = "ROM must be 64 KB"; return false; }
	if (crc32(rom, len) != 0xab9b7347u) { error = "native engine needs the TX81Z v1.6 ROM"; return false; }
	std::memcpy(&m_mem[0x8000], rom + 0x8000, 0x8000);   // bank 1: code tables
	m_opz.reset();
	static const uint8_t boot[][2] = { { 0x09, 0x00 }, { 0x0f, 0x00 }, { 0x1c, 0x00 }, { 0x1e, 0x00 },
	                                   { 0x0a, 0x04 }, { 0x14, 0x70 }, { 0x15, 0x01 } };   // $909D
	for (auto &b : boot) write(b[0], b[1]);
	power_on();
	m_ready = true;
	return true;
}

void Engine::write(uint8_t reg, uint8_t val)
{
	if (on_write) on_write(reg, val);
	m_opz.write(0, reg);
	m_opz.write(1, val);
}

// single mode: one instrument (0) on all 8 voices, LFO 1
void Engine::power_on()
{
	for (int ch = 0; ch < 8; ch++)
	{
		w(0x7846 + ch, 0x01);
		w(0x793B + ch, 0x00);
		w(0x7933 + ch, 0x07);
		w16(0x7CF3 + 2 * ch, 0x1700);
		w16(0x7D03 + 2 * ch, 0x1700);
		w16(0x7796 + 2 * ch, 0xffff);
		w16(0x77A6 + 2 * ch, 0xffff);
	}
	const uint8_t lfo[5] = { 0x00, 0xFF, 0x00, 0xFF, 0x00 };
	for (int i = 0; i < 5; i++) w(0x784E + i, lfo[i]);
	w(0x7EF1, 0); w(0x7EF9, 7); w(0xF6, 8);
	w(0x7772, 0x02);
	reset_controllers();
	w16(0x86, 0);
	w16(0x7F29, 0x1700);
}

// $C832 (for all 8 instruments; single mode uses instrument 0)
void Engine::reset_controllers()
{
	for (int i = 0; i < 8; i++)
	{
		w(0x7D83 + i, 0x80);
		w(0x7D53 + i, 0);
		w(0x7D5B + i, 0);
		w(0x7D63 + i, 0xFE);
		w(0x41 + i, 0x40);
		w(0x49 + i, 0);
		w(0x7D6B + i, 0);
	}
}

// $A12D
void Engine::master_tune(int mt)
{
	int b = ((mt << 1) ^ 0x80) & 0xff;
	int d = (b & 0x80 ? 0xff00 : 0) | b;
	w16(0x86, (d << 1) & 0xffff);
}

// ---------------------------------------------------------------------------------------------
// voice load
// ---------------------------------------------------------------------------------------------

// $9F00 / $9F70 curves, $9FEC velocity offsets
void Engine::build_levels()
{
	const int alg = r(VOICE + 52);
	for (int oi = 0; oi < 4; oi++)
	{
		const uint32_t o = VOICE + oi * 13;
		int out = r(o + 10);
		int a = out < 20 ? r(T_OUT_ATT + out) : 99 - out;
		int add = 0;
		switch (alg)
		{
		case 4: add = (oi & 2) ? 8 : 0; break;
		case 5: case 6: add = oi ? 13 : 0; break;
		case 7: add = 16; break;
		default: break;
		}
		if (add) a = std::min(0x7f, a + add);
		const int base = 2 * a;
		const int depth = scale(r(o + 5));
		const int kvs = r(o + 9);
		for (int ch = 0; ch < 8; ch++)
		{
			for (int i = 0; i < 29; i++)
				w(0x7943 + oi * 0x1d + ch * 0x74 + i, sat8(base + ((r(T_LS_CURVE + i) * depth) >> 8)));
			w(0x78D3 + slot(ch, oi), kvs ? 15 - 2 * kvs : 0);
			w(0x78F3 + slot(ch, oi), 32 * kvs);
		}
	}
}

void Engine::load_voice(const uint8_t *vced, const uint8_t *aced)
{
	uint8_t v[93], a[23];
	std::memcpy(v, vced, 93);
	std::memcpy(a, aced, 23);
	for (int oi = 0; oi < 4; oi++)   // the firmware's clean-up of the stored voice
	{
		v[oi * 13 + 3] = std::max<uint8_t>(1, v[oi * 13 + 3]);
		if (v[oi * 13 + 11] < 4) a[oi * 5 + 2] = std::min<uint8_t>(7, a[oi * 5 + 2]);
	}
	a[19] = 0;
	for (int i = 0; i < 87; i++) w(VOICE + i, v[i]);
	for (int i = 0; i < 23; i++) w(VOICE + 87 + i, a[i]);

	// ---- registers ($94CE), all 8 channels
	const int alg = v[52], fb = v[53];
	auto fld = [&](int oi, int f) { return int(v[oi * 13 + f]); };
	auto afl = [&](int oi, int f) { return int(a[oi * 5 + f]); };
	auto mulfield = [&](int oi, int &dt2) {
		int crs = fld(oi, 11);
		dt2 = CRS[crs].dt2;
		if (afl(oi, 0)) return (afl(oi, 1) << 4) | (crs >> 2);
		return (DT1[fld(oi, 12)] << 4) | CRS[crs].mul;
	};
	auto each = [&](auto fn) { for (int oi = 0; oi < 4; oi++) for (int ch = 0; ch < 8; ch++) fn(oi, ch); };

	for (int ch = 0; ch < 8; ch++)
	{
		for (int oi = 0; oi < 4; oi++) write(uint8_t(0xE0 + slot(ch, oi)), 0xFE);
		for (int oi = 0; oi < 4; oi++) write(uint8_t(0xC0 + slot(ch, oi)), 0x28);
	}
	for (int ch = 0; ch < 8; ch++) write(0x08, uint8_t(ch));
	each([&](int oi, int ch) { write(uint8_t(0x80 + slot(ch, oi)), uint8_t((fld(oi, 6) << 6) | (afl(oi, 0) << 5) | fld(oi, 0))); });
	each([&](int oi, int ch) { write(uint8_t(0xA0 + slot(ch, oi)), uint8_t((fld(oi, 8) << 7) | fld(oi, 1))); });
	each([&](int oi, int ch) { int d; mulfield(oi, d); write(uint8_t(0xC0 + slot(ch, oi)), uint8_t((d << 6) | fld(oi, 2))); });
	each([&](int oi, int ch) { write(uint8_t(0xC0 + slot(ch, oi)), 0x28); });
	m_r20 = uint8_t(0x80 | (fb << 3) | alg);
	for (int ch = 0; ch < 8; ch++) { w(0x7913 + ch, m_r20); write(uint8_t(0x20 + ch), m_r20); }
	each([&](int oi, int ch) { int d; write(uint8_t(0x40 + slot(ch, oi)), uint8_t(mulfield(oi, d))); });
	each([&](int oi, int ch) { write(uint8_t(0x40 + slot(ch, oi)), uint8_t(0x80 | (afl(oi, 3) << 4) | afl(oi, 2))); });
	const int rev = a[20];
	each([&](int oi, int ch) {
		uint8_t val = uint8_t((afl(oi, 4) << 6) | 0x28 | rev);   // a[19] (OP1) is 0
		m_c_alt[slot(ch, oi)] = val;
		write(uint8_t(0xC0 + slot(ch, oi)), val);
	});
	for (int ch = 0; ch < 8; ch++) write(uint8_t(0x38 + ch), uint8_t((v[60] << 4) | v[61]));
	const int lfw = v[59], lfs = v[54];
	write(0x1B, uint8_t(lfw));
	int d = scale(lfs);
	write(0x18, uint8_t(lfw == 3 ? d : 255 - r(T_ATT + d)));   // $9B02
	each([&](int oi, int ch) { write(uint8_t(0xE0 + slot(ch, oi)), uint8_t(((15 - fld(oi, 4)) << 4) | fld(oi, 3))); });

	// ---- RAM (per channel copies of the instrument's values)
	for (int ch = 0; ch < 8; ch++)
	{
		w(0x7D3B + ch, scale(v[71]));
		w(0x7D43 + ch, scale(v[73]));
		w(0x7D4B + ch, scale(a[21]));
		w(0x7D7B + ch, scale(v[56]));
		int ad = 99 - v[55], n = 7 - ((ad & 0x70) >> 4);                  // $9CF5
		w16(0x780E + 2 * ch, (((((ad & 0x0f) | 0x10) << 1) & 0xff) << 8) >> n);
		w16(0x781E + 2 * ch, r16(T_LFO16 + 2 * v[54]));
		w(0x782E + ch, v[58]);
		w(0x77C6 + ch, r(T_PORT + v[66]));
		w(0x7923 + ch, r(T_PBR + v[64]));
		w(0x792B + ch, v[75] - 50);
		w(0x7D1B + ch, scale(v[67]));
		w(0x7D33 + ch, scale(v[76]));
		w(0x791B + ch, v[62] - 24);
		for (int oi = 0; oi < 4; oi++) w(0x7853 + slot(ch, oi), r(T_EBS + v[oi * 13 + 7]));
	}
	w(0x7D13, scale(v[57]));
	w(0x7D15, scale(v[72]));
	w(0x7D17, scale(v[74]));
	w(0x7D19, scale(a[22]));
	w(0x7852, v[59]);
	build_levels();

	// voices reset ($90D5)
	for (int ch = 0; ch < 8; ch++)
	{
		w16(0x7CF3 + 2 * ch, 0x1700);
		w16(0x7D03 + 2 * ch, 0x1700);
		m_key[ch] = -1;
		set_flag(ch, 0);
		w(0x7CE3 + 2 * ch, 0);
	}
	w16(0x7F29, 0x1700);
	m_next = 0;
	m_qlen = 0;
}

// ---------------------------------------------------------------------------------------------
// notes
// ---------------------------------------------------------------------------------------------

// $A155: per-note level (RAM $78A3), half TL steps
int Engine::note_level(int oi, int key, int vel) const
{
	int ch = 0;   // curves and offsets are the same on every channel
	int b = key - 13 + int(int8_t(r(0x791B)));
	while (b < 0) b += 12;
	while (b >= 0x60) b -= 12;
	int g = std::clamp(int(r(T_KEY + b)) - 14, 0, 0x70) >> 2;
	int curve = r(0x7943 + oi * 0x1d + ch * 0x74 + g);
	int v = sat8(r(0x78D3 + slot(ch, oi)) + hi(r(0x78F3 + slot(ch, oi)), r(T_VEL + (vel & 0x7f))));
	return sat8(v + curve);
}

// $82BA: round robin from the voice after the last one used; held or sustained voices are skipped;
// all busy -> steal the oldest note
void Engine::note_on(int key, int vel)
{
	if (vel == 0) { note_off(key); return; }
	int c = -1;
	for (int i = 0; i < VOICES; i++)
	{
		int t = (m_next + i) % VOICES;
		if (!(m_flag[t] & 3)) { c = t; break; }
	}
	if (c < 0)
	{
		c = m_queue[0];
		std::memmove(m_queue, m_queue + 1, sizeof(int) * --m_qlen);
	}
	for (int oi = 0; oi < 4; oi++) write(uint8_t(0xE0 + slot(c, oi)), 0xFE);
	for (int oi = 0; oi < 4; oi++) write(uint8_t(0xC0 + slot(c, oi)), uint8_t(m_c_alt[slot(c, oi)] & 0xE8));
	write(0x08, uint8_t(c));
	// pitch: glide from the last note ($8363)
	// $89D9: note - 13 + transpose, folded into 0..95 (8-bit signed test as in the firmware)
	int pa = (key - 13) & 0xff, t = r(0x791B + c);
	int sum = int8_t(pa) + int8_t(t);
	pa = (pa + t) & 0xff;
	bool lt = (sum >= -128 && sum <= 127) ? sum < 0 : sum >= 0;
	while (lt)
	{
		int sa = int8_t(pa) + 12;
		pa = (pa + 12) & 0xff;
		lt = (sa >= -128 && sa <= 127) ? sa < 0 : sa >= 0;
	}
	while (pa >= 0x60) pa -= 12;
	int pitch = pa << 8;
	w16(0x7D03 + 2 * c, r16(0x7F29));
	w16(0x7CF3 + 2 * c, pitch);
	w16(0x7F29, pitch);
	m_key[c] = key;
	w(0x7CE3 + 2 * c, key);
	set_flag(c, 0x02);
	m_queue[m_qlen++] = c;
	m_next = (c + 1) % VOICES;
	// LFO delay restarts ($A354)
	if (r16(0x780E) != 0x1300) { w16(0x7796, 0); w16(0x77A6, 0); }
	for (int oi = 0; oi < 4; oi++)
	{
		int s = slot(c, oi);
		int lv = note_level(oi, key, vel);
		w(0x78A3 + s, lv);
		int t = lv == 0xff ? 0xff : sat8(lv + r(0x7873 + s));
		write(uint8_t(0x60 + s), uint8_t(t >> 1));
	}
	note_keycode(c);
	if (r(0x782E + c)) write(0x1B, uint8_t(0x10 | r(0x7852)));   // LFO sync ($8525)
	if (r(0x782E)) w16(0x7836, 0x3fff);
	write(uint8_t(0x20 + c), uint8_t(0x40 | r(0x7913 + c)));
}

// $A210: key code / fraction of the new voice right away (from the glide start when gliding)
void Engine::note_keycode(int c)
{
	int inst = r(0x793B + c);
	uint32_t vx = VOICE + uint32_t(inst >= 8 ? 0 : inst) * 0x6E;
	bool glide;
	if (r(vx + 0x3F) && r(vx + 0x41)) glide = true;
	else glide = !(inst & 0x80) && (r(0x41 + inst) & 0x40);
	if (glide && r(0x77C6 + c) == 0xff) glide = false;
	int base = r16((glide ? 0x7D03 : 0x7CF3) + 2 * c);
	auto add_clamped = [](int d, int x) {
		int sd = int16_t(d), sx = int16_t(x);
		if (sd + sx < -0x8000 || sd + sx > 0x7fff) return (d + x > 0xffff) ? 0x0000 : 0x7fff;
		return (d + x) & 0xffff;
	};
	int d = add_clamped(r16(0x86), r16(0x78C3 + 2 * c));
	if (c != 0 || (r(0x7846) & 3) == 3) d = add_clamped(d, r16(0x77B6 + 2 * c));
	write_keycode(c, d, base);
}

// fold the semitone into 0..95 (the signed test uses N ^ V of the 16-bit add) and write 28+c / 30+c
void Engine::write_keycode(int c, int d, int cur)
{
	int tot = int16_t(d) + int16_t(cur);
	int rr = tot & 0xffff;
	bool lt = (tot >= -0x8000 && tot <= 0x7fff) ? tot < 0 : tot >= 0;
	int a = rr >> 8, b = rr & 0xff;
	while (lt)
	{
		int sa = int8_t(a) + 12;
		a = (a + 12) & 0xff;
		lt = (sa >= -128 && sa <= 127) ? sa < 0 : sa >= 0;
	}
	while (a >= 0x60) a -= 12;
	write(uint8_t(0x28 + c), r(T_KEY + a));
	write(uint8_t(0x30 + c), uint8_t((b & 0xFC) | (r(0x7933 + c) & 1)));
}

// voice flags, mirrored in RAM $7CE4 + 2c as the firmware keeps them (4 = note from MIDI)
void Engine::set_flag(int c, int f)
{
	m_flag[c] = uint8_t(f);
	w(0x7CE4 + 2 * c, f ? f | 4 : 0);
}

void Engine::key_off_voice(int c)
{
	set_flag(c, 0);
	int *q = std::find(m_queue, m_queue + m_qlen, c);
	if (q != m_queue + m_qlen) { std::memmove(q, q + 1, sizeof(int) * (m_queue + m_qlen - q - 1)); m_qlen--; }
	write(0x08, uint8_t(c));
	write(uint8_t(0x20 + c), uint8_t(r(0x7913 + c) & 0xBF));
}

// $8596: the first held voice with this key; with sustain it stays sounding (bit 0)
void Engine::note_off(int key)
{
	for (int c = 0; c < VOICES; c++)
	{
		if (m_key[c] != key || !(m_flag[c] & 2)) continue;
		if (r(0x49) & 0x80) { set_flag(c, (m_flag[c] | 1) & ~2); return; }
		key_off_voice(c);
		return;
	}
}

void Engine::control_change(int cc, int value)
{
	value &= 0x7f;
	switch (cc)
	{
	case 1: w(0x7D53, value << 1); break;                      // mod wheel
	case 2: w(0x7D5B, value << 1); break;                      // breath
	case 4: w(0x7D63, value << 1); break;                      // foot
	case 7: w(0x7D6B, ((~(value << 1)) - 1) & 0xff); break;   // volume
	case 5:                                                    // portamento time ($CE6A)
	{
		int p = (value * 0xC8) >> 8;
		if (p != r(VOICE + 66))
		{
			w(VOICE + 66, p);
			for (int ch = 0; ch < 8; ch++) w(0x77C6 + ch, r(T_PORT + p));
		}
		break;
	}
	case 64:                                                   // sustain ($CEA0, $885F)
		if (value >= 0x40) w(0x41, r(0x41) | 0x80);
		else w(0x41, r(0x41) & 0x7f);
		if ((r(0x49) & 0x80) && !(r(0x41) & 0x80))
			for (int c = 0; c < VOICES; c++)
				if (m_flag[c] & 1) key_off_voice(c);
		w(0x49, r(0x41) & 0x80);
		break;
	case 65:                                                   // portamento on / off ($CEEB)
		if (value >= 0x40) w(0x41, r(0x41) | 0x40);
		else w(0x41, r(0x41) & 0xbf);
		break;
	default: break;
	}
}

void Engine::pitch_bend(int value14)
{
	w(0x7D83, ((value14 >> 7) << 1) & 0xff);   // $CCEF: MSB only
}

// ---------------------------------------------------------------------------------------------
// timer task ($8F1F), see tools/native/fwport.py for the reference ports
// ---------------------------------------------------------------------------------------------

void Engine::phase_a()
{
	bend_all();
	soft_lfo();
	pitch_depth();
	keycode();
}

void Engine::phase_b()
{
	lfo_delay();
	const int start = r(0x91) ? 4 : 0;
	for (int ch = start; ch < start + 4; ch++) channel_volume(ch);
	tl_refresh();
	am_depth();
}

// $A390
void Engine::bend_all()
{
	for (int ch = 7; ch >= 0; ch--)
	{
		int inst = r(0x793B + ch);
		if (inst & 0x80) { w16(0x78C3 + 2 * ch, 0); continue; }
		int b = r(0x7D83 + inst);
		int d = 0;
		if (b != 0x80)
		{
			int a = ((r(T_BEND + (b >> 1)) << 1) & 0xff) ^ 0x80;
			w(0x8C, a);
			int rng = r(0x7923 + ch);
			if (rng != 0)
			{
				if (rng == 0xC2 && !(a & 0x80) && a == 0x7E)
					d = 0x0C00;
				else if (rng == 0xC2 && (a & 0x80))
				{
					int x = (~(((-a) & 0xff) * 0xC0)) & 0xffff;
					int lo = ((x & 0xff) + 1) & 0xff;
					d = ((((x >> 8) + 1) & 0xff) << 8 | lo) >> 3;
				}
				else
				{
					if (!(a & 0x80)) d = ((a << 1) & 0xff) * rng;
					else d = (~((((~a) & 0xff) << 1 & 0xff) * rng)) & 0xffff;
					d >>= 4;
				}
				if (a & 0x80) d |= 0xF000;
			}
		}
		w16(0x89, d);
		w16(0x78C3 + 2 * ch, (bc_bias(ch) + d) & 0xffff);
	}
}

// $A400
int Engine::bc_bias(int ch)
{
	int bb = r(0x792B + ch);
	if (bb == 0) return 0;
	int inst = r(0x793B + ch);
	int a = (inst & 0x80) ? 0 : r(0x7D5B + inst);
	if (!(inst & 0x80) && a == 0) return 0;
	int d;
	if (bb == 0x31 || bb == 0x32 || !(bb & 0x80))
		d = ((bb == 0x31 || bb == 0x32) && a >= 0xFE) ? 0xC000 : a * r(T_BCPB + bb);
	else
	{
		int nb = (-bb) & 0xff;
		d = (nb >= 0x31 && a >= 0xFE) ? 0xC000 : a * r(T_BCPB + nb);
		d = (((~d) & 0xffff) + 1) & 0xffff;
		if (d == 0) return 0;
	}
	d >>= 2;
	if (bb & 0x80) d |= 0xC000;
	return d;
}

// $AAF7
void Engine::soft_lfo()
{
	for (int b = 0; b < 8; b++)
	{
		if (b != 0 && (r(0x7846 + b) & 3) != 3) { w16(0x77B6 + 2 * b, 0); continue; }
		int ph = (r16(0x7836 + 2 * b) + r16(0x781E + 2 * b)) & 0xffff;
		w16(0x7836 + 2 * b, ph);
		int a = (ph >> 7) & 0xff;
		if (ph & 0x8000) a = (~a) & 0xff;
		if (b == 0) w(0x776A, a);
		int lv = (a + 0x80) & 0xff;
		int dep = r(0x7D7B + b) + r(0x7D73 + b);
		if ((dep & 0xff) == 0) { w16(0x77B6 + 2 * b, 0); continue; }
		dep = sat8(dep);
		int k = hi(dep, r(0x77A6 + 2 * b));
		int d = !(lv & 0x80) ? lv * k : (((~(((-lv) & 0xff) * k)) & 0xffff) + 1) & 0xffff;
		int h = (d >> 8) & 0xff;
		d = ((h & 0x80) ? 0xff00 : 0) | h;
		w16(0x77B6 + 2 * b, (d << 1) & 0xffff);
	}
}

int Engine::lfo_pick(int k) const
{
	int b = r(0x7850);
	if (b == 0 || b == 0xff) return r(k == 0 ? 0x784E : 0x784F);
	return r(k == 0 ? 0x784F : 0x784E);
}

// $AA1A
void Engine::pitch_depth()
{
	const int half = r(0x91) ? 4 : 0;
	w(0x92, half);
	for (int c = 0; c < 4; c++)
	{
		int ch = c + half, inst = r(0x793B + ch);
		if (inst & 0x80) continue;
		int mw = hi(r(0x7D3B + ch), r(0x7D53 + inst));
		w(0x7D73 + ch, mw);
		int fc = hi(r(0x7D4B + ch), r(0x7D63 + inst));
		int bc = hi(r(0x7D43 + ch), r(0x7D5B + inst));
		int a = bc + fc;
		a = a > 0xff ? 0xff : sat8(a + mw);
		w(0x7D73 + ch, a);
	}
	const int k = r(0x91) ? 1 : 0;
	int sel = r(k == 0 ? 0x7850 : 0x7851), a = 0;
	if (!(sel & 0x80))
	{
		a = hi(r(0x7D7B + sel), r(0x77A6 + 2 * sel));
		int b = lfo_pick(k);
		if (!(b & 0x80)) a = sat8(a + r(0x7D73 + r(0x7EF1 + b)));
	}
	write(k == 0 ? 0x19 : 0x17, uint8_t((a >> 1) | 0x80));
}

// $A538
void Engine::keycode()
{
	for (int ch = 7; ch >= 0; ch--)
	{
		const uint32_t p = 0x7CF3 + 2 * ch;
		int inst = r(0x793B + ch);
		bool glide;
		uint32_t vx = VOICE + uint32_t(inst >= 8 ? 0 : inst) * 0x6E;   // $9A07
		if (r(vx + 0x3F) && r(vx + 0x41)) glide = true;
		else glide = !(inst & 0x80) && (r(0x41 + inst) & 0x40);
		int tgt = r16(p), cur = r16(p + 0x10);
		if (!glide) cur = tgt;
		else
		{
			int diff = (tgt - cur) & 0xffff, rate = r(0x77C6 + ch);
			if (diff == 0 || rate == 0xff) cur = tgt;
			else if (!(diff & 0x8000))
			{
				int n = (((((diff >> 8) >> 2) + 1) * rate) + cur) & 0xffff;
				cur = n < tgt ? n : tgt;
			}
			else
			{
				int n = (cur - (((((-(diff >> 8)) & 0xff) >> 2) + 1) * rate)) & 0xffff;
				cur = n >= tgt ? n : tgt;
			}
		}
		w16(p + 0x10, cur);
		int d = (r16(0x86) + r16(0x78C3 + 2 * ch)) & 0xffff;
		if (ch != 0 || (r(0x7846) & 3) == 3)
		{
			int x = r16(0x77B6 + 2 * ch);
			int sd = int16_t(d), sx = int16_t(x);
			if (sd + sx < -0x8000 || sd + sx > 0x7fff) d = (d + x > 0xffff) ? 0x0000 : 0x7fff;
			else d = (d + x) & 0xffff;
		}
		write_keycode(ch, d, cur);
	}
}

// $A64D
void Engine::lfo_delay()
{
	for (int b = 0; b < 8; b++)
	{
		int rate = r16(0x780E + 2 * b);
		int d = std::min(0xffff, r16(0x7796 + 2 * b) + rate);
		w16(0x7796 + 2 * b, d);
		if (d != 0xffff) continue;
		w16(0x77A6 + 2 * b, std::min(0xffff, r16(0x77A6 + 2 * b) + rate));
	}
}

// $A69B
void Engine::channel_volume(int ch)
{
	int inst = r(0x793B + ch);
	if (inst & 0x80) return;
	int br = r(0x7D5B + inst);
	w(0x7D23 + ch, r(0x7D33 + ch) - hi(r(0x7D33 + ch), br));
	int fcv = r(0x7D1B + ch);
	w(0x7D2B + ch, hi(fcv, r(0x7D63 + inst)));
	int a = ((fcv - r(0x7D2B + ch)) & 0xff) + r(0x7D6B + inst);
	a = (~sat8(a)) & 0xff;
	a = r(T_ATT + a) + r(T_ATT + ((~r(0x776D)) & 0xff));
	if (a > 0xff) a = 0xff;
	else if (r(0x776B) & 2) a = sat8(a + r(T_ATT + ((~r(0x7753 + inst)) & 0xff)));
	w(0x7D2B + ch, a);
	int nv = a + r(T_ATT + ((~r(0x7D23 + ch)) & 0xff));
	int old = r16(0x7893 + 2 * ch);
	w16(0x7893 + 2 * ch, nv);
	if (old == nv) return;
	int diff = old > nv ? old - nv : nv - old;
	write(uint8_t(ch), diff >= 0x20 ? 0 : r(T_VOLDIFF + diff));
}

// $A769
void Engine::tl_refresh()
{
	if (r(0x91) == 0) { w(0x91, 0xff); w(0x92, 0); }
	else { w(0x91, 0); w(0x92, 4); }
	const int half = r(0x92);
	for (int oi = 3; oi >= 0; oi--)
		for (int c = 3; c >= 0; c--)
		{
			int ch = c + half, s = slot(ch, oi);
			int a = r(T_ATT + ((~hi(r(0x7853 + s), r(0x7D23 + ch))) & 0xff));
			int alg = r(0x7913 + ch) & 7, bb = alg - 4;
			bool car = bb < 0 ? oi == 3 : bb == 0 ? (oi & 2) != 0 : bb < 3 ? oi != 0 : true;
			if (car) a = sat8(a + r(0x7D2B + ch));
			w(0x7873 + s, a);
			int t = sat8(a + r(0x78A3 + s));
			int bit7 = ((r(0x776B) & 2) && r(0x7767)) ? 0 : 0x80;
			write(uint8_t(0x60 + s), uint8_t(bit7 | (t >> 1)));
		}
}

// $A94B
void Engine::am_depth()
{
	const int k = r(0x91) ? 1 : 0;
	const int b = lfo_pick(k);
	int f2 = hi(r(0x7D15 + k), r(0x7D53 + b));
	int f3 = hi(r(0x7D17 + k), r(0x7D5B + b));
	int f5 = hi(r(0x7D19 + k), r(0x7D63 + b));
	int s = f2 + f3;
	s = s > 0xff ? 0xff : sat8(s + f5);
	int sel = r(0x7850 + k);
	int dly = (sel & 0x80) ? 0 : r(0x77A6 + sel);
	int a = sat8(hi(r(0x7D13 + k), dly) + s);
	a = r(T_ATT + ((~a) & 0xff));
	if (!(sel & 0x80))
	{
		int bias = r(0x7873 + sel);
		a = (sat8(a + bias) - bias) & 0xff;
	}
	write(k == 0 ? 0x19 : 0x17, uint8_t(a >> 1));
}

void Engine::render(float *left, float *right, int n)
{
	ymfm::ym2414::output_data out;
	for (int i = 0; i < n; i++)
	{
		if (m_ready && ++m_phase_pos >= PHASE)
		{
			m_phase_pos = 0;
			if (m_phase_b) phase_b(); else phase_a();
			m_phase_b = !m_phase_b;
		}
		m_opz.generate(&out);
		float l = float(out.data[0]) * (0.60f / 32768.0f);
		float rr = float(out.data[1]) * (0.60f / 32768.0f);
		if (left) left[i] = std::clamp(l, -1.0f, 1.0f);
		if (right) right[i] = std::clamp(rr, -1.0f, 1.0f);
	}
}

} // namespace txn
