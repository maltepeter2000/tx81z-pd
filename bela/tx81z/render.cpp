// TX81Z emulator for Bela (native C++ project).
//
// Project folder contents: this file + tx81z.*, hd44780.h, cpu/, ymfm_*,
// the firmware (tx81z-v1.6.bin) and optionally tx81z.nvram.
//
// All files live flat in this folder; the ymfm copy here is patched to build
// with Bela's default C++11, so no extra make parameters are needed.
//
// Features
//   - runs the original firmware, stereo out on audio outputs 0/1
//   - MIDI in from all USB MIDI ports found (hw:1,0,0 / hw:1,0,1 / hw:2,0,0)
//   - LCD -> O2O OLED display via OSC (/display-strings-and-numbers, 127.0.0.1:7562)
//   - auto play: selects C15 "LatelyBass" (program change 110) and loops
//     4 bars Gm - Bb - Dm - F in a gallop rhythm (long-short-short) or
//     straight eighths; stops as soon as real MIDI arrives
//   - no battery RAM file? -> runs the factory init (test mode: presets, volume)
//     and reboots the emulated synth automatically
//   - battery RAM saved on stop (cleanup) and on OSC /tx81z/save
//   - OSC in on port 7563: /tx81z/press <button>, /tx81z/button <button> <0|1>,
//     /tx81z/autoplay <0|1>, /tx81z/save
//     buttons: store utility edit play param- param+ dec inc vol- vol+ cursor

#include <Bela.h>
#include <libraries/Midi/Midi.h>
#include <libraries/OscSender/OscSender.h>
#include <libraries/OscReceiver/OscReceiver.h>

#include "tx81z.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

static const char *kRomFile   = "tx81z-v1.6.bin";
static const char *kNvramFile = "tx81z.nvram";
static const char *kMidiPorts[] = { "hw:1,0,0", "hw:1,0,1", "hw:2,0,0" };
static const char *kOledHost  = "127.0.0.1";
static const int   kOledPort  = 7562;            // O2O
static const int   kOscInPort = 7563;
static const float kGain      = 1.0f;
static bool        gAutoPlayDefault = true;

// ---------------------------------------------------------------------------

static TX81Z *gSynth;
static std::vector<std::unique_ptr<Midi>> gMidi;
static OscSender gOscSender;
static OscReceiver gOscReceiver;

// resampler (native ~55.93 kHz -> Bela 44.1 kHz), 4-point Hermite
static double gPhase = 0.0, gStep = 1.0;
static float gHl[4], gHr[4];

// LCD -> OLED (sent from an auxiliary task, never from the audio thread)
static AuxiliaryTask gLcdTask;
static AuxiliaryTask gSaveTask;
static char gLcdPending[2][17];
static char gLcdSent[2][17];
static std::atomic<bool> gLcdBusy{false};
static uint64_t gLastLcdFrame = 0;
static bool gLcdDirty = false;

// button requests from OSC (non-RT thread) -> audio thread
static std::atomic<int> gPressRequest{-1};
static std::atomic<int> gButtonState[TX81Z::BTN_COUNT];
static std::atomic<bool> gSaveRequest{false};
static std::atomic<bool> gAutoPlay{true};
static int gPressedButton = -1;
static int gPressFramesLeft = 0;

// factory init state
enum InitState { INIT_NONE, INIT_HOLD, INIT_WAIT_INC, INIT_RELEASE_INC, INIT_REBOOT };
static InitState gInit = INIT_NONE;
static uint64_t gFrame = 0;

// auto play: 4-bar loop, one root note per bar, 16th-note grid
static const int   kAutoProgram = 110;                  // C15 LatelyBass (I01-C32 = 0-127)
static const float kAutoBpm     = 110.f;
static const bool  kGallop      = true;                 // true: lang-kurz-kurz, false: Achtel
static const int   kAutoRoots[4] = { 43, 46, 38, 41 };  // G2 (Gm), Bb2 (Bb), D2 (Dm), F2 (F)
static const int   kAutoVelocity = 100;
static uint64_t gAutoNextFrame = 0;
static int gAutoStep = 0;            // 16th-note counter, -1 = program change pending
static bool gAutoProgramSent = false;
static int gAutoNote = -1;
static uint64_t gAutoNoteOff = 0;
static float gSampleRate = 44100.f;

static uint64_t secondsToFrames(float s) { return uint64_t(s * gSampleRate); }


// ---------------------------------------------------------------------------
// auxiliary tasks / OSC
// ---------------------------------------------------------------------------

static void sendLcd(void *)
{
	std::string l0(gLcdPending[0]), l1(gLcdPending[1]);
	gOscSender.newMessage("/display-strings-and-numbers").add(l0).add(std::string("\n")).add(l1).send();
	gLcdBusy = false;
}

static void saveNvram(void *)
{
	if (gSynth && gSynth->save_nvram(kNvramFile)) printf("tx81z: battery RAM saved\n");
}

static void onOsc(oscpkt::Message *msg, const char *, void *)
{
	std::string name;
	int value;
	if (msg->match("/tx81z/press").popStr(name).isOkNoMoreArgs())
	{
		TX81Z::Button b;
		if (TX81Z::button_from_name(name, b)) gPressRequest = int(b);
	}
	else if (msg->match("/tx81z/button").popStr(name).popInt32(value).isOkNoMoreArgs())
	{
		TX81Z::Button b;
		if (TX81Z::button_from_name(name, b)) gButtonState[b] = value ? 1 : 0;
	}
	else if (msg->match("/tx81z/autoplay").popInt32(value).isOkNoMoreArgs())
		gAutoPlay = value != 0;
	else if (msg->match("/tx81z/save").isOkNoMoreArgs())
		gSaveRequest = true;
}

// ---------------------------------------------------------------------------
// helpers (audio thread)
// ---------------------------------------------------------------------------

static void pressButton(TX81Z::Button b, float seconds)
{
	if (gPressedButton >= 0) gSynth->set_button(TX81Z::Button(gPressedButton), false);
	gSynth->set_button(b, true);
	gPressedButton = b;
	gPressFramesLeft = int(seconds * gSampleRate);
}

static void autoPlayStop()
{
	if (gAutoNote >= 0)
	{
		gSynth->midi_in({ 0x80, uint8_t(gAutoNote), 0 });
		gAutoNote = -1;
	}
}

static void autoPlayTick()
{
	if (!gAutoPlay || gInit != INIT_NONE) return;

	if (gAutoNote >= 0 && gFrame >= gAutoNoteOff)
	{
		gSynth->midi_in({ 0x80, uint8_t(gAutoNote), 0 });
		gAutoNote = -1;
	}
	if (gFrame < gAutoNextFrame) return;

	// first: select the voice, give the firmware half a second to load it
	if (!gAutoProgramSent)
	{
		gSynth->midi_in({ 0xC0, uint8_t(kAutoProgram) });
		gAutoProgramSent = true;
		gAutoStep = 0;
		gAutoNextFrame = gFrame + secondsToFrames(0.5f);
		return;
	}

	const float sixteenth = 60.f / kAutoBpm / 4.f;
	int pos = gAutoStep % 4;                       // position within the beat
	int len = 0;                                   // note length in 16ths, 0 = no note
	if (kGallop)
		len = (pos == 0) ? 2 : (pos >= 2 ? 1 : 0); // lang (8th) - kurz - kurz (16ths)
	else
		len = (pos == 0 || pos == 2) ? 2 : 0;      // straight eighths

	if (len)
	{
		int bar = (gAutoStep / 16) % 4;
		int note = kAutoRoots[bar];
		if (gAutoNote >= 0) gSynth->midi_in({ 0x80, uint8_t(gAutoNote), 0 });
		gSynth->midi_in({ 0x90, uint8_t(note), uint8_t(kAutoVelocity) });
		gAutoNote = note;
		gAutoNoteOff = gFrame + secondsToFrames(sixteenth * len * 0.8f);
	}
	gAutoNextFrame = gFrame + secondsToFrames(sixteenth);
	gAutoStep++;
}

static void factoryInitTick()
{
	// test mode entry (service manual p.28): hold MASTER VOLUME + CURSOR at
	// power-on, then INC loads the presets and sets the MIDI parameters
	switch (gInit)
	{
	case INIT_HOLD:
		if (gFrame >= secondsToFrames(3.f))
		{
			gSynth->set_button(TX81Z::BTN_VOL_LEFT, false);
			gSynth->set_button(TX81Z::BTN_CURSOR, false);
			gInit = INIT_WAIT_INC;
		}
		break;
	case INIT_WAIT_INC:
		if (gFrame >= secondsToFrames(5.f))
		{
			gSynth->set_button(TX81Z::BTN_INC, true);
			gInit = INIT_RELEASE_INC;
		}
		break;
	case INIT_RELEASE_INC:
		if (gFrame >= secondsToFrames(5.2f))
		{
			gSynth->set_button(TX81Z::BTN_INC, false);
			gInit = INIT_REBOOT;
		}
		break;
	case INIT_REBOOT:
		if (gFrame >= secondsToFrames(7.f))
		{
			rt_printf("tx81z: factory init done, rebooting emulated synth\n");
			gSynth->reset();                     // battery RAM is kept
			gInit = INIT_NONE;
			gAutoNextFrame = gFrame + secondsToFrames(6.f);   // after the boot animation
			gAutoProgramSent = false;
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Bela callbacks
// ---------------------------------------------------------------------------

bool setup(BelaContext *context, void *)
{
	gSampleRate = context->audioSampleRate;
	gSynth = new TX81Z();

	std::string err;
	if (!gSynth->load_rom(kRomFile, err))
	{
		fprintf(stderr, "tx81z: ROM %s: %s\n", kRomFile, err.c_str());
		return false;
	}

	bool haveNvram = gSynth->load_nvram(kNvramFile);
	printf("tx81z: battery RAM %s\n", haveNvram ? "loaded" : "missing -> running factory init");

	if (!haveNvram)
	{
		gSynth->set_button(TX81Z::BTN_VOL_LEFT, true);
		gSynth->set_button(TX81Z::BTN_CURSOR, true);
		gInit = INIT_HOLD;
	}
	gSynth->reset();

	for (auto &b : gButtonState) b = 0;
	gAutoPlay = gAutoPlayDefault;
	gAutoNextFrame = secondsToFrames(6.f);

	for (const char *port : kMidiPorts)
	{
		std::unique_ptr<Midi> m(new Midi());
		if (m->readFrom(port) > 0)
		{
			m->enableParser(false);              // raw bytes go straight to the SCI
			printf("tx81z: MIDI in %s\n", port);
			gMidi.push_back(std::move(m));
		}
	}

	gOscSender.setup(kOledPort, kOledHost);
	gOscReceiver.setup(kOscInPort, onOsc);
	gLcdTask = Bela_createAuxiliaryTask(sendLcd, 50, "tx81z-lcd", nullptr);
	gSaveTask = Bela_createAuxiliaryTask(saveNvram, 10, "tx81z-save", nullptr);

	gStep = double(gSynth->sample_rate()) / context->audioSampleRate;
	std::memset(gHl, 0, sizeof(gHl));
	std::memset(gHr, 0, sizeof(gHr));
	std::memset(gLcdSent, 0, sizeof(gLcdSent));
	printf("tx81z: native rate %u Hz -> %.0f Hz, auto play %s\n",
		gSynth->sample_rate(), context->audioSampleRate, gAutoPlay ? "on" : "off");
	return true;
}

void render(BelaContext *context, void *)
{
	// ---- MIDI in (raw bytes) ----
	for (auto &m : gMidi)
	{
		int byte;
		while ((byte = m->getInput()) >= 0)
		{
			uint8_t b = uint8_t(byte);
			// real MIDI playing (anything but clock / active sensing) stops auto play
			if (gAutoPlay && b >= 0x80 && b < 0xF8)
			{
				gAutoPlay = false;
				autoPlayStop();
			}
			gSynth->midi_in(&b, 1);
		}
	}

	// ---- OSC button requests ----
	int req = gPressRequest.exchange(-1);
	if (req >= 0) pressButton(TX81Z::Button(req), 0.15f);
	if (gInit == INIT_NONE)
		for (int i = 0; i < TX81Z::BTN_COUNT; i++)
			if (i != gPressedButton)
				gSynth->set_button(TX81Z::Button(i), gButtonState[i] != 0);
	if (!gAutoPlay && gAutoNote >= 0) autoPlayStop();

	// ---- audio ----
	for (unsigned int n = 0; n < context->audioFrames; n++)
	{
		if (gPressFramesLeft > 0 && --gPressFramesLeft == 0 && gPressedButton >= 0)
		{
			gSynth->set_button(TX81Z::Button(gPressedButton), false);
			gPressedButton = -1;
		}
		factoryInitTick();
		autoPlayTick();

		gPhase += gStep;
		while (gPhase >= 1.0)
		{
			gPhase -= 1.0;
			float l, r;
			gSynth->render(&l, &r, 1);
			gHl[0] = gHl[1]; gHl[1] = gHl[2]; gHl[2] = gHl[3]; gHl[3] = l;
			gHr[0] = gHr[1]; gHr[1] = gHr[2]; gHr[2] = gHr[3]; gHr[3] = r;
		}
		float t = float(gPhase);
		auto herm = [t](const float *y) {
			float c1 = 0.5f * (y[2] - y[0]);
			float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
			float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
			return ((c3 * t + c2) * t + c1) * t + y[1];
		};
		float outL = herm(gHl) * kGain, outR = herm(gHr) * kGain;
		if (context->audioOutChannels >= 2)
		{
			audioWrite(context, n, 0, outL);
			audioWrite(context, n, 1, outR);
		}
		else
			audioWrite(context, n, 0, 0.5f * (outL + outR));
		gFrame++;
	}

	// ---- LCD -> OLED, at most every 40 ms, only on real changes ----
	if (gSynth->lcd_changed()) gLcdDirty = true;
	if (gLcdDirty && !gLcdBusy && gFrame - gLastLcdFrame >= secondsToFrames(0.04f))
	{
		char l0[17], l1[17];
		gSynth->lcd_line_chars(0, l0);
		gSynth->lcd_line_chars(1, l1);
		gLcdDirty = false;
		if (std::strcmp(l0, gLcdSent[0]) || std::strcmp(l1, gLcdSent[1]))
		{
			std::memcpy(gLcdPending[0], l0, 17);
			std::memcpy(gLcdPending[1], l1, 17);
			std::memcpy(gLcdSent[0], l0, 17);
			std::memcpy(gLcdSent[1], l1, 17);
			gLastLcdFrame = gFrame;
			gLcdBusy = true;
			Bela_scheduleAuxiliaryTask(gLcdTask);
		}
	}

	if (gSaveRequest.exchange(false))
		Bela_scheduleAuxiliaryTask(gSaveTask);
}

void cleanup(BelaContext *, void *)
{
	if (gSynth)
	{
		if (gSynth->save_nvram(kNvramFile)) printf("tx81z: battery RAM saved\n");
		delete gSynth;
		gSynth = nullptr;
	}
}
