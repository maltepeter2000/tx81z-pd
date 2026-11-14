// tx81z~ : Yamaha TX81Z emulator as a Pure Data external.
//
// Runs the original TX81Z firmware (user supplied) on the emulated HD6303X,
// with the YM2414 provided by ymfm. Output is resampled from the native
// ~55.93 kHz to Pd's sample rate (4-point Hermite).
//
// Inlet (messages):
//   rom <file>            load firmware (64 KB) and reset
//   nvram <file>          battery RAM file: loaded now, saved on "save" and when the object is freed
//   save                  write battery RAM to the nvram file
//   reset                 power-cycle
//   <float> / list        raw MIDI bytes (connect [midiin] directly)
//   midi <bytes...>       raw MIDI bytes
//   note <pitch> <vel> [ch]   note on/off (vel 0 = off), channel 1-16 (default 1)
//   cc <num> <val> [ch]   control change
//   pgm <n> [ch]          program change (0-127)
//   bend <-8192..8191> [ch]
//   press <button>        press and release a front-panel button
//   button <button> <0|1> hold / release a button
//   gain <f>              output gain (default 1)
//   pcswitch <0|1|2>      program change switch off / com / ind (must not be
//                         off, or program changes are ignored)
//   sysparam <p> <d>      any system setup parameter (SYS byte p, value d)
//   factoryinit           load the presets / reset the MIDI setup (test mode
//                         init, ~26 s of DSP time); runs automatically if the nvram file
//                         given as argument does not exist yet
//   buttons: store utility edit play param- param+ dec inc vol- vol+ cursor
//
// Voice parameters (controller layer, see tools/tx81z_params.py):
//   <name> <value>        set a voice parameter with its TX81Z data value, e.g. alg 4, out1 99
//   n <name> <0-127>      set it from a pot / CC position (range, zones in the external)
//   ninfo <name>          report one parameter now as info
//   vced <p> <d>, aced <p> <d>   raw parameter change (VCED 0-93, ACED 0-22)
//   dumpreq               ask the firmware for the edit buffer (ACED + VCED dump)
//   Parameter changes go to the firmware as SysEx (F0 43 10 12/13 pp dd F7). They are
//   coalesced: only the newest value of a parameter is sent, and only while the
//   emulated MIDI input is idle, so notes are never delayed by a turning knob.
//   The firmware sends ACED + VCED dumps when a voice is selected (PLAY SINGLE);
//   tx81z~ reads them from its MIDI Out, so it always knows the current values.
//
// Outlets: left / right audio, messages:
//   lcd <line1> <line2>   on every LCD change (symbols)
//   led <1-4 bitmask>
//   midiout <byte>        MIDI Out of the TX81Z
//   info <name> <0-127> <value text>   every parameter change (also its own)
//   voice <name>          voice name from a VCED dump

#include "m_pd.h"
#include "tx81z.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static t_class *tx81z_class;

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
static const int GROUP_SIZE[2] = { 94, 23 };

struct t_tx81z
{
	t_object x_obj;
	TX81Z *synth;
	t_outlet *out_msg;
	t_clock *clock;          // defers message output out of the DSP routine
	t_clock *release_clock;  // releases "press"ed buttons
	t_symbol *dir;           // patch directory for relative paths
	std::string *nvram_path;
	std::vector<uint8_t> *midi_out;
	std::string *lcd0, *lcd1;
	bool rom_loaded;
	float gain;
	uint8_t last_leds;
	int pressed;             // button currently "press"ed, -1 none
	t_clock *init_clock;     // factory init sequence
	int init_stage;          // 0 = idle
	int init_seq;            // step in the front panel sequence of the factory init
	double init_wait;        // Pd samples until the next init step (runs only while DSP is on)
	bool nvram_ok;           // battery RAM file was loaded

	// voice parameters
	int shadow[2][128];      // current data value per VCED / ACED number, -1 = unknown
	int pend[2][128];        // value waiting to be sent, -1 = none
	int pend_next;           // round robin position
	double sent_at[2][128];  // logical time a change went to the firmware (a dump just after it is older)
	int reported[NTXPARAMS]; // last value reported as info
	t_clock *rep_clock;      // reports changes (logical time of the change)
	t_clock *req_clock;      // dump request after boot
	std::vector<uint8_t> *sx; // SysEx from the firmware's MIDI Out
	bool in_sx;

	// resampler
	double phase, step;
	float hl[4], hr[4];
};

static std::string resolve(t_tx81z *x, t_symbol *s)
{
	std::string p = s->s_name;
	if (!p.empty() && p[0] != '/' && p[0] != '~' && !(p.size() > 1 && p[1] == ':'))
		p = std::string(x->dir->s_name) + "/" + p;
	return p;
}

// ---------------------------------------------------------------------------
// voice parameters
// ---------------------------------------------------------------------------

static const t_txparam *find_param(t_symbol *s)
{
	for (int i = 0; i < NTXPARAMS; i++)
		if (s == gensym(TXPARAMS[i].name)) return &TXPARAMS[i];
	return 0;
}

static int gidx(const t_txparam *p) { return p->group == 'V' ? G_V : G_A; }

static int nvalues(const t_txparam *p) { return p->hi - p->lo + 1; }

// pot position -> data: up to 16 values in zones of equal width, more values linear
static int from127(const t_txparam *p, float c)
{
	c = c < 0 ? 0 : c > 127 ? 127 : c;
	int n = nvalues(p);
	if (n <= 16) { int k = int(std::floor(c * n / 128.0f)); return p->lo + (k > n - 1 ? n - 1 : k); }
	return p->lo + int(std::lround(c * (p->hi - p->lo) / 127.0f));
}

// data -> pot position (middle of the zone)
static int to127(const t_txparam *p, int v)
{
	int n = nvalues(p);
	float c = n <= 16 ? (v - p->lo + 0.5f) * 128.0f / n : (v - p->lo) * 127.0f / (p->hi - p->lo);
	int r = int(std::lround(c));
	return r < 0 ? 0 : r > 127 ? 127 : r;
}

static void value_text(const t_txparam *p, int v, char *buf, size_t nb)
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

static void emit_info(t_tx81z *x, int i)
{
	const t_txparam *p = &TXPARAMS[i];
	int v = x->shadow[gidx(p)][p->num];
	if (v < 0) return;
	v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
	char buf[32];
	value_text(p, v, buf, sizeof buf);
	t_atom a[3];
	SETSYMBOL(&a[0], gensym(p->name));
	SETFLOAT(&a[1], to127(p, v));
	SETSYMBOL(&a[2], gensym(buf));
	outlet_anything(x->out_msg, gensym("info"), 3, a);
}

// every change is reported once as info (clock 0: in the logical time of the change)
static void rep_tick(t_tx81z *x)
{
	for (int i = 0; i < NTXPARAMS; i++)
	{
		const t_txparam *p = &TXPARAMS[i];
		int v = x->shadow[gidx(p)][p->num];
		if (v < 0 || v == x->reported[i]) continue;
		x->reported[i] = v;
		emit_info(x, i);
	}
}

static void set_param(t_tx81z *x, int g, int num, int v)
{
	if (num < 0 || num >= GROUP_SIZE[g]) return;
	v = v < 0 ? 0 : v > 127 ? 127 : v;
	x->shadow[g][num] = v;
	x->pend[g][num] = v;
	clock_delay(x->rep_clock, 0);
}

// one waiting parameter change into the emulated MIDI input, only when it is idle
static void send_pending(t_tx81z *x)
{
	if (x->init_stage || x->synth->midi_in_pending() > 0) return;
	const int total = GROUP_SIZE[G_V] + GROUP_SIZE[G_A];
	for (int k = 0; k < total; k++)
	{
		int i = (x->pend_next + k) % total;
		int g = i < GROUP_SIZE[G_V] ? G_V : G_A;
		int num = g == G_V ? i : i - GROUP_SIZE[G_V];
		if (x->pend[g][num] < 0) continue;
		x->synth->midi_in({ 0xF0, 0x43, 0x10, uint8_t(g == G_V ? 0x12 : 0x13), uint8_t(num), uint8_t(x->pend[g][num]), 0xF7 });
		x->pend[g][num] = -1;
		x->sent_at[g][num] = clock_getlogicaltime();
		x->pend_next = (i + 1) % total;
		return;
	}
}

static void dump_request(t_tx81z *x)
{
	static const uint8_t req[] = { 0xF0, 0x43, 0x20, 0x7E, 'L', 'M', ' ', ' ', '8', '9', '7', '6', 'A', 'E', 0xF7 };
	x->synth->midi_in(req, sizeof req);
}

// SysEx from the firmware's MIDI Out: ACED / VCED dumps and parameter changes
static bool recent(t_tx81z *x, int g, int num)
{
	return x->pend[g][num] >= 0 || (x->sent_at[g][num] > 0 && clock_gettimesince(x->sent_at[g][num]) < 500);
}

static void parse_sysex(t_tx81z *x)
{
	const std::vector<uint8_t> &m = *x->sx;
	size_t n = m.size();
	if (n < 7 || m[1] != 0x43) return;
	if ((m[2] & 0xF0) == 0x00 && m[3] == 0x03 && n >= 6 + 93 + 2)              // VCED dump
	{
		// changes waiting or just sent win over the dump: the firmware applies them after it
		for (int i = 0; i < 93; i++) if (!recent(x, G_V, i)) x->shadow[G_V][i] = m[6 + i];
		char nm[11];
		for (int i = 0; i < 10; i++) { char c = char(m[6 + 77 + i]); nm[i] = (c >= 32 && c < 127) ? c : ' '; }
		nm[10] = 0;
		int e = 10; while (e > 0 && nm[e - 1] == ' ') nm[--e] = 0;
		for (char *c = nm; *c; c++) if (*c == ' ') *c = '_';
		t_atom a; SETSYMBOL(&a, gensym(nm));
		outlet_anything(x->out_msg, gensym("voice"), 1, &a);
	}
	else if ((m[2] & 0xF0) == 0x00 && m[3] == 0x7E && n >= 16 + 23 + 2 &&
		std::memcmp(&m[6], "LM  8976AE", 10) == 0)                           // ACED dump
	{
		for (int i = 0; i < 23; i++) if (!recent(x, G_A, i)) x->shadow[G_A][i] = m[16 + i];
	}
	else if ((m[2] & 0xF0) == 0x10 && (m[3] == 0x12 || m[3] == 0x13) && n == 7)   // parameter change
	{
		int g = m[3] == 0x12 ? G_V : G_A;
		if (m[4] < GROUP_SIZE[g]) x->shadow[g][m[4]] = m[5];
	}
	else return;
	rep_tick(x);
}

static void midi_out_byte(t_tx81z *x, uint8_t b)
{
	if (b >= 0xF8) return;                       // realtime
	if (b == 0xF0) { x->sx->clear(); x->sx->push_back(b); x->in_sx = true; return; }
	if (!x->in_sx) return;
	if (b & 0x80 && b != 0xF7) { x->in_sx = false; return; }
	x->sx->push_back(b);
	if (b == 0xF7) { x->in_sx = false; parse_sysex(x); }
	else if (x->sx->size() > 512) x->in_sx = false;
}

static void tx81z_flush(t_tx81z *x)
{
	if (x->synth->lcd_changed())
	{
		std::string l0 = x->synth->lcd_line(0), l1 = x->synth->lcd_line(1);
		if (l0 != *x->lcd0 || l1 != *x->lcd1)   // only send real changes
		{
			*x->lcd0 = l0;
			*x->lcd1 = l1;
			t_atom a[2];
			SETSYMBOL(&a[0], gensym(l0.c_str()));
			SETSYMBOL(&a[1], gensym(l1.c_str()));
			outlet_anything(x->out_msg, gensym("lcd"), 2, a);
		}
	}
	uint8_t leds = x->synth->leds();
	if (leds != x->last_leds)
	{
		x->last_leds = leds;
		t_atom a;
		SETFLOAT(&a, leds);
		outlet_anything(x->out_msg, gensym("led"), 1, &a);
	}
	if (!x->midi_out->empty())
	{
		std::vector<uint8_t> bytes;
		bytes.swap(*x->midi_out);
		for (uint8_t b : bytes)
		{
			t_atom a;
			SETFLOAT(&a, b);
			outlet_anything(x->out_msg, gensym("midiout"), 1, &a);
			midi_out_byte(x, b);
		}
	}
}

static void tx81z_tick(t_tx81z *x) { tx81z_flush(x); }

static t_int *tx81z_perform(t_int *w)
{
	t_tx81z *x = (t_tx81z *)(w[1]);
	t_sample *outl = (t_sample *)(w[2]);
	t_sample *outr = (t_sample *)(w[3]);
	int n = (int)(w[4]);

	if (!x->rom_loaded)
	{
		std::memset(outl, 0, n * sizeof(t_sample));
		std::memset(outr, 0, n * sizeof(t_sample));
		return w + 5;
	}

	send_pending(x);
	if (x->init_stage && x->init_wait > 0)
	{
		x->init_wait -= n;
		if (x->init_wait <= 0) clock_delay(x->init_clock, 0);   // next step in message context
	}

	for (int i = 0; i < n; i++)
	{
		x->phase += x->step;
		while (x->phase >= 1.0)
		{
			x->phase -= 1.0;
			float l, r;
			x->synth->render(&l, &r, 1);
			x->hl[0] = x->hl[1]; x->hl[1] = x->hl[2]; x->hl[2] = x->hl[3]; x->hl[3] = l;
			x->hr[0] = x->hr[1]; x->hr[1] = x->hr[2]; x->hr[2] = x->hr[3]; x->hr[3] = r;
		}
		// 4-point Hermite between hl[1] and hl[2]
		float t = (float)x->phase;
		auto herm = [t](const float *y) {
			float c0 = y[1];
			float c1 = 0.5f * (y[2] - y[0]);
			float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
			float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
			return ((c3 * t + c2) * t + c1) * t + c0;
		};
		outl[i] = herm(x->hl) * x->gain;
		outr[i] = herm(x->hr) * x->gain;
	}

	if (x->synth->lcd_changed_peek() || x->synth->leds() != x->last_leds || !x->midi_out->empty())
		clock_delay(x->clock, 0);
	return w + 5;
}

static void tx81z_dsp(t_tx81z *x, t_signal **sp)
{
	x->step = double(x->synth->sample_rate()) / sp[0]->s_sr;
	dsp_add(tx81z_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, (t_int)sp[0]->s_n);
}

// ---------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------

static void tx81z_reset(t_tx81z *x)
{
	x->nvram_ok = false;
	if (!x->nvram_path->empty())
		x->nvram_ok = x->synth->load_nvram(*x->nvram_path);
	x->synth->reset();
	x->phase = 0;
	std::memset(x->hl, 0, sizeof(x->hl));
	std::memset(x->hr, 0, sizeof(x->hr));
	for (int g = 0; g < 2; g++)
		for (int i = 0; i < 128; i++) { x->shadow[g][i] = -1; x->pend[g][i] = -1; x->sent_at[g][i] = 0; }
	for (int i = 0; i < NTXPARAMS; i++) x->reported[i] = -1;
	clock_delay(x->req_clock, 7000);   // after the boot animation: ask for the edit buffer
}

static void tx81z_rom(t_tx81z *x, t_symbol *s)
{
	std::string err;
	if (!x->synth->load_rom(resolve(x, s), err))
	{
		pd_error(x, "tx81z~: %s", err.c_str());
		return;
	}
	x->rom_loaded = true;
	tx81z_reset(x);
	post("tx81z~: loaded %s", s->s_name);
}

static void tx81z_nvram(t_tx81z *x, t_symbol *s)
{
	*x->nvram_path = resolve(x, s);
	if (x->synth->load_nvram(*x->nvram_path))
		post("tx81z~: loaded battery RAM %s", s->s_name);
	else
		post("tx81z~: %s does not exist yet, will be created on save", s->s_name);
	if (x->rom_loaded) tx81z_reset(x);
}

static void tx81z_save(t_tx81z *x)
{
	if (x->nvram_path->empty()) { pd_error(x, "tx81z~: no nvram file set"); return; }
	if (x->synth->save_nvram(*x->nvram_path)) post("tx81z~: saved battery RAM");
	else pd_error(x, "tx81z~: could not write %s", x->nvram_path->c_str());
}

static void tx81z_bytes(t_tx81z *x, int argc, t_atom *argv)
{
	std::vector<uint8_t> b;
	for (int i = 0; i < argc; i++)
		if (argv[i].a_type == A_FLOAT)
			b.push_back(uint8_t(int(atom_getfloat(&argv[i])) & 0xff));
	if (!b.empty()) x->synth->midi_in(b.data(), b.size());
}

static void tx81z_float(t_tx81z *x, t_floatarg f)
{
	uint8_t b = uint8_t(int(f) & 0xff);
	x->synth->midi_in(&b, 1);
}

static void tx81z_list(t_tx81z *x, t_symbol *, int argc, t_atom *argv) { tx81z_bytes(x, argc, argv); }
static void tx81z_midi(t_tx81z *x, t_symbol *, int argc, t_atom *argv) { tx81z_bytes(x, argc, argv); }

static uint8_t chan(int argc, t_atom *argv, int idx)
{
	int c = argc > idx ? int(atom_getfloat(&argv[idx])) : 1;
	if (c < 1) c = 1;
	if (c > 16) c = 16;
	return uint8_t(c - 1);
}

static uint8_t b7(float f) { int v = int(f); return uint8_t(v < 0 ? 0 : v > 127 ? 127 : v); }

static void tx81z_note(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 2) return;
	uint8_t p = b7(atom_getfloat(&argv[0])), v = b7(atom_getfloat(&argv[1]));
	uint8_t c = chan(argc, argv, 2);
	if (v) x->synth->midi_in({ uint8_t(0x90 | c), p, v });
	else x->synth->midi_in({ uint8_t(0x80 | c), p, 0 });
}

static void tx81z_cc(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 2) return;
	x->synth->midi_in({ uint8_t(0xB0 | chan(argc, argv, 2)), b7(atom_getfloat(&argv[0])), b7(atom_getfloat(&argv[1])) });
}

static void tx81z_pgm(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 1) return;
	x->synth->midi_in({ uint8_t(0xC0 | chan(argc, argv, 1)), b7(atom_getfloat(&argv[0])) });
}

static void tx81z_bend(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 1) return;
	int v = int(atom_getfloat(&argv[0])) + 8192;
	v = v < 0 ? 0 : v > 16383 ? 16383 : v;
	x->synth->midi_in({ uint8_t(0xE0 | chan(argc, argv, 1)), uint8_t(v & 0x7f), uint8_t(v >> 7) });
}

static bool get_button(t_tx81z *x, t_symbol *s, TX81Z::Button &b)
{
	if (TX81Z::button_from_name(s->s_name, b)) return true;
	pd_error(x, "tx81z~: unknown button '%s' (store utility edit play param- param+ dec inc vol- vol+ cursor)", s->s_name);
	return false;
}

static void tx81z_button(t_tx81z *x, t_symbol *s, t_floatarg f)
{
	if (x->init_stage) return;   // factory init in progress
	TX81Z::Button b;
	if (get_button(x, s, b)) x->synth->set_button(b, f != 0);
}

static void tx81z_release(t_tx81z *x)
{
	if (x->pressed >= 0) x->synth->set_button(TX81Z::Button(x->pressed), false);
	x->pressed = -1;
}

static void tx81z_press(t_tx81z *x, t_symbol *s)
{
	if (x->init_stage) return;   // factory init in progress
	TX81Z::Button b;
	if (!get_button(x, s, b)) return;
	tx81z_release(x);
	x->synth->set_button(b, true);
	x->pressed = b;
	clock_delay(x->release_clock, 150);   // firmware scans the panel at a few ms; 150 ms is a safe tap
}

static void tx81z_gain(t_tx81z *x, t_floatarg f) { x->gain = f; }

// n <name> <0-127>: a parameter from a pot / CC position
static void tx81z_n(t_tx81z *x, t_symbol *name, t_floatarg c)
{
	if (name == gensym("none")) return;
	const t_txparam *p = find_param(name);
	if (!p) { pd_error(x, "tx81z~: n: unknown parameter '%s'", name->s_name); return; }
	set_param(x, gidx(p), p->num, from127(p, c));
}

static void tx81z_ninfo(t_tx81z *x, t_symbol *name)
{
	const t_txparam *p = find_param(name);
	if (p) emit_info(x, int(p - TXPARAMS));
}

static void tx81z_vced(t_tx81z *x, t_floatarg p, t_floatarg d) { set_param(x, G_V, int(p), int(d)); }
static void tx81z_aced(t_tx81z *x, t_floatarg p, t_floatarg d) { set_param(x, G_A, int(p), int(d)); }
static void tx81z_dumpreq(t_tx81z *x) { if (x->rom_loaded && !x->init_stage) dump_request(x); }

// <name> <value>: a voice parameter with its TX81Z data value
static void tx81z_anything(t_tx81z *x, t_symbol *s, int argc, t_atom *argv)
{
	const t_txparam *p = find_param(s);
	if (!p || argc < 1 || argv[0].a_type != A_FLOAT)
	{
		pd_error(x, "tx81z~: no method for '%s'", s->s_name);
		return;
	}
	int v = int(std::lround(atom_getfloat(&argv[0])));
	v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
	set_param(x, gidx(p), p->num, v);
}

// System parameter change (service manual p.15/26): F0 43 1n 10 7B pp dd F7
//   pp = SYS byte: 0 master tune, 1 basic receive ch (16 = omni), 2 transmit ch,
//   3 program change switch (0 off, 1 com, 2 ind), 4 control change switch,
//   5 pitch bend switch, 6 note switch, 7 SysEx on/off, 8 memory protect,
//   9 combine, 10 aftertouch -> breath
static void tx81z_sysparam(t_tx81z *x, t_floatarg p, t_floatarg d)
{
	if (x->init_stage) return;   // factory init in progress (it sets the switch itself)
	int pp = int(p), dd = int(d);
	if (pp < 0 || pp > 26 || dd < 0 || dd > 127) { pd_error(x, "tx81z~: sysparam <0-26> <0-127>"); return; }
	x->synth->midi_in({ 0xF0, 0x43, 0x10, 0x10, 0x7B, uint8_t(pp), uint8_t(dd), 0xF7 });
}

// program change switch: 0 off, 1 com, 2 ind
static void tx81z_pcswitch(t_tx81z *x, t_floatarg f)
{
	int v = int(f);
	if (v < 0 || v > 2) { pd_error(x, "tx81z~: pcswitch 0 (off) / 1 (com) / 2 (ind)"); return; }
	tx81z_sysparam(x, 3, v);
}

// Factory init (service manual p.28): hold MASTER VOLUME + CURSOR at power-on
// -> "Test Entry ?", INC loads the presets and sets the MIDI parameters,
// then power-cycle and select PLAY mode.
// the init steps are timed in emulated time: the CPU only runs while DSP is on
static void init_after(t_tx81z *x, double ms)
{
	x->init_wait = ms * 0.001 * (sys_getsr() > 0 ? sys_getsr() : 44100);
}

static void tx81z_init_tick(t_tx81z *x)
{
	switch (x->init_stage)
	{
	case 1:  // release the held buttons
		x->synth->set_button(TX81Z::BTN_VOL_LEFT, false);
		x->synth->set_button(TX81Z::BTN_CURSOR, false);
		x->init_stage = 2;
		init_after(x, 2000);
		break;
	case 2:  // INC: load presets
		x->synth->set_button(TX81Z::BTN_INC, true);
		x->init_stage = 3;
		init_after(x, 200);
		break;
	case 3:
		x->synth->set_button(TX81Z::BTN_INC, false);
		x->init_stage = 4;
		init_after(x, 2000);
		break;
	case 4:  // power-cycle, battery RAM is kept
		x->synth->reset();
		x->init_stage = 5;
		init_after(x, 6000);
		break;
	case 5:  // after the boot animation the firmware is in PLAY SINGLE
		x->init_seq = 0;
		x->init_stage = 6;
		init_after(x, 100);
		break;
	case 6:  // MIDI setup by the front panel (SysEx is ignored until Exclusive is on)
	{
		static const TX81Z::Button seq[] = {
			TX81Z::BTN_UTILITY, TX81Z::BTN_PARAM_RIGHT, TX81Z::BTN_INC,     // UTILITY > MIDI Control
			TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC,   // Basic Rcv.Ch 1 -> omni
			TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC,
			TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC,
			TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC, TX81Z::BTN_INC,
			TX81Z::BTN_PARAM_RIGHT,                                          // Transmit Ch (1)
			TX81Z::BTN_PARAM_RIGHT, TX81Z::BTN_INC,                          // P.Change off -> com
			TX81Z::BTN_PARAM_RIGHT, TX81Z::BTN_INC,                          // Cont.Change off -> norm
			TX81Z::BTN_PARAM_RIGHT,                                          // A.Touch > BC (off)
			TX81Z::BTN_PARAM_RIGHT, TX81Z::BTN_INC,                          // Pitch Bend off -> norm
			TX81Z::BTN_PARAM_RIGHT,                                          // Note on/off (all)
			TX81Z::BTN_PARAM_RIGHT, TX81Z::BTN_INC,                          // Exclusive off -> on
			TX81Z::BTN_PLAY,                                                 // back to PLAY SINGLE
		};
		const int n = int(sizeof seq / sizeof seq[0]);
		int k = x->init_seq / 2;
		bool down = (x->init_seq % 2) == 0;
		x->synth->set_button(seq[k], down);
		x->init_seq++;
		if (x->init_seq >= 2 * n) { x->init_stage = 7; init_after(x, 500); }
		else init_after(x, down ? 150 : 250);
		break;
	}
	case 7:
		x->init_stage = 0;
		dump_request(x);
		if (!x->nvram_path->empty()) x->synth->save_nvram(*x->nvram_path);
		post("tx81z~: factory init done (presets, MIDI omni, program change, control change, pitch bend and SysEx on, battery RAM saved)");
		break;
	default:
		x->init_stage = 0;
		break;
	}
}

static void tx81z_factoryinit(t_tx81z *x)
{
	if (!x->rom_loaded) { pd_error(x, "tx81z~: load a ROM first"); return; }
	post("tx81z~: factory init (about 26 s, needs DSP on) ...");
	tx81z_release(x);
	for (int b = 0; b < TX81Z::BTN_COUNT; b++) x->synth->set_button(TX81Z::Button(b), false);
	x->synth->set_button(TX81Z::BTN_VOL_LEFT, true);
	x->synth->set_button(TX81Z::BTN_CURSOR, true);
	x->synth->reset();
	x->init_stage = 1;
	init_after(x, 3000);
	if (!canvas_dspstate) post("tx81z~: switch DSP on, the factory init runs only while DSP is on");
}

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

static void *tx81z_new(t_symbol *, int argc, t_atom *argv)
{
	t_tx81z *x = (t_tx81z *)pd_new(tx81z_class);
	x->synth = new TX81Z();
	x->nvram_path = new std::string();
	x->midi_out = new std::vector<uint8_t>();
	x->lcd0 = new std::string();
	x->lcd1 = new std::string();
	x->dir = canvas_getdir(canvas_getcurrent());
	x->rom_loaded = false;
	x->gain = 1.0f;
	x->last_leds = 0xff;
	x->pressed = -1;
	x->phase = 0;
	x->step = 1;
	std::memset(x->hl, 0, sizeof(x->hl));
	std::memset(x->hr, 0, sizeof(x->hr));

	x->synth->on_midi_out = [x](uint8_t b) { x->midi_out->push_back(b); };

	outlet_new(&x->x_obj, &s_signal);
	outlet_new(&x->x_obj, &s_signal);
	x->out_msg = outlet_new(&x->x_obj, 0);
	x->clock = clock_new(x, (t_method)tx81z_tick);
	x->release_clock = clock_new(x, (t_method)tx81z_release);
	x->init_clock = clock_new(x, (t_method)tx81z_init_tick);
	x->init_stage = 0;
	x->init_wait = 0;
	x->nvram_ok = false;
	x->sx = new std::vector<uint8_t>();
	x->in_sx = false;
	x->pend_next = 0;
	for (int g = 0; g < 2; g++)
		for (int i = 0; i < 128; i++) { x->shadow[g][i] = -1; x->pend[g][i] = -1; x->sent_at[g][i] = 0; }
	for (int i = 0; i < NTXPARAMS; i++) x->reported[i] = -1;
	x->rep_clock = clock_new(x, (t_method)rep_tick);
	x->req_clock = clock_new(x, (t_method)tx81z_dumpreq);

	// [tx81z~ <rom> <nvram>]
	if (argc >= 2 && argv[1].a_type == A_SYMBOL)
		*x->nvram_path = resolve(x, atom_getsymbol(&argv[1]));
	if (argc >= 1 && argv[0].a_type == A_SYMBOL)
		tx81z_rom(x, atom_getsymbol(&argv[0]));
	// no battery RAM file yet -> initialize like a new TX81Z
	if (x->rom_loaded && !x->nvram_path->empty() && !x->nvram_ok)
	{
		post("tx81z~: no battery RAM file, running factory init");
		tx81z_factoryinit(x);
	}
	return x;
}

static void tx81z_free(t_tx81z *x)
{
	if (x->rom_loaded && !x->nvram_path->empty())
		x->synth->save_nvram(*x->nvram_path);
	clock_free(x->clock);
	clock_free(x->release_clock);
	clock_free(x->init_clock);
	clock_free(x->rep_clock);
	clock_free(x->req_clock);
	delete x->sx;
	delete x->synth;
	delete x->nvram_path;
	delete x->midi_out;
	delete x->lcd0;
	delete x->lcd1;
}

extern "C" {
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void tx81z_tilde_setup(void)
{
	tx81z_class = class_new(gensym("tx81z~"), (t_newmethod)tx81z_new, (t_method)tx81z_free,
		sizeof(t_tx81z), CLASS_DEFAULT, A_GIMME, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_dsp, gensym("dsp"), A_CANT, A_NULL);
	class_addfloat(tx81z_class, (t_method)tx81z_float);
	class_addlist(tx81z_class, (t_method)tx81z_list);
	class_addmethod(tx81z_class, (t_method)tx81z_rom, gensym("rom"), A_SYMBOL, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_nvram, gensym("nvram"), A_SYMBOL, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_save, gensym("save"), A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_reset, gensym("reset"), A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_midi, gensym("midi"), A_GIMME, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_note, gensym("note"), A_GIMME, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_cc, gensym("cc"), A_GIMME, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_pgm, gensym("pgm"), A_GIMME, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_bend, gensym("bend"), A_GIMME, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_press, gensym("press"), A_SYMBOL, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_button, gensym("button"), A_SYMBOL, A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_gain, gensym("gain"), A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_factoryinit, gensym("factoryinit"), A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_sysparam, gensym("sysparam"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_pcswitch, gensym("pcswitch"), A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_n, gensym("n"), A_SYMBOL, A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_ninfo, gensym("ninfo"), A_SYMBOL, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_vced, gensym("vced"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_aced, gensym("aced"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_dumpreq, gensym("dumpreq"), A_NULL);
	class_addanything(tx81z_class, (t_method)tx81z_anything);
}
}
