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
