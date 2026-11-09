// tx81z-emu: command-line runner for the TX81Z emulator.
//
// Examples:
//   tx81z-emu --rom tx81z-v1.6.ic15 --seconds 5
//   tx81z-emu --rom rom.ic15 --hold vol-,cursor --press 2:inc        (test mode)
//   tx81z-emu --rom rom.ic15 --nvram tx81z.nvram --midi "3:90 30 64" --midi "4:80 30 00" --wav out.wav
//   tx81z-emu --rom rom.ic15 --syx 1:patch.syx --opzlog opz.csv

#include "tx81z.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Event
{
	double t;
	enum { PRESS, RELEASE, MIDI } kind;
	TX81Z::Button button;
	std::vector<uint8_t> bytes;
	bool done = false;
};

void usage()
{
	std::fprintf(stderr,
		"usage: tx81z-emu --rom FILE [options]\n"
		"  --rom FILE         64 KB TX81Z firmware image (IC15)\n"
		"  --nvram FILE       battery RAM image (loaded if present, saved on exit)\n"
		"  --seconds S        emulated time to run (default 5)\n"
		"  --wav FILE         write audio (48 kHz, 16-bit stereo)\n"
		"  --hold B[,B...]    hold buttons from power-on until --release-at\n"
		"  --release-at S     release held buttons at S seconds (default 3)\n"
		"  --press S:B        press button B at S seconds for 150 ms\n"
		"  --midi S:HEX       send MIDI bytes at S seconds, e.g. \"2:90 3C 64\"\n"
		"  --syx S:FILE       send the bytes of FILE at S seconds\n"
		"  --opzlog FILE      log YM2414 register writes as CSV (time_s,reg,val)\n"
		"  --quiet            do not print LCD changes\n"
		"buttons: store utility edit play param- param+ dec inc vol- vol+ cursor\n");
}

bool parse_time_arg(const std::string &arg, double &t, std::string &rest)
{
	auto p = arg.find(':');
	if (p == std::string::npos) return false;
	t = std::atof(arg.substr(0, p).c_str());
	rest = arg.substr(p + 1);
	return true;
}

std::vector<uint8_t> parse_hex(const std::string &s)
{
	std::vector<uint8_t> out;
	std::istringstream in(s);
	std::string tok;
	while (in >> tok)
		out.push_back(uint8_t(std::strtoul(tok.c_str(), nullptr, 16)));
	return out;
}

void write_wav(const std::string &path, const std::vector<int16_t> &pcm, int rate)
{
	std::ofstream f(path, std::ios::binary);
	auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	uint32_t bytes = uint32_t(pcm.size() * 2);
	f.write("RIFF", 4); w32(36 + bytes); f.write("WAVE", 4);
	f.write("fmt ", 4); w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
	f.write("data", 4); w32(bytes);
	f.write(reinterpret_cast<const char *>(pcm.data()), bytes);
}

} // namespace

int main(int argc, char **argv)
{
	std::string rom, nvram, wav, opzlog;
	double seconds = 5.0, release_at = 3.0;
	bool quiet = false;
	std::vector<TX81Z::Button> held;
	std::vector<Event> events;

	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() -> std::string {
			if (i + 1 >= argc) { usage(); std::exit(1); }
			return argv[++i];
		};
		if (a == "--rom") rom = next();
		else if (a == "--nvram") nvram = next();
		else if (a == "--seconds") seconds = std::atof(next().c_str());
		else if (a == "--wav") wav = next();
		else if (a == "--opzlog") opzlog = next();
		else if (a == "--release-at") release_at = std::atof(next().c_str());
		else if (a == "--quiet") quiet = true;
		else if (a == "--hold")
		{
			std::istringstream in(next());
			std::string name;
			while (std::getline(in, name, ','))
			{
				TX81Z::Button b;
				if (!TX81Z::button_from_name(name, b)) { std::fprintf(stderr, "unknown button %s\n", name.c_str()); return 1; }
				held.push_back(b);
			}
		}
		else if (a == "--press" || a == "--midi" || a == "--syx")
		{
			double t; std::string rest;
			if (!parse_time_arg(next(), t, rest)) { usage(); return 1; }
			if (a == "--press")
			{
				TX81Z::Button b;
				if (!TX81Z::button_from_name(rest, b)) { std::fprintf(stderr, "unknown button %s\n", rest.c_str()); return 1; }
				events.push_back({ t, Event::PRESS, b, {} });
				events.push_back({ t + 0.15, Event::RELEASE, b, {} });
			}
			else if (a == "--midi")
				events.push_back({ t, Event::MIDI, TX81Z::BTN_COUNT, parse_hex(rest) });
			else
			{
				std::ifstream f(rest, std::ios::binary);
				if (!f) { std::fprintf(stderr, "cannot open %s\n", rest.c_str()); return 1; }
				std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
				events.push_back({ t, Event::MIDI, TX81Z::BTN_COUNT, data });
			}
		}
		else { usage(); return 1; }
	}
	if (rom.empty()) { usage(); return 1; }

	static TX81Z synth;   // large object; keep off the stack
	std::string err;
	if (!synth.load_rom(rom, err)) { std::fprintf(stderr, "ROM: %s\n", err.c_str()); return 1; }
	if (!nvram.empty() && synth.load_nvram(nvram))
		std::fprintf(stderr, "loaded NVRAM from %s\n", nvram.c_str());

	FILE *log = nullptr;
	if (!opzlog.empty())
	{
		log = std::fopen(opzlog.c_str(), "w");
		std::fprintf(log, "time_s,reg,val\n");
		synth.on_opz_write = [&](uint8_t reg, uint8_t val) {
			std::fprintf(log, "%.6f,%02x,%02x\n", synth.time(), reg, val);
		};
	}
	synth.on_midi_out = [&](uint8_t b) {
		if (!quiet) std::printf("[%8.3f] MIDI OUT %02X\n", synth.time(), b);
	};

	for (auto b : held) synth.set_button(b, true);
	synth.reset();

	const int rate = int(synth.sample_rate());
	const int out_rate = 48000;
	const int block = 64;
	std::vector<float> l(block), r(block);
	std::vector<int16_t> pcm;
	double resample_pos = 0.0, step = double(rate) / out_rate;
	float prev_l = 0, prev_r = 0;
	bool released = held.empty();
	uint8_t last_leds = 0xff;

	const uint64_t total = uint64_t(seconds * rate);
	for (uint64_t done = 0; done < total; done += block)
	{
		double t = synth.time();
		if (!released && t >= release_at)
		{
			for (auto b : held) synth.set_button(b, false);
			released = true;
		}
		for (auto &e : events)
		{
			if (e.done || t < e.t) continue;
			e.done = true;
			if (e.kind == Event::PRESS) synth.set_button(e.button, true);
			else if (e.kind == Event::RELEASE) synth.set_button(e.button, false);
			else synth.midi_in(e.bytes.data(), e.bytes.size());
		}

		synth.render(l.data(), r.data(), block);

		if (!wav.empty())
		{
			// linear resampling to 48 kHz
			for (int i = 0; i < block; i++)
			{
				while (resample_pos < 1.0)
				{
					float fl = prev_l + float(resample_pos) * (l[i] - prev_l);
					float fr = prev_r + float(resample_pos) * (r[i] - prev_r);
					pcm.push_back(int16_t(std::lrint(fl * 32767.0f)));
					pcm.push_back(int16_t(std::lrint(fr * 32767.0f)));
					resample_pos += step;
				}
				resample_pos -= 1.0;
				prev_l = l[i]; prev_r = r[i];
			}
		}

		if (!quiet && synth.lcd_changed())
			std::printf("[%8.3f] LCD |%s|\n           |%s|\n", synth.time(), synth.lcd_line(0).c_str(), synth.lcd_line(1).c_str());
		if (!quiet && synth.leds() != last_leds)
		{
			last_leds = synth.leds();
			std::printf("[%8.3f] LEDs %c%c%c%c\n", synth.time(),
				(last_leds & 1) ? '*' : '.', (last_leds & 2) ? '*' : '.', (last_leds & 4) ? '*' : '.', (last_leds & 8) ? '*' : '.');
		}
	}

	std::printf("final LCD |%s|\n          |%s|\n", synth.lcd_line(0).c_str(), synth.lcd_line(1).c_str());
	std::printf("CPU PC=%04X\n", synth.cpu().pc());

	if (!wav.empty())
	{
		write_wav(wav, pcm, out_rate);
		double peak = 0;
		for (auto s : pcm) peak = std::max(peak, std::abs(double(s)) / 32768.0);
		std::printf("wrote %s (peak %.3f)\n", wav.c_str(), peak);
	}
	if (log) std::fclose(log);
	if (!nvram.empty())
	{
		if (synth.save_nvram(nvram)) std::fprintf(stderr, "saved NVRAM to %s\n", nvram.c_str());
		else std::fprintf(stderr, "could not save NVRAM to %s\n", nvram.c_str());
	}
	return 0;
}
