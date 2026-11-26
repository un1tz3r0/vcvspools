#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "voice.hpp"

using namespace spools;

// 4 slices of 0.25 s each at 48 kHz (1 beat at 240 BPM). Each slice holds a constant level 0.1*(i+1).
static std::shared_ptr<Loop> steps(int channels = 1) {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = channels;
	a->frames = 48000;
	for (int c = 0; c < channels; c++)
		for (size_t i = 0; i < 48000; i++) a->data[c].push_back(0.1f * float(i / 12000 + 1));
	return sliceEvenly(*makeLoop(a, {0, 48000}, 240, true, {}), 4);
}

struct Run { std::vector<float> l, r; std::vector<int> trigs; int eocs = 0; };

// beatPeriod in seconds; trigger at sample 0.
static Run render(Voice& v, const Loop& loop, PhraseParams p, VoiceParams vp, double beatPeriod, size_t n, int semis = 0, size_t gateLen = 10) {
	Run out;
	for (size_t i = 0; i < n; i++) {
		bool g = i < gateLen;
		VoiceOut o = v.process(i / 48000.0 / beatPeriod, beatPeriod, g, i == 0, semis, loop, p, vp, 48000);
		out.l.push_back(o.left);
		out.r.push_back(o.right);
		if (o.trig) out.trigs.push_back(int(i));
		out.eocs += o.eoc;
	}
	return out;
}

TEST_CASE("slices start on schedule and play their own audio") {
	auto loop = steps();
	Voice v;
	PhraseParams p;
	p.length = 4;
	auto r = render(v, *loop, p, {}, 0.25, 48000 + 2000);
	REQUIRE(r.trigs.size() == 4);
	for (int i = 0; i < 4; i++) {
		CHECK(r.trigs[i] == doctest::Approx(12000 * i).epsilon(0.0001));
		CHECK(r.l[size_t(12000 * i + 6000)] == doctest::Approx(0.1f * (i + 1)).epsilon(0.01));
	}
	CHECK(r.eocs == 1);
	CHECK(std::fabs(r.l.back()) < 1e-6f);
}

TEST_CASE("faster clock: slices are cut short and overlap briefly; slower clock leaves gaps in Cut mode") {
	auto loop = steps();
	PhraseParams p;
	p.length = 2;
	Voice v;
	auto fast = render(v, *loop, p, {}, 0.125, 20000);  // twice the tempo
	REQUIRE(fast.trigs.size() == 2);
	CHECK(fast.trigs[1] == 6000);
	CHECK(fast.l[9000] == doctest::Approx(0.2f).epsilon(0.01));  // second slice, first one gone

	VoiceParams cut;
	cut.tail = TailMode::Cut;
	cut.duration = 0.5f;
	Voice w;
	auto gated = render(w, *loop, p, cut, 0.25, 30000);
	CHECK(gated.l[3000] == doctest::Approx(0.1f).epsilon(0.01));
	CHECK(std::fabs(gated.l[7000]) < 1e-6f);  // gated off at half the slot
	CHECK(gated.l[15000] == doctest::Approx(0.2f).epsilon(0.01));
}

TEST_CASE("pitch changes rate, so a slice ends sooner when pitched up") {
	auto loop = steps();
	PhraseParams p;
	VoiceParams vp;
	vp.pitch = 12;
	Voice v;
	auto r = render(v, *loop, p, vp, 1.0, 12000);  // long slot; slice audio lasts 6000 samples at 2x
	CHECK(r.l[3000] == doctest::Approx(0.1f).epsilon(0.01));
	CHECK(std::fabs(r.l[6100]) < 1e-6f);
}

TEST_CASE("per-slice params add to globals: gain, pan, reverse") {
	auto loop = std::make_shared<Loop>(*steps());
	loop->slices[1].p.gain = -6.0206f;
	loop->slices[1].p.pan = 1.f;
	PhraseParams p;
	p.startSlice = 1;
	Voice v;
	auto r = render(v, *loop, p, {}, 0.25, 12000);
	CHECK(r.l[6000] == doctest::Approx(0.f).epsilon(1e-4));
	CHECK(r.r[6000] == doctest::Approx(0.2f * 0.5f * 1.41421f).epsilon(0.01));

	VoiceParams vp;
	vp.reverse = true;
	loop->slices[1].p.reverse = true;  // reverse XOR reverse = forward
	Voice w;
	auto f = render(w, *loop, p, vp, 0.25, 12000);
	CHECK(f.r[6000] == doctest::Approx(r.r[6000]).epsilon(0.01));
}

TEST_CASE("stereo balance and semitone offset") {
	auto loop = steps(2);
	PhraseParams p;
	VoiceParams vp;
	vp.pan = -0.5f;
	Voice v;
	auto r = render(v, *loop, p, vp, 0.25, 12000, 2);
	CHECK(r.trigs.size() == 1);
	CHECK(r.l[6000] == doctest::Approx(0.3f).epsilon(0.01));
	CHECK(r.r[6000] == doctest::Approx(0.15f).epsilon(0.01));
}

TEST_CASE("replacing the loop drops players that point at the old audio") {
	auto a = steps(), b = steps();
	PhraseParams p;
	p.length = 4;
	Voice v;
	for (int i = 0; i < 1000; i++) v.process(i / 12000.0, 0.25, i < 10, i == 0, 0, *a, p, {}, 48000);
	VoiceOut o = v.process(1000 / 12000.0, 0.25, false, false, 0, *b, p, {}, 48000);
	CHECK_FALSE(o.playing);
}

TEST_CASE("the voice filter follows global and per-slice cutoff") {
	auto loop = steps();
	auto l = std::make_shared<Loop>(*loop);
	auto noise = std::make_shared<Audio>(*l->audio);
	uint32_t seed = 1;
	for (auto& x : noise->data[0]) x = float((seed = seed * 1664525u + 1013904223u) >> 8) / 16777216.f - 0.5f;
	l->audio = noise;
	l->slices[1].p.cutoff = -4;  // a dull second slice
	PhraseParams p;
	p.length = 2;
	auto energy = [](const Run& r, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; i++) e += r.l[i] * r.l[i]; return e; };
	Voice v0, v1;
	VoiceParams open, lp;
	lp.filter = FilterType::LP12;
	lp.cutoff = 3;  // ~2.1 kHz
	auto dry = render(v0, *l, p, open, 0.25, 24000);
	auto wet = render(v1, *l, p, lp, 0.25, 24000);
	double d0 = energy(dry, 1000, 11000), w0 = energy(wet, 1000, 11000);
	double d1 = energy(dry, 13000, 23000), w1 = energy(wet, 13000, 23000);
	CHECK(w0 / d0 < 0.2);
	CHECK(w0 / d0 > 0.02);
	CHECK(w1 / d1 < w0 / d0 * 0.2);
}

TEST_CASE("amp envelope shapes each slice, or the whole note when retriggering per note") {
	auto loop = steps();
	PhraseParams p;
	p.length = 2;
	VoiceParams vp;
	vp.ampEnv = {0.05f, 0.5f, 1.f, 0.f};
	Voice v;
	auto perSlice = render(v, *loop, p, vp, 0.25, 24000);
	CHECK(perSlice.l[600] < 0.5f * 0.1f);                                 // early in the first attack
	CHECK(perSlice.l[12600] < 0.5f * 0.2f);                               // second slice attacks again
	CHECK(perSlice.l[11000] == doctest::Approx(0.1f).epsilon(0.01));
	vp.retriggerPerNote = true;
	Voice w;
	auto perNote = render(w, *loop, p, vp, 0.25, 24000);
	CHECK(perNote.l[600] == doctest::Approx(perSlice.l[600]));
	CHECK(perNote.l[12600] == doctest::Approx(0.2f).epsilon(0.01));      // no new attack
}

TEST_CASE("a long amp release lets the slice ring past the note's end; the filter envelope moves the cutoff") {
	auto loop = steps();
	PhraseParams p;
	p.length = 1;
	VoiceParams vp;
	vp.tail = TailMode::Cut;
	vp.duration = 0.5f;          // gated off at 6000 samples
	vp.ampEnv.r = 0.05f;
	Voice v;
	auto r = render(v, *loop, p, vp, 0.25, 12000);
	CHECK(r.l[5990] == doctest::Approx(0.1f).epsilon(0.01));
	CHECK(r.l[6000 + 1200] > 0.002f);  // still releasing 25 ms later (-30 dB)
	CHECK(r.l[6000 + 1200] < 0.005f);
	CHECK(r.l[6000 + 2500] == 0.f);    // -60 dB reached, player gone

	Voice f;
	VoiceParams fp;
	fp.filterEnvAmount = 5;
	fp.filterEnv = {0.f, 0.1f, 0.f, 0.1f};
	float e0 = 0, e1 = 0;
	for (int i = 0; i < 9600; i++) {
		VoiceOut o = f.process(i / 48000.0 / 0.25, 0.25, i < 10, i == 0, 0, *loop, p, fp, 48000);
		if (i == 1) e0 = o.filterEnv;
		if (i == 9599) e1 = o.filterEnv;
	}
	CHECK(e0 == doctest::Approx(1).epsilon(0.01));
	CHECK(e1 < 0.01f);
}
