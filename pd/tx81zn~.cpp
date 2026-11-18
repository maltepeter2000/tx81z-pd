// tx81zn~ : Yamaha TX81Z sound engine without the CPU emulation (native engine, single mode).
//
// The voice, note, modulation and controller handling of the v1.6 firmware are ported routine by
// routine (src/native, docs/native-engine.md) and drive the YM2414 directly; the register values are
// the firmware's. Needs the same user supplied ROM as tx81z~ (it reads its tables and the preset
// banks A-D from it). The battery RAM file of tx81z~ provides the internal bank I, the master tune and
// the receive channel; tx81zn~ never writes it.
//
// Same messages as tx81z~ where they make sense, so txedit / txcc work with both:
//   rom <file>, nvram <file>                 load (the internal bank comes from the nvram file)
//   <float> / list / midi <bytes...>        raw MIDI (notes, CC, bend, program change, SysEx
//                                           parameter changes and ACED + VCED voice dumps)
//   note <pitch> <vel> [ch], cc <num> <val> [ch], pgm <n> [ch], bend <-8192..8191> [ch]
//   bank <I|A|B|C|D> <1-32>                 select a voice of a bank directly
//   <name> <value>, n <name> <0-127>, ninfo <name>, vced <p> <d>, aced <p> <d>
//   getvoice, voicedata <name> <93 VCED> <23 ACED>, random <kind> [seed]
//   channel <0 omni | 1-16>, gain <f>, dumpreq (report every parameter again)
//   tune <0-127>                            master tune (64 = centre)
//   factoryinit                             all notes off, voice A01 (nothing is written)
//   fm <control> <0-127>                    FM view (txfm.h): roles C / A / B from the algorithm
// Program change: 0-31 internal I01-I32, 32-127 presets A01-C32 (the TX81Z's initial table).
// Outlets: left / right audio, messages: info <name> <0-127> <text>, voice <name>, voicedata ...,
//   fm <control> <0-127> <text>, channel <n>, status <text>, lcd <line1> <line2> (the splash screen, once on load)

#include "m_pd.h"
#include "txnative.h"
#include "txvoice.h"
#include "txshared.h"
#include "txfm.h"

#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static t_class *txn_class;

struct t_txn
{
	t_object x_obj;
	txn::Engine *eng;
	t_outlet *out_msg;
	t_symbol *dir;
	std::vector<uint8_t> *rom;
	std::vector<uint8_t> *internal;   // 32 voice records
	bool ready;
	float gain;
	int channel;                      // 0 = omni, 1-16
	int reported[NTXPARAMS];
	int fm_reported[64];
	char fm_text[64][16];
	t_clock *rep_clock;
	t_clock *stat_clock;
	t_clock *ch_clock;      // reports the channel once the outlets are connected
	std::string *stat_text;
	// MIDI parser
	uint8_t status, d[2];
	int nd;
	std::vector<uint8_t> *sx;
	bool in_sx;
	// resampler
	double phase, step;
	float hl[4], hr[4];
};

// ---------------------------------------------------------------------------

static std::string resolve(t_txn *x, t_symbol *s)
{
	std::string p = s->s_name;
	if (!p.empty() && p[0] != '/' && p[0] != '~' && !(p.size() > 1 && p[1] == ':'))
		p = std::string(x->dir->s_name) + "/" + p;
	return p;
}

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

static bool read_file(const std::string &path, std::vector<uint8_t> &out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

static void stat_tick(t_txn *x)
{
	t_atom a;
	SETSYMBOL(&a, gensym(x->stat_text->c_str()));
	outlet_anything(x->out_msg, gensym("status"), 1, &a);
}

static void status(t_txn *x, const char *text)
{
	*x->stat_text = text;
	clock_delay(x->stat_clock, 0);
}

static uint8_t b7(float f) { int v = int(f); return uint8_t(v < 0 ? 0 : v > 127 ? 127 : v); }

// ---------------------------------------------------------------------------
// parameters and reports

static int param_value(t_txn *x, const t_txparam *p) { return x->eng->get_param(p->group, p->num); }

static void emit_info(t_txn *x, int i)
{
	const t_txparam *p = &TXPARAMS[i];
	int v = param_value(x, p);
	v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
	char buf[32];
	value_text(p, v, buf, sizeof buf);
	t_atom a[3];
	SETSYMBOL(&a[0], gensym(p->name));
	SETFLOAT(&a[1], to127(p, v));
	SETSYMBOL(&a[2], gensym(buf));
	outlet_anything(x->out_msg, gensym("info"), 3, a);
}

// FM view (txfm.h): fm <name> <0-127> <text>; an empty role reports - as its text
static void emit_fm(t_txn *x, int i, int v, const char *text)
{
	t_atom a[3];
	SETSYMBOL(&a[0], gensym(txfm::MACROS[i].name));
	SETFLOAT(&a[1], v);
	SETSYMBOL(&a[2], gensym(text));
	outlet_anything(x->out_msg, gensym("fm"), 3, a);
}

static_assert(sizeof txfm::MACROS / sizeof txfm::MACROS[0] <= 64, "fm cache");

static int fm_value(t_txn *x, int i, char *text, size_t nb)
{
	int v;
	bool on = txfm::fm_get(i, [x](char g, int n) { return x->eng->get_param(g, n); }, v, text, nb);
	return on ? v : -1 - v;   // the cache tells an empty role apart
}

// every change is reported once as info (and as fm for the FM view)
static void rep_tick(t_txn *x)
{
	if (!x->ready) return;
	for (int i = 0; i < NTXPARAMS; i++)
	{
		int v = param_value(x, &TXPARAMS[i]);
		if (v == x->reported[i]) continue;
		x->reported[i] = v;
		emit_info(x, i);
	}
	for (int i = 0; i < txfm::fm_count(); i++)
	{
		char text[32];
		int c = fm_value(x, i, text, sizeof text);
		text[15] = 0;   // the ratio text also depends on fine and fixed: compare it too
		if (c == x->fm_reported[i] && !std::strcmp(text, x->fm_text[i])) continue;
		x->fm_reported[i] = c;
		std::strcpy(x->fm_text[i], text);
		emit_fm(x, i, c < 0 ? -1 - c : c, text);
	}
}

// voice name as one symbol, spaces as '_' (as tx81z~)
static t_symbol *voice_name(t_txn *x)
{
	char name[11];
	for (int i = 0; i < 10; i++)
	{
		int c = x->eng->get_param('V', 77 + i);
		name[i] = (c > 32 && c < 127) ? char(c) : '_';
	}
	name[10] = 0;
	return gensym(name);
}

static void emit_voice_name(t_txn *x)
{
	t_atom a;
	SETSYMBOL(&a, voice_name(x));
	outlet_anything(x->out_msg, gensym("voice"), 1, &a);
}

static void set_param(t_txn *x, char g, int num, int v)
{
	if (!x->ready) return;
	x->eng->set_param(g, num, v, false);
	clock_delay(x->rep_clock, 0);
}

static void load_voice(t_txn *x, const uint8_t *vc, const uint8_t *ac)
{
	if (!x->ready) return;
	x->eng->load_voice(vc, ac);
	clock_delay(x->rep_clock, 0);
	emit_voice_name(x);
}

static void load_record(t_txn *x, const uint8_t *rec)
{
	uint8_t vc[93], ac[23];
	txn::unpack_voice(rec, vc, ac);
	load_voice(x, vc, ac);
}

// bank 0 = I, 1-4 = A-D; slot 0-31
static void select_voice(t_txn *x, int bank, int slot)
{
	if (!x->ready || slot < 0 || slot > 31) return;
	if (bank == 0)
	{
		if (x->internal->size() < 32 * txn::VOICE_RECORD) { status(x, "no internal bank (nvram)"); return; }
		load_record(x, x->internal->data() + slot * txn::VOICE_RECORD);
	}
	else
		load_record(x, x->rom->data() + txn::ROM_PRESETS + ((bank - 1) * 32 + slot) * txn::VOICE_RECORD);
}

static void program(t_txn *x, int pc)
{
	pc &= 127;
	if (pc < 32) select_voice(x, 0, pc);
	else select_voice(x, 1 + (pc - 32) / 32, (pc - 32) % 32);
}

// ---------------------------------------------------------------------------
// MIDI in

static bool my_channel(t_txn *x, int ch) { return x->channel == 0 || ch == x->channel - 1; }

static void sysex(t_txn *x, const std::vector<uint8_t> &m)
{
	if (m.size() < 4 || m[1] != 0x43) return;
	int ch = m[2] & 15;
	if (!my_channel(x, ch)) return;
	// parameter change F0 43 1n 12|13 pp dd F7
	if ((m[2] & 0xF0) == 0x10 && m.size() >= 7 && (m[3] == 0x12 || m[3] == 0x13))
	{
		set_param(x, m[3] == 0x12 ? 'V' : 'A', m[4], m[5]);
		return;
	}
	// ACED dump F0 43 0n 7E 00 21 "LM  8976AE" <23> sum F7, VCED dump F0 43 0n 03 00 5D <93> sum F7
	static uint8_t aced[23];
	static bool have_aced = false;
	if ((m[2] & 0xF0) == 0x00 && m[3] == 0x7E && m.size() >= 16 + 23 && std::memcmp(&m[6], "LM  8976AE", 10) == 0)
	{
		std::memcpy(aced, &m[16], 23);
		have_aced = true;
	}
	else if ((m[2] & 0xF0) == 0x00 && m[3] == 0x03 && m.size() >= 6 + 93)
	{
		uint8_t ac[23] = { 0 };
		if (have_aced) std::memcpy(ac, aced, 23);
		have_aced = false;
		load_voice(x, &m[6], ac);
	}
}

static void channel_msg(t_txn *x, uint8_t st, const uint8_t *d)
{
	if (!my_channel(x, st & 15)) return;
	switch (st & 0xF0)
	{
	case 0x80: x->eng->note_off(d[0]); break;
	case 0x90: x->eng->note_on(d[0], d[1]); break;
	case 0xB0: x->eng->control_change(d[0], d[1]); break;
	case 0xC0: program(x, d[0]); break;
	case 0xE0: x->eng->pitch_bend(d[0] | d[1] << 7); break;
	default: break;
	}
}

static void midi_byte(t_txn *x, uint8_t b)
{
	if (b >= 0xF8) return;   // realtime
	if (b == 0xF0) { x->sx->assign(1, b); x->in_sx = true; return; }
	if (x->in_sx)
	{
		if (b == 0xF7) { x->sx->push_back(b); x->in_sx = false; sysex(x, *x->sx); }
		else if (b < 0x80) x->sx->push_back(b);
		else x->in_sx = false;
		if (b < 0x80) return;
		if (b == 0xF7) return;
	}
	if (b >= 0x80) { x->status = b < 0xF0 ? b : 0; x->nd = 0; return; }
	if (!x->status) return;
	x->d[x->nd++] = b;
	int need = ((x->status & 0xF0) == 0xC0 || (x->status & 0xF0) == 0xD0) ? 1 : 2;
	if (x->nd < need) return;
	x->nd = 0;
	channel_msg(x, x->status, x->d);
}

static void txn_bytes(t_txn *x, int argc, t_atom *argv)
{
	for (int i = 0; i < argc; i++) midi_byte(x, b7(atom_getfloat(&argv[i])) | (atom_getfloat(&argv[i]) >= 128 ? 0x80 : 0));
}

static void txn_float(t_txn *x, t_floatarg f)
{
	int v = int(f);
	if (v >= 0 && v < 256) midi_byte(x, uint8_t(v));
}

static void txn_list(t_txn *x, t_symbol *, int argc, t_atom *argv)
{
	for (int i = 0; i < argc; i++) txn_float(x, atom_getfloat(&argv[i]));
}

static int chan_arg(t_txn *x, int argc, t_atom *argv, int idx)
{
	if (argc > idx) { int c = int(atom_getfloat(&argv[idx])); return (c < 1 || c > 16) ? 0 : c - 1; }
	return x->channel == 0 ? 0 : x->channel - 1;
}

static void txn_note(t_txn *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 2) return;
	uint8_t d[2] = { b7(atom_getfloat(&argv[0])), b7(atom_getfloat(&argv[1])) };
	channel_msg(x, uint8_t(0x90 | chan_arg(x, argc, argv, 2)), d);
}

static void txn_cc(t_txn *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 2) return;
	uint8_t d[2] = { b7(atom_getfloat(&argv[0])), b7(atom_getfloat(&argv[1])) };
	channel_msg(x, uint8_t(0xB0 | chan_arg(x, argc, argv, 2)), d);
}

static void txn_pgm(t_txn *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 1) return;
	uint8_t d[2] = { b7(atom_getfloat(&argv[0])), 0 };
	channel_msg(x, uint8_t(0xC0 | chan_arg(x, argc, argv, 1)), d);
}

static void txn_bend(t_txn *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc < 1) return;
	int v = int(atom_getfloat(&argv[0])) + 8192;
	v = v < 0 ? 0 : v > 16383 ? 16383 : v;
	uint8_t d[2] = { uint8_t(v & 127), uint8_t(v >> 7) };
	channel_msg(x, uint8_t(0xE0 | chan_arg(x, argc, argv, 1)), d);
}

static void txn_bank(t_txn *x, t_symbol *b, t_floatarg n)
{
	static const char *NAMES = "IABCD";
	const char *p = b->s_name[0] ? std::strchr(NAMES, std::toupper((unsigned char)b->s_name[0])) : nullptr;
	if (!p || n < 1 || n > 32) { pd_error(x, "tx81zn~: bank <I|A|B|C|D> <1-32>"); return; }
	select_voice(x, int(p - NAMES), int(n) - 1);
}

// ---------------------------------------------------------------------------
// parameters (as tx81z~)

static void txn_n(t_txn *x, t_symbol *name, t_floatarg c)
{
	if (name == gensym("none")) return;
	const t_txparam *p = find_param(name);
	if (!p) { pd_error(x, "tx81zn~: n: unknown parameter '%s'", name->s_name); return; }
	set_param(x, p->group, p->num, from127(p, c));
}

static void txn_ninfo(t_txn *x, t_symbol *name)
{
	const t_txparam *p = find_param(name);
	if (p && x->ready) emit_info(x, int(p - TXPARAMS));
}

static void txn_vced(t_txn *x, t_floatarg p, t_floatarg d) { set_param(x, 'V', int(p), int(d)); }
static void txn_aced(t_txn *x, t_floatarg p, t_floatarg d) { set_param(x, 'A', int(p), int(d)); }

static void txn_fm(t_txn *x, t_symbol *name, t_floatarg v)
{
	int i = txfm::fm_find(name->s_name);
	if (i < 0) { pd_error(x, "tx81zn~: fm: unknown control '%s'", name->s_name); return; }
	if (!x->ready) return;
	txfm::fm_set(i, int(v), [x](char g, int n) { return x->eng->get_param(g, n); },
		[x](char g, int n, int val) { x->eng->set_param(g, n, val, false); });
	clock_delay(x->rep_clock, 0);
}

static void report_channel(t_txn *x);

static void txn_dumpreq(t_txn *x)
{
	for (int i = 0; i < NTXPARAMS; i++) x->reported[i] = -1;
	for (int i = 0; i < 64; i++) x->fm_reported[i] = INT32_MIN;
	clock_delay(x->rep_clock, 0);
	if (x->ready) emit_voice_name(x);
	report_channel(x);
}

static void txn_anything(t_txn *x, t_symbol *s, int argc, t_atom *argv)
{
	const t_txparam *p = find_param(s);
	if (!p || argc < 1 || argv[0].a_type != A_FLOAT) { pd_error(x, "tx81zn~: no method for '%s'", s->s_name); return; }
	int v = int(std::lround(atom_getfloat(&argv[0])));
	v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
	set_param(x, p->group, p->num, v);
}

static void txn_getvoice(t_txn *x)
{
	if (!x->ready) return;
	t_atom a[117];
	SETSYMBOL(&a[0], voice_name(x));
	for (int i = 0; i < 93; i++) SETFLOAT(&a[1 + i], x->eng->get_param('V', i));
	for (int i = 0; i < 23; i++) SETFLOAT(&a[94 + i], x->eng->get_param('A', i));
	outlet_anything(x->out_msg, gensym("voicedata"), 117, a);
}

static void txn_voicedata(t_txn *x, t_symbol *, int argc, t_atom *argv)
{
	if (argc == 117 || (argc > 0 && argv[0].a_type == A_SYMBOL)) { argc--; argv++; }
	if (argc < 116) { pd_error(x, "tx81zn~: voicedata needs 93 VCED + 23 ACED values"); return; }
	uint8_t vc[93], ac[23];
	for (int i = 0; i < 93; i++) vc[i] = b7(atom_getfloat(&argv[i]));
	for (int i = 0; i < 23; i++) ac[i] = b7(atom_getfloat(&argv[93 + i]));
	load_voice(x, vc, ac);
}

static void txn_random(t_txn *x, t_symbol *s, t_floatarg seed)
{
	uint8_t vc[93], ac[23];
	if (!random_voice(s->s_name, unsigned(seed), vc, ac))
	{
		pd_error(x, "tx81zn~: random pluck / bass / lead / brass / pad / strings / total");
		return;
	}
	load_voice(x, vc, ac);
}

static void report_channel(t_txn *x)
{
	t_atom a;
	SETFLOAT(&a, x->channel);
	outlet_anything(x->out_msg, gensym("channel"), 1, &a);
}

static void txn_channel(t_txn *x, t_floatarg f)
{
	int c = int(f);
	if (c < 0 || c > 16) { pd_error(x, "tx81zn~: channel 0 (omni) / 1-16"); return; }
	x->channel = c;
	report_channel(x);
}

// the native engine keeps no battery RAM of its own: reload voice A01 and reset the controllers
static void txn_factoryinit(t_txn *x)
{
	if (!x->ready) return;
	x->eng->control_change(123, 0);
	select_voice(x, 1, 0);
	status(x, "factory voice A01 (tx81zn~ never writes the battery RAM)");
}

static void txn_gain(t_txn *x, t_floatarg f) { x->gain = f; }

static void txn_tune(t_txn *x, t_floatarg f)
{
	if (x->ready) x->eng->master_tune(b7(f));
}

// ---------------------------------------------------------------------------
// files

static void splash(t_txn *x)
{
	t_atom a[2];
	SETSYMBOL(&a[0], gensym("* YAMAHA TX81Z *"));
	SETSYMBOL(&a[1], gensym("<Good morning!!>"));
	outlet_anything(x->out_msg, gensym("lcd"), 2, a);
}

static void txn_nvram(t_txn *x, t_symbol *s)
{
	std::string path = find_nocase(resolve(x, s));
	std::vector<uint8_t> nv;
	if (!read_file(path, nv) || nv.size() < 0x1567)
	{
		pd_error(x, "tx81zn~: cannot read battery RAM file %s", path.c_str());
		return;
	}
	x->internal->assign(nv.begin() + txn::NVRAM_INTERNAL, nv.begin() + txn::NVRAM_INTERNAL + 32 * txn::VOICE_RECORD);
	// system setup (see tx81z~): 0x1565 master tune, 0x1566 receive channel (16 = omni)
	if (x->ready) x->eng->master_tune(nv[0x1565]);
	int rx = nv[0x1566];
	x->channel = rx >= 16 ? 0 : rx + 1;
	clock_delay(x->ch_clock, 0);
}

static void txn_rom(t_txn *x, t_symbol *s)
{
	std::string path = find_nocase(resolve(x, s));
	if (!read_file(path, *x->rom))
	{
		pd_error(x, "tx81zn~: cannot read ROM %s", path.c_str());
		status(x, "ROM not found");
		return;
	}
	std::string err;
	delete x->eng;
	x->eng = new txn::Engine();
	if (!x->eng->init(x->rom->data(), x->rom->size(), err))
	{
		x->ready = false;
		pd_error(x, "tx81zn~: %s", err.c_str());
		status(x, err.c_str());
		return;
	}
	x->ready = true;
	x->step = double(x->eng->sample_rate()) / (sys_getsr() > 0 ? sys_getsr() : 44100);
	post("tx81zn~: loaded %s", path.c_str());
	splash(x);
	status(x, "native engine ready");
	select_voice(x, 1, 0);   // A01 until a program change
}

// ---------------------------------------------------------------------------
// DSP

static t_int *txn_perform(t_int *w)
{
	t_txn *x = (t_txn *)(w[1]);
	t_sample *outl = (t_sample *)(w[2]);
	t_sample *outr = (t_sample *)(w[3]);
	int n = (int)(w[4]);
	if (!x->ready)
	{
		std::memset(outl, 0, n * sizeof(t_sample));
		std::memset(outr, 0, n * sizeof(t_sample));
		return w + 5;
	}
	for (int i = 0; i < n; i++)
	{
		x->phase += x->step;
		while (x->phase >= 1.0)
		{
			x->phase -= 1.0;
			float l, r;
			x->eng->render(&l, &r, 1);
			x->hl[0] = x->hl[1]; x->hl[1] = x->hl[2]; x->hl[2] = x->hl[3]; x->hl[3] = l;
			x->hr[0] = x->hr[1]; x->hr[1] = x->hr[2]; x->hr[2] = x->hr[3]; x->hr[3] = r;
		}
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
	return w + 5;
}

static void txn_dsp(t_txn *x, t_signal **sp)
{
	if (x->eng) x->step = double(x->eng->sample_rate()) / sp[0]->s_sr;
	dsp_add(txn_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, (t_int)sp[0]->s_n);
}

// ---------------------------------------------------------------------------

static void *txn_new(t_symbol *, int argc, t_atom *argv)
{
	t_txn *x = (t_txn *)pd_new(txn_class);
	x->eng = nullptr;
	x->rom = new std::vector<uint8_t>();
	x->internal = new std::vector<uint8_t>();
	x->dir = canvas_getdir(canvas_getcurrent());
	x->ready = false;
	x->gain = 1.0f;
	x->channel = 0;          // omni until the battery RAM says otherwise
	for (int i = 0; i < NTXPARAMS; i++) x->reported[i] = -1;
	for (int i = 0; i < 64; i++) x->fm_reported[i] = INT32_MIN;
	x->status = 0;
	x->nd = 0;
	x->sx = new std::vector<uint8_t>();
	x->in_sx = false;
	x->phase = 0;
	x->step = 1;
	std::memset(x->hl, 0, sizeof x->hl);
	std::memset(x->hr, 0, sizeof x->hr);
	outlet_new(&x->x_obj, &s_signal);
	outlet_new(&x->x_obj, &s_signal);
	x->out_msg = outlet_new(&x->x_obj, 0);
	x->rep_clock = clock_new(x, (t_method)rep_tick);
	x->stat_clock = clock_new(x, (t_method)stat_tick);
	x->ch_clock = clock_new(x, (t_method)report_channel);
	x->stat_text = new std::string();
	// [tx81zn~ <rom> <nvram>]
	if (argc >= 1 && argv[0].a_type == A_SYMBOL) txn_rom(x, atom_getsymbol(&argv[0]));
	if (argc >= 2 && argv[1].a_type == A_SYMBOL) txn_nvram(x, atom_getsymbol(&argv[1]));
	return x;
}

static void txn_free(t_txn *x)
{
	clock_free(x->rep_clock);
	clock_free(x->stat_clock);
	clock_free(x->ch_clock);
	delete x->stat_text;
	delete x->eng;
	delete x->rom;
	delete x->internal;
	delete x->sx;
}

extern "C" {
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void tx81zn_tilde_setup(void)
{
	txn_class = class_new(gensym("tx81zn~"), (t_newmethod)txn_new, (t_method)txn_free,
		sizeof(t_txn), CLASS_DEFAULT, A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_dsp, gensym("dsp"), A_CANT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_rom, gensym("rom"), A_SYMBOL, A_NULL);
	class_addmethod(txn_class, (t_method)txn_nvram, gensym("nvram"), A_SYMBOL, A_NULL);
	class_addmethod(txn_class, (t_method)txn_list, gensym("midi"), A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_note, gensym("note"), A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_cc, gensym("cc"), A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_pgm, gensym("pgm"), A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_bend, gensym("bend"), A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_bank, gensym("bank"), A_SYMBOL, A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_n, gensym("n"), A_SYMBOL, A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_ninfo, gensym("ninfo"), A_SYMBOL, A_NULL);
	class_addmethod(txn_class, (t_method)txn_vced, gensym("vced"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_aced, gensym("aced"), A_FLOAT, A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_fm, gensym("fm"), A_SYMBOL, A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_dumpreq, gensym("dumpreq"), A_NULL);
	class_addmethod(txn_class, (t_method)txn_getvoice, gensym("getvoice"), A_NULL);
	class_addmethod(txn_class, (t_method)txn_voicedata, gensym("voicedata"), A_GIMME, A_NULL);
	class_addmethod(txn_class, (t_method)txn_random, gensym("random"), A_SYMBOL, A_DEFFLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_channel, gensym("channel"), A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_factoryinit, gensym("factoryinit"), A_NULL);
	class_addmethod(txn_class, (t_method)txn_gain, gensym("gain"), A_FLOAT, A_NULL);
	class_addmethod(txn_class, (t_method)txn_tune, gensym("tune"), A_FLOAT, A_NULL);
	class_addfloat(txn_class, (t_method)txn_float);
	class_addlist(txn_class, (t_method)txn_list);
	class_addanything(txn_class, (t_method)txn_anything);
}
}
