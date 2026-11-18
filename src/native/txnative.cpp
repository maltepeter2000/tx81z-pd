#include "txnative.h"

#include <algorithm>
#include <cstring>

namespace txn {

namespace {

// coarse 0..63 -> (MUL, DT2): all MUL x DT2 ratios sorted, duplicates removed (see model.py)
struct Crs { uint8_t mul, dt2; };
std::array<Crs, 64> make_crs()
{
	static const int mul10[16] = { 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150 };   // x0.1
	static const int dt2[4] = { 100, 141, 157, 173 };   // x0.01
	struct E { int r; int m, d; };
	E all[64];
	int n = 0;
	for (int m = 0; m < 16; m++)
		for (int d = 0; d < 4; d++)
			all[n++] = { mul10[m] * dt2[d], m, d };   // ratio x 0.001, exact in integers
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
inline int slot(int ch, int oi) { return ch + 8 * oi; }   // oi: 0 = OP4 ... 3 = OP1

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

} // namespace

bool RomTables::load(const uint8_t *rom, size_t len, std::string &error)
{
	if (len != 0x10000) { error = "ROM must be 64 KB"; return false; }
	if (crc32(rom, len) != 0xab9b7347u) { error = "native engine needs the TX81Z v1.6 ROM"; return false; }
	std::memcpy(out_att, rom + 0x9FA8, sizeof out_att);
	std::memcpy(ls_curve, rom + 0x9FBC, sizeof ls_curve);
	std::memcpy(key_group, rom + 0xA2F0, sizeof key_group);
	std::memcpy(vel, rom + 0xDF94, sizeof vel);
	std::memcpy(mod_att, rom + 0xA82B, sizeof mod_att);
	return true;
}

Engine::Engine()
	: m_opz(*this)
{
	for (int c = 0; c < VOICES; c++) { m_key[c] = -1; m_pitch[c] = 0x1700; }
}

bool Engine::init(const uint8_t *rom, size_t len, std::string &error)
{
	if (!m_t.load(rom, len, error)) return false;
	m_opz.reset();
	// boot writes of the firmware ($909D)
	static const uint8_t boot[][2] = { { 0x09, 0x00 }, { 0x0f, 0x00 }, { 0x1c, 0x00 }, { 0x1e, 0x00 },
	                                   { 0x0a, 0x04 }, { 0x14, 0x70 }, { 0x15, 0x01 } };
	for (auto &b : boot) write(b[0], b[1]);
	m_ready = true;
	return true;
}

void Engine::write(uint8_t reg, uint8_t val)
{
	if (on_write) on_write(reg, val);
	m_opz.write(0, reg);
	m_opz.write(1, val);
}

Engine::Op Engine::op(int k) const
{
	const uint8_t *o = m_v + (4 - k) * 13;
	const uint8_t *x = m_a + (4 - k) * 5;
	return { o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9], o[10], o[11], o[12],
	         x[0], x[1], x[2], x[3], x[4] };
}

// $9F00: output level -> attenuation plus algorithm offset, in half TL steps
int Engine::base_att(const Op &o, int k) const
{
	int a = o.out < 20 ? m_t.out_att[o.out] : 99 - o.out;
	int oi = 4 - k, alg = m_v[52], add = 0;
	switch (alg)
	{
	case 4: add = (oi & 2) ? 8 : 0; break;
	case 5: case 6: add = oi ? 13 : 0; break;
	case 7: add = 16; break;
	default: break;
	}
	if (add) a = std::min(0x7f, a + add);
	return 2 * a;
}

// $9F70: 29-point level scaling curve per operator
void Engine::build_curves()
{
	for (int k = 1; k <= 4; k++)
	{
		Op o = op(k);
		int depth = (o.ls * 0xA5 * 4 >> 8) & 0xff;
		int base = base_att(o, k);
		for (int i = 0; i < 29; i++)
			m_curve[4 - k][i] = uint8_t(sat8(base + ((m_t.ls_curve[i] * depth) >> 8)));
	}
}

// $A155: per-note level, half TL steps
int Engine::note_level(int oi, int key, int vel) const
{
	Op o = op(4 - oi);
	int lo = o.kvs ? 15 - 2 * o.kvs : 0, mul = 32 * o.kvs;
	int v = sat8(lo + ((mul * m_t.vel[vel & 0x7f]) >> 8));
	int b = key - 13 + m_v[62] - 24;
	while (b < 0) b += 12;
	while (b >= 0x60) b -= 12;
	int g = std::clamp(int(m_t.key_group[b]) - 14, 0, 0x70) >> 2;
	return sat8(v + m_curve[oi][g]);
}

void Engine::load_voice(const uint8_t *vced, const uint8_t *aced)
{
	std::memcpy(m_v, vced, sizeof m_v);
	std::memcpy(m_a, aced, sizeof m_a);
	const int alg = m_v[52], fb = m_v[53];
	Op ops[4];
	for (int oi = 0; oi < 4; oi++) ops[oi] = op(4 - oi);
	auto mulfield = [&](const Op &o, int &dt2) {
		if (o.fix) { dt2 = CRS[o.crs].dt2; return (o.fixr << 4) | (o.crs >> 2); }
		dt2 = CRS[o.crs].dt2;
		return (DT1[o.det] << 4) | CRS[o.crs].mul;
	};
	auto each = [&](auto fn) {   // firmware order: OP4 ch 0..7, OP3 ..., OP1
		for (int oi = 0; oi < 4; oi++)
			for (int ch = 0; ch < 8; ch++) fn(oi, ch, ops[oi]);
	};

	for (int ch = 0; ch < 8; ch++)
	{
		for (int oi = 0; oi < 4; oi++) write(uint8_t(0xE0 + slot(ch, oi)), 0xFE);
		for (int oi = 0; oi < 4; oi++) write(uint8_t(0xC0 + slot(ch, oi)), 0x28);
	}
	for (int ch = 0; ch < 8; ch++) write(0x08, uint8_t(ch));
	each([&](int oi, int ch, const Op &o) { write(uint8_t(0x80 + slot(ch, oi)), uint8_t((o.rs << 6) | (o.fix << 5) | o.ar)); });
	each([&](int oi, int ch, const Op &o) { write(uint8_t(0xA0 + slot(ch, oi)), uint8_t((o.ame << 7) | o.d1r)); });
	each([&](int oi, int ch, const Op &o) { int d; mulfield(o, d); write(uint8_t(0xC0 + slot(ch, oi)), uint8_t((d << 6) | o.d2r)); });
	each([&](int oi, int ch, const Op &) { write(uint8_t(0xC0 + slot(ch, oi)), 0x28); });
	m_r20 = uint8_t(0x80 | (fb << 3) | alg);
	for (int ch = 0; ch < 8; ch++) write(uint8_t(0x20 + ch), m_r20);
	each([&](int oi, int ch, const Op &o) { int d; write(uint8_t(0x40 + slot(ch, oi)), uint8_t(mulfield(o, d))); });
	each([&](int oi, int ch, const Op &o) {
		int fin = o.crs < 4 ? std::min(o.fin, 7) : o.fin;
		write(uint8_t(0x40 + slot(ch, oi)), uint8_t(0x80 | (o.osw << 4) | fin));
	});
	const int rev = m_a[20];
	each([&](int oi, int ch, const Op &o) {
		int sh = oi == 3 ? 0 : o.sh;   // no EG shift on OP1
		uint8_t val = uint8_t((sh << 6) | 0x28 | rev);
		m_c_alt[slot(ch, oi)] = val;
		write(uint8_t(0xC0 + slot(ch, oi)), val);
	});
	for (int ch = 0; ch < 8; ch++) write(uint8_t(0x38 + ch), uint8_t((m_v[60] << 4) | m_v[61]));
	const int lfw = m_v[59], lfs = m_v[54];
	write(0x1B, uint8_t(lfw));
	int d = (lfs * 0xA5 * 4 >> 8) & 0xff;
	write(0x18, uint8_t(lfw == 3 ? d : 255 - m_t.mod_att[d]));   // $9B02
	each([&](int oi, int ch, const Op &o) { write(uint8_t(0xE0 + slot(ch, oi)), uint8_t(((15 - o.d1l) << 4) | std::max(1, o.rr))); });
	// BC EG bias -> channel volume 00-07: not used by ymfm, left out for now

	build_curves();
	m_next = 0;
	m_qlen = 0;
	for (int c = 0; c < VOICES; c++) { m_key[c] = -1; m_pitch[c] = 0x1700; }
}

// $82BA: round robin from the voice after the last one used; all held -> steal the oldest held note
void Engine::note_on(int key, int vel)
{
	if (vel == 0) { note_off(key); return; }
	int c = -1;
	for (int i = 0; i < VOICES; i++)
	{
		int t = (m_next + i) % VOICES;
		if (m_key[t] < 0) { c = t; break; }
	}
	if (c < 0)
	{
		c = m_queue[0];
		std::memmove(m_queue, m_queue + 1, sizeof(int) * --m_qlen);
	}
	for (int oi = 0; oi < 4; oi++) write(uint8_t(0xE0 + slot(c, oi)), 0xFE);
	for (int oi = 0; oi < 4; oi++) write(uint8_t(0xC0 + slot(c, oi)), uint8_t(m_c_alt[slot(c, oi)] & 0xE8));
	write(0x08, uint8_t(c));
	m_key[c] = key;
	m_queue[m_qlen++] = c;
	m_next = (c + 1) % VOICES;
	m_pitch[c] = int16_t((key + m_v[62] - 37) * 256);
	for (int oi = 0; oi < 4; oi++)
	{
		m_level[slot(c, oi)] = uint8_t(note_level(oi, key, vel));
		write(uint8_t(0x60 + slot(c, oi)), uint8_t(m_level[slot(c, oi)] >> 1));
	}
	if (m_v[58]) write(0x1B, uint8_t(0x10 | m_v[59]));   // LFO sync ($8525)
	write(uint8_t(0x20 + c), uint8_t(0x40 | m_r20));
}

// $8596
void Engine::note_off(int key)
{
	for (int c = 0; c < VOICES; c++)
	{
		if (m_key[c] != key) continue;
		m_key[c] = -1;
		int *q = std::find(m_queue, m_queue + m_qlen, c);
		if (q != m_queue + m_qlen) { std::memmove(q, q + 1, sizeof(int) * (m_queue + m_qlen - q - 1)); m_qlen--; }
		write(0x08, uint8_t(c));
		write(uint8_t(0x20 + c), m_r20);
		return;
	}
}

// periodic task (neutral modulation for now): TL refresh of one half of the channels, LFO depth,
// key code / fraction of all channels
void Engine::tick()
{
	const int base = m_half ? 0 : 4;
	for (int oi = 3; oi >= 0; oi--)
		for (int ch = base + 3; ch >= base; ch--)
		{
			int s = slot(ch, oi);
			int mod = m_t.mod_att[255];        // $A769 with no modulation
			write(uint8_t(0x60 + s), uint8_t(0x80 | (sat8(m_level[s] + mod) >> 1)));
		}
	const uint8_t depth_reg = m_half ? 0x17 : 0x19;
	write(depth_reg, 0x00);
	write(depth_reg, 0x80);
	m_half ^= 1;
	for (int ch = 7; ch >= 0; ch--)
	{
		int p = m_pitch[ch];
		int s = p >> 8;   // arithmetic shift: semitone, may be negative
		while (s < 0) s += 12;
		while (s >= 0x60) s -= 12;
		write(uint8_t(0x28 + ch), m_t.key_group[s]);
		write(uint8_t(0x30 + ch), uint8_t((p & 0xfc) | 1));
	}
}

void Engine::render(float *left, float *right, int n)
{
	ymfm::ym2414::output_data out;
	for (int i = 0; i < n; i++)
	{
		if (m_ready && ++m_tick_pos >= TICK) { m_tick_pos = 0; tick(); }
		m_opz.generate(&out);
		float l = float(out.data[0]) * (0.60f / 32768.0f);
		float r = float(out.data[1]) * (0.60f / 32768.0f);
		if (left) left[i] = std::clamp(l, -1.0f, 1.0f);
		if (right) right[i] = std::clamp(r, -1.0f, 1.0f);
	}
}

} // namespace txn
