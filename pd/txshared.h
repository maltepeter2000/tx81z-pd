// Shared by tx81z~ (firmware engine) and tx81zn~ (native engine): the voice parameter table and
// its pot mapping, and the random voice generator.
#pragma once

#include "m_pd.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <random>
#include <vector>

struct t_txparam
{
	const char *name;
	char group;          // 'V' VCED, 'A' ACED
	int num;             // parameter number in its group
	int lo, hi;          // TX81Z data range
	int offset;          // added for display (transpose -24, BC pitch bias -50)
	const char *label;
	const char *names;   // "a|b|c" for lo, lo+1, ... or "" for a number
};
#include "tx81z_params.h"

enum { G_V = 0, G_A = 1 };

static inline const t_txparam *find_param(t_symbol *s)
{
	for (int i = 0; i < NTXPARAMS; i++)
		if (s == gensym(TXPARAMS[i].name)) return &TXPARAMS[i];
	return 0;
}

static inline int gidx(const t_txparam *p) { return p->group == 'V' ? G_V : G_A; }

static inline int nvalues(const t_txparam *p) { return p->hi - p->lo + 1; }

// pot position -> data: up to 16 values in zones of equal width, more values linear
static inline int from127(const t_txparam *p, float c)
{
	c = c < 0 ? 0 : c > 127 ? 127 : c;
	int n = nvalues(p);
	if (n <= 16) { int k = int(std::floor(c * n / 128.0f)); return p->lo + (k > n - 1 ? n - 1 : k); }
	return p->lo + int(std::lround(c * (p->hi - p->lo) / 127.0f));
}

// data -> pot position (middle of the zone)
static inline int to127(const t_txparam *p, int v)
{
	int n = nvalues(p);
	float c = n <= 16 ? (v - p->lo + 0.5f) * 128.0f / n : (v - p->lo) * 127.0f / (p->hi - p->lo);
	int r = int(std::lround(c));
	return r < 0 ? 0 : r > 127 ? 127 : r;
}

static inline void value_text(const t_txparam *p, int v, char *buf, size_t nb)
{
	if (p->names[0])
	{
		int k = v - p->lo, n = 0;
		const char *a = p->names, *q;
		while (n < k && (q = std::strchr(a, '|'))) { a = q + 1; n++; }
		q = std::strchr(a, '|');
		int len = q ? int(q - a) : int(std::strlen(a));
		std::snprintf(buf, nb, "%.*s", len, a);
	}
	else if (p->offset) std::snprintf(buf, nb, "%+d", v + p->offset);
	else std::snprintf(buf, nb, "%d", v);
}


// ---- random voices -------------------------------------------------------------------
// random <pluck|bass|lead|brass|pad|strings|total> [seed]: a new voice into the edit buffer.
// Every category: algorithms, envelope and level ranges for carriers and modulators,
// frequency ratios (coarse: 0 = 0.5, 4 = 1, 8 = 2, 10 = 3, 13 = 4, 16 = 5, 19 = 6, 22 = 7,
// 25 = 8; checked against the firmware's display), LFO. Ranges are lo..hi of the data values.
struct t_rng_env { int ar[2], d1r[2], d1l[2], d2r[2], rr[2], out[2]; };
struct t_rng_cat {
	const char *name;
	std::vector<int> algs;
	t_rng_env car, mod;
	std::vector<int> car_crs, mod_crs, waves, dets;
	int fb[2], kvs_car[2], kvs_mod[2];
	int lfs[2], lfd[2], pmd[2], amd[2], pms[2], ams[2];
	int mono_pct;   // chance of mono + portamento
};

static const t_rng_cat RNG_CATS[] = {
	{ "pluck", {0, 1, 2, 3, 4},
	  {{31, 31}, {7, 12}, {4, 10}, {3, 7}, {6, 9}, {95, 99}},
	  {{31, 31}, {10, 18}, {0, 6}, {5, 10}, {6, 9}, {60, 82}},
	  {4, 4, 4, 8}, {4, 8, 10, 13, 16, 19, 5, 9, 11}, {0, 0, 0, 4, 1}, {3, 3, 2, 4},
	  {0, 6}, {1, 3}, {2, 5}, {30, 40}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, 0 },
	{ "bass", {0, 1, 2, 3, 4},
	  {{31, 31}, {6, 14}, {8, 13}, {2, 8}, {8, 11}, {97, 99}},
	  {{28, 31}, {8, 16}, {4, 10}, {3, 8}, {8, 11}, {65, 85}},
	  {0}, {0, 4, 4, 8}, {0, 0, 0, 4}, {3},
	  {3, 7}, {1, 3}, {1, 4}, {30, 40}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, 30 },
	{ "lead", {0, 1, 2, 3, 4},
	  {{29, 31}, {3, 8}, {13, 15}, {0, 1}, {7, 10}, {97, 99}},
	  {{26, 31}, {3, 10}, {9, 15}, {0, 3}, {7, 10}, {70, 90}},
	  {4}, {4, 8, 10, 13, 4}, {0, 0, 4, 1, 2}, {3, 3, 2, 4},
	  {4, 7}, {0, 2}, {1, 3}, {30, 40}, {30, 60}, {4, 12}, {0, 0}, {2, 4}, {0, 0}, 50 },
	{ "brass", {0, 1, 2, 3},
	  {{12, 15}, {4, 9}, {12, 14}, {0, 1}, {7, 9}, {97, 99}},
	  {{11, 14}, {4, 10}, {9, 13}, {0, 2}, {7, 9}, {72, 88}},
	  {4}, {4, 4, 8}, {0}, {3, 3, 2, 4},
	  {5, 7}, {1, 3}, {2, 4}, {30, 35}, {40, 70}, {3, 8}, {0, 0}, {2, 3}, {0, 0}, 0 },
	{ "pad", {4, 5, 6, 7},
	  {{6, 14}, {2, 6}, {13, 15}, {0, 1}, {3, 6}, {92, 99}},
	  {{6, 14}, {2, 8}, {8, 14}, {0, 2}, {3, 6}, {50, 75}},
	  {4, 4, 8, 0}, {4, 8, 13}, {0, 0, 4, 1}, {1, 2, 4, 5},
	  {0, 5}, {0, 2}, {0, 2}, {20, 30}, {0, 40}, {2, 6}, {0, 10}, {1, 3}, {0, 1}, 0 },
	{ "strings", {4, 6},
	  {{9, 15}, {3, 6}, {13, 15}, {0, 1}, {5, 7}, {93, 99}},
	  {{10, 16}, {3, 6}, {11, 15}, {0, 1}, {5, 7}, {68, 80}},
	  {4, 4, 8}, {4}, {0}, {2, 4, 1, 5},
	  {5, 7}, {0, 2}, {1, 2}, {30, 36}, {40, 70}, {4, 8}, {0, 0}, {2, 3}, {0, 0}, 0 },
};

// carriers of the 8 algorithms (operator numbers 1-4)
static inline bool is_carrier(int alg, int op)
{
	// from the chip and the firmware's level code: algorithm 5 has its carriers in the VCED blocks of
	// OP1 and OP2 (blocks 3 and 2), 6 and 7 in OP1-OP3, 8 in all
	static const unsigned char CAR[8] = { 0x1, 0x1, 0x1, 0x1, 0x3, 0x7, 0x7, 0xF };   // bit k-1 = op k
	return (CAR[alg & 7] >> (op - 1)) & 1;
}

static inline std::mt19937 &rng()
{
	static std::mt19937 g((unsigned)std::random_device{}() ^ (unsigned)time(nullptr));
	return g;
}

static inline int rr2(const int r[2]) { return std::uniform_int_distribution<int>(r[0], r[1])(rng()); }
static inline int rri(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng()); }
static inline int pick(const std::vector<int> &v) { return v[size_t(rri(0, int(v.size()) - 1))]; }

// random <kind> [seed]: kind pluck / bass / lead / brass / pad / strings / total; false if unknown
static inline bool random_voice(const char *kind, unsigned seed, uint8_t vc[93], uint8_t ac[23])
{
	const t_rng_cat *c = nullptr;
	bool total = std::strcmp(kind, "total") == 0 || std::strcmp(kind, "random") == 0;
	for (const auto &k : RNG_CATS) if (std::strcmp(kind, k.name) == 0) c = &k;
	if (!c && !total) return false;
	if (seed != 0) rng().seed(unsigned(seed));
	memset(ac, 0, 23);
	// common part as in the factory voices
	static const uint8_t COMMON[25] = { 0, 0, 35, 0, 0, 0, 0, 2, 0, 0, 12, 0, 2, 0, 0, 99, 1, 0, 0, 50, 0, 0, 0, 50, 0 };
	memcpy(vc + 52, COMMON, 25);
	static const uint8_t EXTRA[6] = { 99, 99, 99, 50, 50, 50 };
	memcpy(vc + 87, EXTRA, 6);
	int alg = total ? rri(0, 7) : pick(c->algs);
	vc[52] = uint8_t(alg);
	for (int op = 1; op <= 4; op++)
	{
		uint8_t *o = vc + (4 - op) * 13;
		uint8_t *a = ac + (4 - op) * 5;
		bool car = is_carrier(alg, op);
		if (total)
		{
			o[0] = uint8_t(car ? rri(8, 31) : rri(0, 31)); o[1] = uint8_t(rri(0, 31)); o[2] = uint8_t(rri(0, 31));
			o[3] = uint8_t(rri(3, 15)); o[4] = uint8_t(car ? rri(4, 15) : rri(0, 15));
			o[5] = uint8_t(rri(0, 40)); o[6] = uint8_t(rri(0, 3)); o[7] = 0; o[8] = 0;
			o[9] = uint8_t(rri(0, 7)); o[10] = uint8_t(car ? rri(85, 99) : rri(30, 99));
			o[11] = uint8_t(car ? rri(0, 31) : rri(0, 63)); o[12] = uint8_t(rri(0, 6));
			a[0] = uint8_t(rri(0, 9) == 0); a[1] = uint8_t(rri(0, 7)); a[2] = uint8_t(rri(0, 15));
			a[3] = uint8_t(rri(0, 7)); a[4] = 0;
			continue;
		}
		const t_rng_env &e = car ? c->car : c->mod;
		o[0] = uint8_t(rr2(e.ar)); o[1] = uint8_t(rr2(e.d1r)); o[2] = uint8_t(rr2(e.d2r));
		o[3] = uint8_t(rr2(e.rr)); o[4] = uint8_t(rr2(e.d1l));
		o[5] = uint8_t(car ? 0 : rri(0, 25));                 // level scaling: modulators darker up high
		o[6] = uint8_t(rri(0, 2));                            // rate scaling
		o[7] = 0; o[8] = 0;
		o[9] = uint8_t(car ? rr2(c->kvs_car) : rr2(c->kvs_mod));
		o[10] = uint8_t(rr2(e.out));
		o[11] = uint8_t(pick(car ? c->car_crs : c->mod_crs));
		o[12] = uint8_t(pick(c->dets));
		a[3] = uint8_t(pick(c->waves));
	}
	if (!total)
	{
		vc[53] = uint8_t(rr2(c->fb));
		vc[54] = uint8_t(rr2(c->lfs)); vc[55] = uint8_t(rr2(c->lfd)); vc[56] = uint8_t(rr2(c->pmd));
		vc[57] = uint8_t(rr2(c->amd)); vc[60] = uint8_t(rr2(c->pms)); vc[61] = uint8_t(rr2(c->ams));
		if (vc[57] > 0) for (int op = 1; op <= 4; op++) if (is_carrier(alg, op)) vc[(4 - op) * 13 + 8] = 1;
		if (rri(1, 100) <= c->mono_pct) { vc[63] = 1; vc[66] = uint8_t(rri(5, 25)); }
	}
	else
	{
		vc[53] = uint8_t(rri(0, 7)); vc[54] = uint8_t(rri(0, 99)); vc[55] = uint8_t(rri(0, 99));
		vc[56] = uint8_t(rri(0, 30)); vc[57] = uint8_t(rri(0, 30)); vc[59] = uint8_t(rri(0, 3));
		vc[60] = uint8_t(rri(0, 7)); vc[61] = uint8_t(rri(0, 3));
	}
	char name[16];
	snprintf(name, sizeof name, "%-7.7s%03d", total ? "random" : c->name, rri(0, 999));
	name[0] = char(toupper((unsigned char)name[0]));
	for (int i = 0; i < 10; i++) vc[77 + i] = uint8_t(name[i]);
	return true;
}
