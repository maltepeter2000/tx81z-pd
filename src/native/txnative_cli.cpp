// txnative-cli: run the native engine on a little script and log its register writes / audio,
// for comparison with the firmware (tx81z-emu --opzlog).
//
//   txnative-cli --rom ROM --voice VOICE.bin [--at S] [--note S:on|off:KEY[:VEL]]... [--seconds S]
//                [--opzlog FILE] [--wav FILE]      (WAV at 48 kHz, resampled like tx81z-emu)
// VOICE.bin = 93 VCED bytes followed by 23 ACED bytes.
#include "txnative.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Ev { double t; int kind; int key, vel; };   // kind 0 = load, 1 = on, 2 = off

std::vector<uint8_t> read_file(const std::string &p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_wav(const std::string &path, const std::vector<int16_t> &pcm, int rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) return;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	uint32_t bytes = uint32_t(pcm.size() * 2);
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	std::fwrite(pcm.data(), 2, pcm.size(), f);
	std::fclose(f);
}

} // namespace

int main(int argc, char **argv)
{
	std::string rom, voice, opzlog, wav;
	double seconds = 2.0, load_at = 0.0;
	std::vector<Ev> evs;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto next = [&]() -> std::string { if (i + 1 >= argc) std::exit(1); return argv[++i]; };
		if (a == "--rom") rom = next();
		else if (a == "--voice") voice = next();
		else if (a == "--at") load_at = std::atof(next().c_str());
		else if (a == "--seconds") seconds = std::atof(next().c_str());
		else if (a == "--opzlog") opzlog = next();
		else if (a == "--wav") wav = next();
		else if (a == "--note")
		{
			std::string s = next();
			double t; char kind[8] = {}; int key = 60, vel = 100;
			if (std::sscanf(s.c_str(), "%lf:%3[onf]:%d:%d", &t, kind, &key, &vel) < 3) { std::fprintf(stderr, "bad --note %s\n", s.c_str()); return 1; }
			evs.push_back({ t, std::string(kind) == "on" ? 1 : 2, key, vel });
		}
		else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
	}
	auto romdata = read_file(rom);
	auto vdata = read_file(voice);
	if (vdata.size() != 93 + 23) { std::fprintf(stderr, "voice file must be 116 bytes\n"); return 1; }
	evs.insert(evs.begin(), { load_at, 0, 0, 0 });

	txn::Engine eng;
	const double rate = eng.sample_rate();
	uint64_t pos = 0;
	FILE *log = opzlog.empty() ? nullptr : std::fopen(opzlog.c_str(), "w");
	if (log)
	{
		std::fprintf(log, "time_s,reg,val\n");
		eng.on_write = [&](uint8_t r, uint8_t v) { std::fprintf(log, "%.6f,%02x,%02x\n", pos / rate, r, v); };
	}
	std::string err;
	if (!eng.init(romdata.data(), romdata.size(), err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
	std::vector<int16_t> pcm;
	double resample_pos = 0.0, step = rate / 48000.0;   // same linear resampling as tx81z-emu
	float prev_l = 0, prev_r = 0;
	std::vector<bool> done(evs.size(), false);
	const uint64_t total = uint64_t(seconds * rate);
	float l, r;
	for (; pos < total; pos++)
	{
		double t = pos / rate;
		for (size_t k = 0; k < evs.size(); k++)
		{
			if (done[k] || t < evs[k].t) continue;
			done[k] = true;
			if (evs[k].kind == 0) eng.load_voice(vdata.data(), vdata.data() + 93);
			else if (evs[k].kind == 1) eng.note_on(evs[k].key, evs[k].vel);
			else eng.note_off(evs[k].key);
		}
		eng.render(&l, &r, 1);
		if (!wav.empty())
		{
			while (resample_pos < 1.0)
			{
				float fl = prev_l + float(resample_pos) * (l - prev_l);
				float fr = prev_r + float(resample_pos) * (r - prev_r);
				pcm.push_back(int16_t(std::lrint(fl * 32767.0f)));
				pcm.push_back(int16_t(std::lrint(fr * 32767.0f)));
				resample_pos += step;
			}
			resample_pos -= 1.0;
			prev_l = l; prev_r = r;
		}
	}
	if (log) std::fclose(log);
	if (!wav.empty()) write_wav(wav, pcm, 48000);
	return 0;
}
