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
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <ctime>
#include <cctype>
#include <cstring>

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
	double send_wait;        // Pd samples until the firmware takes the next change (it needs ~35 ms each)
	bool voice_waiting;      // a whole voice waits for its turn
	uint8_t voice_v[93], voice_a[23];
	int rx_reported;         // receive channel last reported (channel outlet)
	t_clock *stat_clock;     // status messages go out a moment later (also from the constructor)
	std::string *stat_text;
	uint8_t held[16][128];   // keys down (from the MIDI going in)
	int nheld;
	bool req_wanted;         // a dump request waits until no key is down
	uint8_t trk_status, trk_d1, trk_n;   // running status of the MIDI going in
	t_clock *play_clock;     // PLAY press before a program change / after a system change
	int play_stage;
	std::vector<uint8_t> *after_play; // MIDI bytes sent once PLAY is released
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

// the same file with another upper / lower case (TX81Z-v1.6.bin for tx81z-v1.6.bin), else p
static std::string find_nocase(const std::string &p)
{
#ifndef _WIN32
	FILE *f = fopen(p.c_str(), "rb");
	if (f) { fclose(f); return p; }
	size_t sl = p.rfind('/');
	std::string dir = sl == std::string::npos ? "." : p.substr(0, sl);
	std::string name = sl == std::string::npos ? p : p.substr(sl + 1);
	DIR *d = opendir(dir.c_str());
	if (!d) return p;
	std::string found = p;
	while (struct dirent *e = readdir(d))
		if (strcasecmp(e->d_name, name.c_str()) == 0) { found = dir + "/" + e->d_name; break; }
	closedir(d);
	return found;
#else
	return p;
#endif
}

// status <text> on outlet 2 (for a display); sent from a clock, so a status from the
// constructor (no firmware, factory init) arrives once the patch is connected
static void stat_tick(t_tx81z *x)
{
	t_atom a;
	SETSYMBOL(&a, gensym(x->stat_text->c_str()));
	outlet_anything(x->out_msg, gensym("status"), 1, &a);
}

static void status(t_tx81z *x, const char *text)
{
	*x->stat_text = text;
	clock_delay(x->stat_clock, 0);
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

// system setup in battery RAM (found by comparing RAM dumps): 0x1565 master tune,
// 0x1566 receive channel (0-15, 16 = omni), 0x1567 transmit channel, 0x1568 program change
// switch, 0x1569 control change, 0x156A pitch bend, 0x156B note on/off, 0x156C Exclusive
enum { SYS_RAM = 0x1565 };

// the firmware only takes SysEx on its receive channel (any channel with omni)
static uint8_t rxch(t_tx81z *x)
{
	int c = x->synth->ram(SYS_RAM + 1);
	return uint8_t(c >= 16 ? 0 : c);
}

// one waiting parameter change into the emulated MIDI input, only when it is idle
// The firmware redraws its display after every parameter change and takes about 35 ms for it
// (measured with v1.6: one change every 30 ms or faster ends in "Midi Buffer Full", as on the
// real TX81Z). So one change per 45 ms of emulated time, and 200 ms after a whole voice.
static const double PARAM_GAP_MS = 45, VOICE_GAP_MS = 200;

static double ms_samples(double ms) { return ms * 0.001 * (sys_getsr() > 0 ? sys_getsr() : 44100); }

static void send_voice_now(t_tx81z *x, const uint8_t *vc, const uint8_t *ac);

static void send_pending(t_tx81z *x)
{
	if (x->init_stage || x->send_wait > 0 || x->synth->midi_in_pending() > 0) return;
	if (x->voice_waiting)
	{
		x->voice_waiting = false;
		send_voice_now(x, x->voice_v, x->voice_a);
		return;
	}
	const int total = GROUP_SIZE[G_V] + GROUP_SIZE[G_A];
	for (int k = 0; k < total; k++)
	{
		int i = (x->pend_next + k) % total;
		int g = i < GROUP_SIZE[G_V] ? G_V : G_A;
		int num = g == G_V ? i : i - GROUP_SIZE[G_V];
		if (x->pend[g][num] < 0) continue;
		x->synth->midi_in({ 0xF0, 0x43, uint8_t(0x10 | rxch(x)), uint8_t(g == G_V ? 0x12 : 0x13), uint8_t(num), uint8_t(x->pend[g][num]), 0xF7 });
		x->pend[g][num] = -1;
		x->sent_at[g][num] = clock_getlogicaltime();
		x->pend_next = (i + 1) % total;
		x->send_wait = ms_samples(PARAM_GAP_MS);
		return;
	}
}

static void dump_request(t_tx81z *x)
{
	const uint8_t req[] = { 0xF0, 0x43, uint8_t(0x20 | rxch(x)), 0x7E, 'L', 'M', ' ', ' ', '8', '9', '7', '6', 'A', 'E', 0xF7 };
	x->synth->midi_in(req, sizeof req);
	if (x->send_wait < ms_samples(100)) x->send_wait = ms_samples(100);   // the firmware sends its dump
}

// SysEx from the firmware's MIDI Out: ACED / VCED dumps and parameter changes
// voice <name> on outlet 2 (spaces as _)
static void emit_voice_name(t_tx81z *x, const uint8_t *n10)
{
	char nm[11];
	for (int i = 0; i < 10; i++) { char c = char(n10[i]); nm[i] = (c >= 32 && c < 127) ? c : ' '; }
	nm[10] = 0;
	int e = 10; while (e > 0 && nm[e - 1] == ' ') nm[--e] = 0;
	for (char *c = nm; *c; c++) if (*c == ' ') *c = '_';
	t_atom a; SETSYMBOL(&a, gensym(nm));
	outlet_anything(x->out_msg, gensym("voice"), 1, &a);
}

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
		emit_voice_name(x, m.data() + 6 + 77);
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

static void report_channel(t_tx81z *x);

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
	if (!x->init_stage) report_channel(x);
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

	if (x->send_wait > 0) x->send_wait -= n;
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
	memset(x->held, 0, sizeof x->held);
	x->nheld = 0;
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
	if (!x->synth->load_rom(find_nocase(resolve(x, s)), err))
	{
		pd_error(x, "tx81z~: %s", err.c_str());
		std::string st = std::string("no firmware: ") + s->s_name;
		status(x, st.c_str());
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

// keys down, from every MIDI byte that goes to the firmware
static void track(t_tx81z *x, const uint8_t *d, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		uint8_t b = d[i];
		if (b >= 0xF8) continue;
		if (b >= 0xF0) { x->trk_status = 0; continue; }
		if (b & 0x80) { x->trk_status = b; x->trk_n = 0; continue; }
		uint8_t st = x->trk_status & 0xF0, ch = x->trk_status & 0x0F;
		if (st == 0x80 || st == 0x90 || st == 0xB0)
		{
			if (x->trk_n == 0) { x->trk_d1 = b; x->trk_n = 1; continue; }
			x->trk_n = 0;
			if (st == 0xB0)
			{
				if (x->trk_d1 == 120 || x->trk_d1 == 123)
					for (int k = 0; k < 128; k++) if (x->held[ch][k]) { x->held[ch][k] = 0; x->nheld--; }
			}
			else
			{
				bool on = st == 0x90 && b > 0;
				uint8_t &h = x->held[ch][x->trk_d1];
				if (on && !h) { h = 1; x->nheld++; }
				else if (!on && h) { h = 0; x->nheld--; }
			}
			if (x->nheld == 0 && x->req_wanted) clock_delay(x->req_clock, 30);
		}
	}
}

static void fw_in(t_tx81z *x, std::initializer_list<uint8_t> bytes)
{
	track(x, bytes.begin(), bytes.size());
	x->synth->midi_in(bytes.begin(), bytes.size());
}

static void tx81z_bytes(t_tx81z *x, int argc, t_atom *argv)
{
	std::vector<uint8_t> b;
	for (int i = 0; i < argc; i++)
		if (argv[i].a_type == A_FLOAT)
			b.push_back(uint8_t(int(atom_getfloat(&argv[i])) & 0xff));
	if (b.empty()) return;
	track(x, b.data(), b.size());
	x->synth->midi_in(b.data(), b.size());
	// the firmware does not send its voice after a program change: ask for it
	for (uint8_t c : b) if ((c & 0xF0) == 0xC0) { clock_delay(x->req_clock, 400); break; }
}

static void tx81z_float(t_tx81z *x, t_floatarg f)
{
	uint8_t b = uint8_t(int(f) & 0xff);
	x->synth->midi_in(&b, 1);
}

static void tx81z_list(t_tx81z *x, t_symbol *, int argc, t_atom *argv) { tx81z_bytes(x, argc, argv); }
static void tx81z_midi(t_tx81z *x, t_symbol *, int argc, t_atom *argv) { tx81z_bytes(x, argc, argv); }

static t_tx81z *cur_x;   // for chan(): the object whose message is being handled

static uint8_t chan(int argc, t_atom *argv, int idx)
{
	int c = argc > idx ? int(atom_getfloat(&argv[idx])) : (cur_x ? rxch(cur_x) + 1 : 1);
	if (c < 1) c = 1;
	if (c > 16) c = 16;
	return uint8_t(c - 1);
}

static uint8_t b7(float f) { int v = int(f); return uint8_t(v < 0 ? 0 : v > 127 ? 127 : v); }

static void tx81z_note(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 2) return;
	uint8_t p = b7(atom_getfloat(&argv[0])), v = b7(atom_getfloat(&argv[1]));
	cur_x = x;
	uint8_t c = chan(argc, argv, 2);
	if (v) fw_in(x, { uint8_t(0x90 | c), p, v });
	else fw_in(x, { uint8_t(0x80 | c), p, 0 });
}

static void tx81z_cc(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 2) return;
	cur_x = x;
	fw_in(x, { uint8_t(0xB0 | chan(argc, argv, 2)), b7(atom_getfloat(&argv[0])), b7(atom_getfloat(&argv[1])) });
}

// in PLAY (single or performance) send the bytes now, otherwise press PLAY, release it,
// then send them
static bool in_play(t_tx81z *x)
{
	std::string l0 = x->synth->lcd_line(0);
	return l0.compare(0, 4, "PLAY") == 0 || l0.compare(0, 2, "PF") == 0;
}

static void play_tick(t_tx81z *x)
{
	if (x->play_stage == 1)
	{
		x->synth->set_button(TX81Z::BTN_PLAY, false);
		x->play_stage = 2;
		clock_delay(x->play_clock, 150);
		return;
	}
	x->play_stage = 0;
	if (!x->after_play->empty()) x->synth->midi_in(x->after_play->data(), x->after_play->size());
	x->after_play->clear();
}

static void play_then(t_tx81z *x, std::initializer_list<uint8_t> bytes)
{
	if (x->init_stage) return;
	if (x->play_stage) { x->after_play->insert(x->after_play->end(), bytes); return; }
	bool sys = bytes.size() == 0;   // after a system change: the UTILITY page opens a moment later
	if (!sys && in_play(x)) { x->synth->midi_in(bytes.begin(), bytes.size()); return; }
	x->after_play->assign(bytes);
	x->play_stage = sys ? 3 : 1;
	if (sys) clock_delay(x->play_clock, 100);
	else { x->synth->set_button(TX81Z::BTN_PLAY, true); clock_delay(x->play_clock, 120); }
}

static void play_tick_entry(t_tx81z *x)
{
	if (x->play_stage == 3)   // system change: press now (if UTILITY opened)
	{
		if (in_play(x)) { x->play_stage = 0; return; }
		x->synth->set_button(TX81Z::BTN_PLAY, true);
		x->play_stage = 1;
		clock_delay(x->play_clock, 120);
		return;
	}
	play_tick(x);
}

// receive channel: 0 = omni, 1-16. Messages without a channel and SysEx follow it.
static void tx81z_channel(t_tx81z *x, t_floatarg f)
{
	int c = int(f);
	if (c < 0 || c > 16) { pd_error(x, "tx81z~: channel 0 (omni) / 1-16"); return; }
	if (x->init_stage) return;
	x->synth->midi_in({ 0xF0, 0x43, uint8_t(0x10 | rxch(x)), 0x10, 0x7B, 1, uint8_t(c == 0 ? 16 : c - 1), 0xF7 });
	play_then(x, {});
}

static void report_channel(t_tx81z *x)
{
	int c = x->synth->ram(SYS_RAM + 1);
	c = c >= 16 ? 0 : c + 1;
	if (c == x->rx_reported) return;
	x->rx_reported = c;
	t_atom a;
	SETFLOAT(&a, c);
	outlet_anything(x->out_msg, gensym("channel"), 1, &a);
}

// the whole voice: getvoice -> voicedata <name> <93 VCED> <23 ACED> (data as in the TX81Z dumps,
// the name also as one symbol, spaces as _); voicedata ... -> ACED + VCED bulk dump to the
// firmware's edit buffer
static void tx81z_getvoice(t_tx81z *x)
{
	for (int i = 0; i < 93; i++) if (x->shadow[G_V][i] < 0) { pd_error(x, "tx81z~: voice not known yet (dumpreq first)"); return; }
	for (int i = 0; i < 23; i++) if (x->shadow[G_A][i] < 0) { pd_error(x, "tx81z~: voice not known yet (dumpreq first)"); return; }
	char name[11];
	for (int i = 0; i < 10; i++) { int c = x->shadow[G_V][77 + i]; name[i] = (c > 32 && c < 127) ? char(c) : '_'; }
	name[10] = 0;
	t_atom a[117];
	SETSYMBOL(&a[0], gensym(name));
	for (int i = 0; i < 93; i++) SETFLOAT(&a[1 + i], x->shadow[G_V][i]);
	for (int i = 0; i < 23; i++) SETFLOAT(&a[94 + i], x->shadow[G_A][i]);
	outlet_anything(x->out_msg, gensym("voicedata"), 117, a);
}

static void send_voice(t_tx81z *x, const uint8_t *vc, const uint8_t *ac);

static void tx81z_voicedata(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc == 117 || (argc > 0 && argv[0].a_type == A_SYMBOL)) { argc--; argv++; }   // the name (a symbol, or a number for a name like 2001)
	if (argc < 116) { pd_error(x, "tx81z~: voicedata needs 93 VCED + 23 ACED values"); return; }
	uint8_t vc[93], ac[23];
	for (int i = 0; i < 93; i++) vc[i] = b7(atom_getfloat(&argv[i]));
	for (int i = 0; i < 23; i++) ac[i] = b7(atom_getfloat(&argv[93 + i]));
	send_voice(x, vc, ac);
}

// a whole voice into the firmware's edit buffer: ACED + VCED bulk dump. If the firmware is still
// busy, the voice waits (only the newest one) and goes out from send_pending.
static void send_voice(t_tx81z *x, const uint8_t *vc, const uint8_t *ac)
{
	if (x->init_stage) return;
	for (int g = 0; g < 2; g++) for (int i = 0; i < 128; i++) x->pend[g][i] = -1;   // the voice replaces them
	if (x->send_wait > 0 || x->synth->midi_in_pending() > 0)
	{
		memcpy(x->voice_v, vc, 93); memcpy(x->voice_a, ac, 23);
		x->voice_waiting = true;
		for (int i = 0; i < 93; i++) x->shadow[G_V][i] = vc[i];
		for (int i = 0; i < 23; i++) x->shadow[G_A][i] = ac[i];
		clock_delay(x->rep_clock, 0);
		emit_voice_name(x, vc + 77);
		return;
	}
	send_voice_now(x, vc, ac);
}

static void send_voice_now(t_tx81z *x, const uint8_t *vc, const uint8_t *ac)
{
	uint8_t ch = rxch(x);
	std::vector<uint8_t> a = { 0xF0, 0x43, ch, 0x7E, 0x00, 0x21, 'L', 'M', ' ', ' ', '8', '9', '7', '6', 'A', 'E' };
	int sum = 0;
	for (int i = 6; i < 16; i++) sum += a[i];
	for (int i = 0; i < 23; i++) { a.push_back(ac[i] & 0x7F); sum += ac[i] & 0x7F; }
	a.push_back(uint8_t((-sum) & 0x7F)); a.push_back(0xF7);
	std::vector<uint8_t> v = { 0xF0, 0x43, ch, 0x03, 0x00, 0x5D };
	sum = 0;
	for (int i = 0; i < 93; i++) { v.push_back(vc[i] & 0x7F); sum += vc[i] & 0x7F; }
	v.push_back(uint8_t((-sum) & 0x7F)); v.push_back(0xF7);
	for (int g = 0; g < 2; g++) for (int i = 0; i < 128; i++) x->pend[g][i] = -1;   // the dump replaces them
	x->synth->midi_in(a.data(), a.size());
	x->synth->midi_in(v.data(), v.size());
	for (int i = 0; i < 93; i++) x->shadow[G_V][i] = v[6 + i];
	for (int i = 0; i < 23; i++) x->shadow[G_A][i] = a[16 + i];
	x->send_wait = ms_samples(VOICE_GAP_MS);
	clock_delay(x->rep_clock, 0);
	emit_voice_name(x, vc + 77);
}

static void tx81z_pgm(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 1) return;
	cur_x = x;
	// the firmware ignores program changes in EDIT and UTILITY mode: press PLAY first
	play_then(x, { uint8_t(0xC0 | chan(argc, argv, 1)), b7(atom_getfloat(&argv[0])) });
	clock_delay(x->req_clock, 700);   // then read the new voice (name, values for the editor)
}

static void tx81z_bend(t_tx81z *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 1) return;
	cur_x = x;
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
// The firmware stops taking notes when it is asked for a dump while a key is down (found with
// the v1.6 firmware: even the note off of that key is lost). So a dump request waits until
// all keys are up.
static void tx81z_dumpreq(t_tx81z *x)
{
	if (!x->rom_loaded || x->init_stage) return;
	if (x->nheld > 0) { x->req_wanted = true; return; }
	x->req_wanted = false;
	dump_request(x);
}

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
	x->synth->midi_in({ 0xF0, 0x43, uint8_t(0x10 | rxch(x)), 0x10, 0x7B, uint8_t(pp), uint8_t(dd), 0xF7 });
	play_then(x, {});   // a system change opens UTILITY on the TX81Z: back to PLAY
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
		status(x, "factory init done");
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
	status(x, "factory init ...");
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
	x->stat_clock = clock_new(x, (t_method)stat_tick);
	x->stat_text = new std::string();
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
	x->play_clock = clock_new(x, (t_method)play_tick_entry);
	x->play_stage = 0;
	x->rx_reported = -1;
	x->send_wait = 0;
	x->voice_waiting = false;
	memset(x->held, 0, sizeof x->held);
	x->nheld = 0;
	x->req_wanted = false;
	x->trk_status = x->trk_d1 = x->trk_n = 0;
	x->after_play = new std::vector<uint8_t>;

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
	clock_free(x->play_clock);
	clock_free(x->stat_clock);
	delete x->stat_text;
	delete x->after_play;
	delete x->sx;
	delete x->synth;
	delete x->nvram_path;
	delete x->midi_out;
	delete x->lcd0;
	delete x->lcd1;
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
static bool is_carrier(int alg, int op)
{
	static const unsigned char CAR[8] = { 0x1, 0x1, 0x1, 0x1, 0x5, 0x7, 0x7, 0xF };   // bit k-1 = op k
	return (CAR[alg & 7] >> (op - 1)) & 1;
}

static std::mt19937 &rng()
{
	static std::mt19937 g((unsigned)std::random_device{}() ^ (unsigned)time(nullptr));
	return g;
}

static int rr2(const int r[2]) { return std::uniform_int_distribution<int>(r[0], r[1])(rng()); }
static int rri(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng()); }
static int pick(const std::vector<int> &v) { return v[size_t(rri(0, int(v.size()) - 1))]; }

static void tx81z_random(t_tx81z *x, t_symbol *s, t_floatarg seed)
{
	const t_rng_cat *c = nullptr;
	bool total = s == gensym("total") || s == gensym("random");
	for (const auto &k : RNG_CATS) if (s == gensym(k.name)) c = &k;
	if (!c && !total) { pd_error(x, "tx81z~: random pluck / bass / lead / brass / pad / strings / total"); return; }
	if (seed != 0) rng().seed(unsigned(seed));
	uint8_t vc[93], ac[23];
	memset(ac, 0, sizeof ac);
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
	send_voice(x, vc, ac);
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
	class_addmethod(tx81z_class, (t_method)tx81z_channel, gensym("channel"), A_FLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_getvoice, gensym("getvoice"), A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_random, gensym("random"), A_SYMBOL, A_DEFFLOAT, A_NULL);
	class_addmethod(tx81z_class, (t_method)tx81z_voicedata, gensym("voicedata"), A_GIMME, A_NULL);
	class_addanything(tx81z_class, (t_method)tx81z_anything);
}
}
