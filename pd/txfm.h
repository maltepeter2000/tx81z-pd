// txfm.h: a simplified FM view of a TX81Z voice, in the style of 4-operator groovebox synths.
//
// The algorithm gives the operators roles: C = carriers, A = the modulator of the main carrier,
// B = the other modulators. One control per role changes every operator of that role (ratio,
// waveform, level, envelope ...); MIX balances the carriers, DETUNE spreads the operators.
// Everything is an ordinary TX81Z voice parameter in the end: a macro reads its value from the
// first operator of the role and writes all of them.
//
//   fm_count(), fm_macro(i)                  the macros (name, page, label)
//   fm_get(i, get, &v127, text, n)           value 0-127 and its text, false = role is empty
//   fm_set(i, v127, get, set)                write the parameters (set(group, num, value))
// get(group, num) / set(group, num, value): VCED 'V' 0-92 / ACED 'A' 0-22 as in the SysEx dumps.
//
// Operator blocks: VCED / ACED keep the operators in the order of the chip's slots, which is
// OP4, OP2, OP3, OP1 in the TX81Z manual's naming (block 0 = OP4, 1 = OP2, 2 = OP3, 3 = OP1):
// the firmware writes block b to slot 8 * b + channel (M1, M2, C1, C2 of the YM2151 family) and
// algorithm 1 of the manual, 4 -> 3 -> 2 -> 1, is the chip's M1 -> C1 -> M2 -> C2.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace txfm {

// ---- roles ---------------------------------------------------------------------------------
enum { ROLE_C, ROLE_A, ROLE_B, NROLES };
enum { OP1 = 3, OP2 = 1, OP3 = 2, OP4 = 0 };   // VCED block of each manual operator

struct t_roles { int n[NROLES]; int op[NROLES][4]; };

// manual algorithms: 1 4>3>2>1, 2 (4+3)>2>1, 3 (4+(3>2))>1, 4 ((4>3)+2)>1, 5 (4>3)+(2>1),
// 6 4>(3,2,1), 7 (4>3)+2+1, 8 4+3+2+1
static inline t_roles roles(int alg)
{
	static const t_roles R[8] = {
		{ { 1, 1, 2 }, { { OP1 }, { OP2 }, { OP3, OP4 } } },
		{ { 1, 1, 2 }, { { OP1 }, { OP2 }, { OP3, OP4 } } },
		{ { 1, 1, 2 }, { { OP1 }, { OP2 }, { OP3, OP4 } } },
		{ { 1, 1, 2 }, { { OP1 }, { OP2 }, { OP3, OP4 } } },
		{ { 2, 1, 1 }, { { OP1, OP3 }, { OP2 }, { OP4 } } },
		{ { 3, 1, 0 }, { { OP1, OP2, OP3 }, { OP4 }, {} } },
		{ { 3, 1, 0 }, { { OP1, OP2, OP3 }, { OP4 }, {} } },
		{ { 4, 0, 0 }, { { OP1, OP2, OP3, OP4 }, {}, {} } },
	};
	return R[alg & 7];
}

// ---- frequency ratios ------------------------------------------------------------------------
// coarse 0-63 -> chip multiplier (0 = 0.5) and DT2 (x1, x2^(600/1200), x2^(781/1200), x2^(950/1200));
// fine adds 1/16 of the multiplier unit (only 0-7 with multiplier 0.5). Measured on the native engine.
static inline double ratio(int crs, int fine)
{
	static const unsigned char MD[64] = {
		0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x30, 0x22, 0x23, 0x40, 0x31, 0x32,
		0x50, 0x33, 0x41, 0x60, 0x42, 0x43, 0x70, 0x51, 0x52, 0x80, 0x61, 0x53, 0x90, 0x62, 0x71, 0xA0,
		0x63, 0x72, 0xB0, 0x81, 0xC0, 0x73, 0x82, 0x91, 0xD0, 0x83, 0xE0, 0xA1, 0x92, 0xF0, 0xB1, 0x93,
		0xA2, 0xC1, 0xB2, 0xA3, 0xD1, 0xC2, 0xB3, 0xE1, 0xD2, 0xC3, 0xF1, 0xE2, 0xD3, 0xF2, 0xE3, 0xF3 };
	static const double D2[4] = { 1.0, 1.41421356, 1.56995006, 1.73107828 };
	int m = MD[crs & 63] >> 4, d = MD[crs & 63] & 3;
	if (m == 0 && fine > 7) fine = 7;
	return D2[d] * ((m ? m : 0.5) + fine / 16.0);
}

// ---- macros --------------------------------------------------------------------------------
enum Kind { PARAM, ROLE, MIX, DETUNE };

struct t_macro
{
	const char *name;    // message name
	const char *label;   // short label for the editor
	int page;            // 0-6, 8 controls per page
	Kind kind;
	char group;          // PARAM: 'V' / 'A' and number; ROLE: 'V' / 'A' and the field of the block
	int num;
	int role;            // ROLE
	int lo, hi;
	const char *names;   // "a|b|c" or "" (number + offset)
	int offset;
};

static const char *const PAGES[7] = { "SYN 1", "SYN 2", "ENV C", "ENV A", "ENV B", "LFO", "VOICE" };

#define TXFM_ENV(pg, r, R) \
	{ "atk" R, "attack", pg, ROLE, 'V', 0, r, 0, 31, "", 0 }, \
	{ "dec" R, "decay", pg, ROLE, 'V', 1, r, 0, 31, "", 0 }, \
	{ "sus" R, "sustain", pg, ROLE, 'V', 4, r, 0, 15, "", 0 }, \
	{ "d2r" R, "fade", pg, ROLE, 'V', 2, r, 0, 31, "", 0 }, \
	{ "rel" R, "release", pg, ROLE, 'V', 3, r, 0, 15, "", 0 }, \
	{ "vel" R, "velocity", pg, ROLE, 'V', 9, r, 0, 7, "", 0 }, \
	{ "lsc" R, "key level", pg, ROLE, 'V', 5, r, 0, 99, "", 0 }, \
	{ "rsc" R, "key rate", pg, ROLE, 'V', 6, r, 0, 3, "", 0 }

#define TXFM_WAVES "W1|W2|W3|W4|W5|W6|W7|W8"

static const t_macro MACROS[] = {
	// SYN 1
	{ "algo", "algorithm", 0, PARAM, 'V', 52, 0, 0, 7, "", 1 },
	{ "ratc", "ratio C", 0, ROLE, 'V', 11, ROLE_C, 0, 63, "", 0 },
	{ "rata", "ratio A", 0, ROLE, 'V', 11, ROLE_A, 0, 63, "", 0 },
	{ "ratb", "ratio B", 0, ROLE, 'V', 11, ROLE_B, 0, 63, "", 0 },
	{ "moda", "mod A", 0, ROLE, 'V', 10, ROLE_A, 0, 99, "", 0 },
	{ "modb", "mod B", 0, ROLE, 'V', 10, ROLE_B, 0, 99, "", 0 },
	{ "fdbk", "feedback", 0, PARAM, 'V', 53, 0, 0, 7, "", 0 },
	{ "mix", "mix", 0, MIX, 0, 0, ROLE_C, -99, 99, "", 0 },
	// SYN 2
	{ "wavc", "wave C", 1, ROLE, 'A', 3, ROLE_C, 0, 7, TXFM_WAVES, 0 },
	{ "wava", "wave A", 1, ROLE, 'A', 3, ROLE_A, 0, 7, TXFM_WAVES, 0 },
	{ "wavb", "wave B", 1, ROLE, 'A', 3, ROLE_B, 0, 7, TXFM_WAVES, 0 },
	{ "dtun", "detune", 1, DETUNE, 0, 0, 0, 0, 3, "", 0 },
	{ "finc", "fine C", 1, ROLE, 'A', 2, ROLE_C, 0, 15, "", 0 },
	{ "fina", "fine A", 1, ROLE, 'A', 2, ROLE_A, 0, 15, "", 0 },
	{ "finb", "fine B", 1, ROLE, 'A', 2, ROLE_B, 0, 15, "", 0 },
	{ "lvlc", "level C", 1, ROLE, 'V', 10, ROLE_C, 0, 99, "", 0 },
	// envelopes per role (C = amplitude)
	TXFM_ENV(2, ROLE_C, "c"),
	TXFM_ENV(3, ROLE_A, "a"),
	TXFM_ENV(4, ROLE_B, "b"),
	// LFO
	{ "lspd", "speed", 5, PARAM, 'V', 54, 0, 0, 99, "", 0 },
	{ "lwav", "wave", 5, PARAM, 'V', 59, 0, 0, 3, "saw|square|triangle|s+h", 0 },
	{ "ldly", "delay", 5, PARAM, 'V', 55, 0, 0, 99, "", 0 },
	{ "lpmd", "pitch depth", 5, PARAM, 'V', 56, 0, 0, 99, "", 0 },
	{ "lamd", "amp depth", 5, PARAM, 'V', 57, 0, 0, 99, "", 0 },
	{ "lpms", "pitch sens", 5, PARAM, 'V', 60, 0, 0, 7, "", 0 },
	{ "lams", "amp sens", 5, PARAM, 'V', 61, 0, 0, 3, "", 0 },
	{ "lsyn", "key sync", 5, PARAM, 'V', 58, 0, 0, 1, "off|on", 0 },
	// VOICE
	{ "poly", "poly/mono", 6, PARAM, 'V', 63, 0, 0, 1, "poly|mono", 0 },
	{ "pbr", "bend range", 6, PARAM, 'V', 64, 0, 0, 12, "", 0 },
	{ "pmod", "porta mode", 6, PARAM, 'V', 65, 0, 0, 1, "full|fingered", 0 },
	{ "port", "porta time", 6, PARAM, 'V', 66, 0, 0, 99, "", 0 },
	{ "trps", "transpose", 6, PARAM, 'V', 62, 0, 0, 48, "", -24 },
	{ "amec", "LFO amp C", 6, ROLE, 'V', 8, ROLE_C, 0, 1, "off|on", 0 },
	{ "amea", "LFO amp A", 6, ROLE, 'V', 8, ROLE_A, 0, 1, "off|on", 0 },
	{ "ameb", "LFO amp B", 6, ROLE, 'V', 8, ROLE_B, 0, 1, "off|on", 0 },
};

#undef TXFM_ENV
#undef TXFM_WAVES

static inline int fm_count() { return int(sizeof MACROS / sizeof MACROS[0]); }

static inline int fm_find(const char *name)
{
	for (int i = 0; i < fm_count(); i++)
		if (!std::strcmp(MACROS[i].name, name)) return i;
	return -1;
}

static inline int num_of(const t_macro &m, int block)
{
	return m.group == 'V' ? block * 13 + m.num : block * 5 + m.num;
}

static inline int scale_from(const t_macro &m, int v127)
{
	v127 = v127 < 0 ? 0 : v127 > 127 ? 127 : v127;
	return m.lo + int(std::lround(v127 * double(m.hi - m.lo) / 127.0));
}

static inline int scale_to(const t_macro &m, int v)
{
	int r = int(std::lround((v - m.lo) * 127.0 / double(m.hi - m.lo)));
	return r < 0 ? 0 : r > 127 ? 127 : r;
}

static inline void name_text(const char *names, int k, char *buf, size_t nb)
{
	const char *a = names, *q;
	for (int n = 0; n < k && (q = std::strchr(a, '|')); n++) a = q + 1;
	q = std::strchr(a, '|');
	std::snprintf(buf, nb, "%.*s", q ? int(q - a) : int(std::strlen(a)), a);
}

// carriers: the main one (first) against the others
template <class Get> static inline void mix_levels(const t_roles &r, Get get, int &main, int &others)
{
	main = get('V', r.op[ROLE_C][0] * 13 + 10);
	others = 0;
	for (int k = 1; k < r.n[ROLE_C]; k++) others += get('V', r.op[ROLE_C][k] * 13 + 10);
	if (r.n[ROLE_C] > 1) others = int(std::lround(others / double(r.n[ROLE_C] - 1)));
}

// all operators, carriers first: detune 3 (centre) +d, -d, +d ...
static inline int detune_order(const t_roles &r, int *ops)
{
	int n = 0;
	for (int ro = 0; ro < NROLES; ro++)
		for (int k = 0; k < r.n[ro]; k++) ops[n++] = r.op[ro][k];
	return n;
}

// value of macro i: v127 0-127 and the text; false if the macro has nothing to act on (empty role)
template <class Get> static inline bool fm_get(int i, Get get, int &v127, char *text, size_t nb)
{
	const t_macro &m = MACROS[i];
	t_roles r = roles(get('V', 52));
	int v = 0;
	switch (m.kind)
	{
	case PARAM:
		v = get(m.group, m.num);
		break;
	case ROLE:
	{
		if (!r.n[m.role]) { v127 = 0; std::snprintf(text, nb, "-"); return false; }
		int b = r.op[m.role][0];
		v = get(m.group, num_of(m, b));
		if (m.num == 11 && m.group == 'V')   // ratio (or fixed frequency)
		{
			v127 = scale_to(m, v);
			if (get('A', b * 5 + 0)) std::snprintf(text, nb, "fixed");
			else std::snprintf(text, nb, "%.2f", ratio(v, get('A', b * 5 + 2)));
			return true;
		}
		break;
	}
	case MIX:
	{
		if (r.n[ROLE_C] < 2) { v127 = 64; std::snprintf(text, nb, "-"); return false; }
		int main, others;
		mix_levels(r, get, main, others);
		v = others - main;
		v127 = v == 0 ? 64 : scale_to(m, v);
		std::snprintf(text, nb, v == 0 ? "even" : "%+d", v);
		return true;
	}
	case DETUNE:
	{
		int ops[4], n = detune_order(r, ops);
		v = n > 1 ? std::abs(get('V', ops[1] * 13 + 12) - 3) : 0;
		break;
	}
	}
	v = v < m.lo ? m.lo : v > m.hi ? m.hi : v;
	v127 = scale_to(m, v);
	if (m.names[0]) name_text(m.names, v - m.lo, text, nb);
	else std::snprintf(text, nb, m.offset ? "%+d" : "%d", v + m.offset);
	if (m.offset > 0) std::snprintf(text, nb, "%d", v + m.offset);   // algorithm 1-8
	return true;
}

// write macro i (value 0-127) through set(group, num, value)
template <class Get, class Set> static inline void fm_set(int i, int v127, Get get, Set set)
{
	const t_macro &m = MACROS[i];
	t_roles r = roles(get('V', 52));
	int v = scale_from(m, v127);
	switch (m.kind)
	{
	case PARAM:
		set(m.group, m.num, v);
		break;
	case ROLE:
		for (int k = 0; k < r.n[m.role]; k++) set(m.group, num_of(m, r.op[m.role][k]), v);
		break;
	case MIX:
	{
		// the louder side keeps its level, the other one goes down by |v| steps
		if (r.n[ROLE_C] < 2) break;
		int main, others;
		mix_levels(r, get, main, others);
		int top = main > others ? main : others;
		if (v127 == 64) v = 0;
		int lm = v > 0 ? top - v : top, lo = v < 0 ? top + v : top;
		set('V', r.op[ROLE_C][0] * 13 + 10, lm < 0 ? 0 : lm);
		for (int k = 1; k < r.n[ROLE_C]; k++) set('V', r.op[ROLE_C][k] * 13 + 10, lo < 0 ? 0 : lo);
		break;
	}
	case DETUNE:
	{
		int ops[4], n = detune_order(r, ops);
		for (int k = 0; k < n; k++) set('V', ops[k] * 13 + 12, k == 0 ? 3 : (k & 1) ? 3 + v : 3 - v);
		break;
	}
	}
}

} // namespace txfm
