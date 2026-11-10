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
//                         init, ~16 s); runs automatically if the nvram file
//                         given as argument does not exist yet
//   buttons: store utility edit play param- param+ dec inc vol- vol+ cursor
//
// Outlets: left / right audio, messages:
//   lcd <line1> <line2>   on every LCD change (symbols)
//   led <1-4 bitmask>
//   midiout <byte>        MIDI Out of the TX81Z

#include "m_pd.h"
#include "tx81z.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static t_class *tx81z_class;

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
	bool nvram_ok;           // battery RAM file was loaded

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
static void tx81z_init_tick(t_tx81z *x)
{
	switch (x->init_stage)
	{
	case 1:  // release the held buttons
		x->synth->set_button(TX81Z::BTN_VOL_LEFT, false);
		x->synth->set_button(TX81Z::BTN_CURSOR, false);
		x->init_stage = 2;
		clock_delay(x->init_clock, 2000);
		break;
	case 2:  // INC: load presets
		x->synth->set_button(TX81Z::BTN_INC, true);
		x->init_stage = 3;
		clock_delay(x->init_clock, 200);
		break;
	case 3:
		x->synth->set_button(TX81Z::BTN_INC, false);
		x->init_stage = 4;
		clock_delay(x->init_clock, 2000);
		break;
	case 4:  // power-cycle, battery RAM is kept
		x->synth->reset();
		x->init_stage = 5;
		clock_delay(x->init_clock, 6000);
		break;
	case 5:  // after the boot animation: PLAY mode
		x->synth->set_button(TX81Z::BTN_PLAY, true);
		x->init_stage = 6;
		clock_delay(x->init_clock, 150);
		break;
	case 6:
		x->synth->set_button(TX81Z::BTN_PLAY, false);
		// program change switch -> "com", otherwise program changes are ignored
		x->synth->midi_in({ 0xF0, 0x43, 0x10, 0x10, 0x7B, 0x03, 0x01, 0xF7 });
		x->init_stage = 7;
		clock_delay(x->init_clock, 500);
		break;
	case 7:
		x->init_stage = 0;
		if (!x->nvram_path->empty()) x->synth->save_nvram(*x->nvram_path);
		post("tx81z~: factory init done (presets loaded, program change on, battery RAM saved)");
		break;
	default:
		x->init_stage = 0;
		break;
	}
}

static void tx81z_factoryinit(t_tx81z *x)
{
	if (!x->rom_loaded) { pd_error(x, "tx81z~: load a ROM first"); return; }
	post("tx81z~: factory init (about 16 s) ...");
	tx81z_release(x);
	for (int b = 0; b < TX81Z::BTN_COUNT; b++) x->synth->set_button(TX81Z::Button(b), false);
	x->synth->set_button(TX81Z::BTN_VOL_LEFT, true);
	x->synth->set_button(TX81Z::BTN_CURSOR, true);
	x->synth->reset();
	x->init_stage = 1;
	clock_delay(x->init_clock, 3000);
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
	x->nvram_ok = false;

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
}
}
