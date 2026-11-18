// txnative-cli: run the native engine on a little script and log its register writes / audio,
// for comparison with the firmware (tx81z-emu --opzlog).
//
//   txnative-cli --rom ROM --voice VOICE.bin [--at S] [--note S:on|off:KEY[:VEL]]... [--seconds S]
//                [--midi S:HEX...] [--phase N[b]] [--opzlog FILE] [--wav FILE]      (WAV at 48 kHz, resampled like tx81z-emu)
// VOICE.bin = 93 VCED bytes followed by 23 ACED bytes.
#include "txnative.h"
#include "ymfm_opz.h"   // unmodified ymfm, reference for --check-ymfm

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Ev { double t; int kind; int key, vel; std::vector<uint8_t> midi; };   // kind 0 load, 1 on, 2 off, 3 midi

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
	int phase_next = -1;
	std::string phase_test, phase_out;
	bool check_ymfm = false;
	double dump_t = -1;
	std::string dump_f;
	bool phase_b = true;
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
		else if (a == "--ramdump") { std::string s = next(); auto c = s.find(':'); dump_t = std::atof(s.substr(0, c).c_str()); dump_f = s.substr(c + 1); }
		else if (a == "--check-ymfm") check_ymfm = true;
		else if (a == "--phase-test") { phase_test = next(); phase_out = next(); }
		else if (a == "--phase") { std::string s = next(); phase_next = std::atoi(s.c_str()); phase_b = s.find('b') != std::string::npos; }
		else if (a == "--midi")
		{
			std::string s = next();
			auto c = s.find(':');
			Ev e{ std::atof(s.substr(0, c).c_str()), 3, 0, 0, {} };
			std::istringstream in(s.substr(c + 1));
			std::string h;
			while (in >> h) e.midi.push_back(uint8_t(std::stoul(h, nullptr, 16)));
			evs.push_back(e);
		}
		else if (a == "--note")
		{
			std::string s = next();
			double t; char kind[8] = {}; int key = 60, vel = 100;
			if (std::sscanf(s.c_str(), "%lf:%3[onf]:%d:%d", &t, kind, &key, &vel) < 3) { std::fprintf(stderr, "bad --note %s\n", s.c_str()); return 1; }
			evs.push_back({ t, std::string(kind) == "on" ? 1 : 2, key, vel, {} });
		}
		else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
	}
	auto romdata = read_file(rom);
	if (!phase_test.empty())
	{
		// --phase-test IN OUT: IN = records (u8 phase 'A'/'B', 256 B internal RAM, 8 KB external RAM);
		// OUT = per record: RAM after the phase, u16 number of writes, writes (reg, val)
		txn::Engine eng;
		std::string err;
		if (!eng.init(romdata.data(), romdata.size(), err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
		auto in = read_file(phase_test);
		FILE *f = std::fopen(phase_out.c_str(), "wb");
		std::vector<uint8_t> wr;
		eng.on_write = [&](uint8_t r, uint8_t v) { wr.push_back(r); wr.push_back(v); };
		const size_t rec = 1 + 0x100 + 0x2000;
		for (size_t o = 0; o + rec <= in.size(); o += rec)
		{
			eng.test_set_ram(&in[o + 1], &in[o + 1 + 0x100]);
			wr.clear();
			eng.test_phase(in[o] == 'B');
			for (int x = 0; x < 0x100; x++) std::fputc(eng.peek(uint16_t(x)), f);
			for (int x = 0x6000; x < 0x8000; x++) std::fputc(eng.peek(uint16_t(x)), f);
			uint16_t n = uint16_t(wr.size() / 2);
			std::fwrite(&n, 2, 1, f);
			std::fwrite(wr.data(), 1, wr.size(), f);
		}
		std::fclose(f);
		return 0;
	}
	auto vdata = read_file(voice);
	if (vdata.size() != 93 + 23) { std::fprintf(stderr, "voice file must be 116 bytes\n"); return 1; }
	evs.insert(evs.begin(), { load_at, 0, 0, 0, {} });

	txn::Engine eng;
	const double rate = eng.sample_rate();
	uint64_t pos = 0;
	FILE *log = opzlog.empty() ? nullptr : std::fopen(opzlog.c_str(), "w");
	if (log) std::fprintf(log, "time_s,reg,val\n");
	// --check-ymfm: the unmodified ymfm gets the same register writes; outputs must be equal
	ymfm::ymfm_interface ref_if;
	ymfm::ym2414 ref(ref_if);
	ref.reset();
	uint64_t mismatches = 0, first_mismatch = 0;
	if (log || check_ymfm)
		eng.on_write = [&](uint8_t r, uint8_t v) {
			if (log) std::fprintf(log, "%.6f,%02x,%02x\n", pos / rate, r, v);
			if (check_ymfm) { ref.write(0, r); ref.write(1, v); }
		};
	std::string err;
	if (!eng.init(romdata.data(), romdata.size(), err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
	if (phase_next >= 0) eng.set_phase(phase_next, phase_b);
	std::vector<int16_t> pcm;
	double resample_pos = 0.0, step = rate / 48000.0;   // same linear resampling as tx81z-emu
	float prev_l = 0, prev_r = 0;
	std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.t < b.t; });
	size_t next_ev = 0;
	const uint64_t total = uint64_t(seconds * rate);
	float l, r;
	for (; pos < total; pos++)
	{
		double t = pos / rate;
		for (; next_ev < evs.size() && t >= evs[next_ev].t; next_ev++)
		{
			const size_t k = next_ev;
			if (evs[k].kind == 0) eng.load_voice(vdata.data(), vdata.data() + 93);
			else if (evs[k].kind == 1) eng.note_on(evs[k].key, evs[k].vel);
			else if (evs[k].kind == 2) eng.note_off(evs[k].key);
			else
			{
				auto &m = evs[k].midi;
				int st = m[0] & 0xf0;
				if (st == 0x90 && m.size() >= 3) eng.note_on(m[1], m[2]);
				else if (st == 0x80 && m.size() >= 3) eng.note_off(m[1]);
				else if (st == 0xB0 && m.size() >= 3) eng.control_change(m[1], m[2]);
				else if (st == 0xE0 && m.size() >= 3) eng.pitch_bend(m[1] | m[2] << 7);
			}
		}
		eng.render(&l, &r, 1);
		if (check_ymfm)
		{
			ymfm::ym2414::output_data o;
			ref.generate(&o);
			if (o.data[0] != eng.last_raw(0) || o.data[1] != eng.last_raw(1))
				if (!mismatches++) first_mismatch = pos;
		}
		if (!dump_f.empty() && t >= dump_t)
		{
			// same layout as tx81z-emu --ramdump: $00-$FF, then $6000-$7FFF
			if (FILE *f = std::fopen(dump_f.c_str(), "wb"))
			{
				for (int x = 0; x < 0x100; x++) std::fputc(eng.peek(uint16_t(x)), f);
				for (int x = 0x6000; x < 0x8000; x++) std::fputc(eng.peek(uint16_t(x)), f);
				std::fclose(f);
			}
			dump_f.clear();
		}
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
	if (check_ymfm)
	{
		std::printf("check-ymfm: %llu samples, %llu differ", (unsigned long long)total, (unsigned long long)mismatches);
		if (mismatches) std::printf(" (first at sample %llu)", (unsigned long long)first_mismatch);
		std::printf("\n");
		if (mismatches) return 2;
	}
	if (!wav.empty()) write_wav(wav, pcm, 48000);
	return 0;
}
