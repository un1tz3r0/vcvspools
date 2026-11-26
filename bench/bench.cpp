// make bench: CPU cost of 16 voices in the main playback modes, as % of one core in real time at 48 kHz.
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include "voice.hpp"

using namespace spools;

// A 2 s stereo loop of 16 decaying tonal hits, sliced evenly and analysed, like a typical break.
static std::shared_ptr<Loop> testLoop() {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 44100;
	a->channels = 2;
	a->frames = 88200;
	std::mt19937 rng(1);
	std::uniform_real_distribution<float> u(-1, 1);
	for (int c = 0; c < 2; c++)
		for (size_t i = 0; i < a->frames; i++) {
			double t = double(i % 5512) / 44100;
			a->data[c].push_back(float(std::exp(-t * 12) * (0.5 * std::sin(2 * M_PI * 110 * (1 + i / 5512) * t) + 0.2 * u(rng))));
		}
	auto l = std::make_shared<Loop>(*sliceEvenly(*makeLoop(a, {0, a->frames}, 120, true, {}), 16));
	l->features = std::make_shared<const std::vector<SliceFeatures>>(analyzeSlices(*l));
	return l;
}

static double run(const Loop& l, VoiceParams vp, int voices, bool gated, double seconds = 4) {
	const float sr = 48000;
	std::vector<Voice> v(static_cast<size_t>(voices));
	PhraseParams p;
	p.length = 16;
	const double period = 60.0 / 137;  // a tempo away from the loop's, so every slice is stretched or cut
	const size_t n = size_t(seconds * sr);
	volatile float sink = 0;
	auto t0 = std::chrono::steady_clock::now();
	for (size_t i = 0; i < n; i++) {
		const double beat = double(i) / sr / period;
		for (int c = 0; c < voices; c++) {
			VoiceParams vc = vp;
			vc.pitch = float(c % 5) * 0.37f;  // fractional rates: the general interpolation path
			VoiceOut o = v[size_t(c)].process(beat, period, gated, gated && i == 0, c % 12, l, p, vc, sr);
			sink = sink + o.left + o.right;
		}
	}
	const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	(void)sink;
	return 100 * used / seconds;
}

int main(int argc, char** argv) {
	const double seconds = argc > 1 ? std::atof(argv[1]) : 4;
	auto l = testLoop();
	struct Case { const char* name; TailMode tail; const SincTable* q; FilterType f; bool gated; };
	const Case cases[] = {
		{"Ring, best", TailMode::Ring, &SincTable::hq(), FilterType::Off, true},
		{"Ring, eco", TailMode::Ring, &SincTable::eco(), FilterType::Off, true},
		{"Ring, best, LP24", TailMode::Ring, &SincTable::hq(), FilterType::LP24, true},
		{"Auto-loop, best", TailMode::AutoLoop, &SincTable::hq(), FilterType::Off, true},
		{"Stretch, best", TailMode::Stretch, &SincTable::hq(), FilterType::Off, true},
		{"Stretch, eco", TailMode::Stretch, &SincTable::eco(), FilterType::Off, true},
		{"Idle (no gate)", TailMode::Ring, &SincTable::hq(), FilterType::LP24, false},
	};
	std::printf("16 voices at 48 kHz, %% of one core:\n");
	for (const Case& c : cases) {
		if (argc > 2 && !std::strstr(c.name, argv[2])) continue;
		VoiceParams vp;
		vp.tail = c.tail;
		vp.quality = c.q;
		vp.filter = c.f;
		vp.cutoff = 4;
		double best = 1e9;  // best of three: a shared machine only ever adds time
		for (int k = 0; k < 3; k++) best = std::min(best, run(*l, vp, 16, c.gated, seconds));
		std::printf("  %-20s %6.2f %%  %5.1f ns per voice-sample\n", c.name, best, best / 100 / (16 * 48000) * 1e9);
	}
}
