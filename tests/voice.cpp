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

TEST_CASE("volume and pitch changes during a slice glide instead of stepping") {
	auto loop = steps();
	PhraseParams p;
	p.length = 1;
	VoiceParams vp;
	Voice v;
	float prev = 0, maxStep = 0, at5ms = 0;
	for (int i = 0; i < 9000; i++) {
		if (i == 3000) vp.gain = -20;
		VoiceOut o = v.process(i / 48000.0 / 0.25, 0.25, i < 10, i == 0, 0, *loop, p, vp, 48000);
		if (i > 3000) maxStep = std::max(maxStep, std::fabs(o.left - prev));
		if (i == 3000 + 32 + 240) at5ms = o.left;
		prev = o.left;
	}
	CHECK(prev == doctest::Approx(0.01f).epsilon(0.01));  // settled at -20 dB
	CHECK(maxStep < 0.003f);                               // no zipper or jump
	CHECK(at5ms > 0.02f);                                  // still gliding a few ms in
	CHECK(at5ms < 0.07f);
}

TEST_CASE("tail modes when the slot and the audio differ in length") {
	auto loop = steps();
	PhraseParams p;
	p.length = 1;
	// Slot longer than the audio (slow clock): both modes play the slice once, then silence, with no click.
	for (TailMode t : {TailMode::Cut, TailMode::Ring}) {
		VoiceParams vp;
		vp.tail = t;
		Voice v;
		auto r = render(v, *loop, p, vp, 0.5, 30000);  // slot 24000, audio 12000
		CHECK(r.l[11000] == doctest::Approx(0.1f).epsilon(0.01));
		CHECK(r.l[12100] == 0.f);
		float maxStep = 0;
		for (size_t i = 11000; i < 12100; i++) maxStep = std::max(maxStep, std::fabs(r.l[i + 1] - r.l[i]));
		CHECK(maxStep < 0.01f);
	}
	// Slot shorter than the audio (fast clock): both fade at the slot end over the overlap time (DESIGN §6.5).
	VoiceParams cut, ring;
	cut.tail = TailMode::Cut;
	cut.overlap = 0.01f;
	ring.tail = TailMode::Ring;
	Voice vc, vr;
	auto rc = render(vc, *loop, p, cut, 0.125, 14000);  // slot 6000
	auto rr = render(vr, *loop, p, ring, 0.125, 14000);
	CHECK(rc.l[5900] == doctest::Approx(0.1f).epsilon(0.01));
	CHECK(rc.l[6240] > 0.01f);   // halfway through the 10 ms fade
	CHECK(rc.l[6240] < 0.09f);
	CHECK(rc.l[6600] == 0.f);
	CHECK(rr.l[5900] == doctest::Approx(0.1f).epsilon(0.01));
	CHECK(rr.l[6600] == 0.f);

	// A slice's own tail mode wins over the global one.
	auto l = std::make_shared<Loop>(*loop);
	l->slices[0].p.tail = TailMode::Cut;
	ring.duration = 0.5f;
	Voice vs;
	auto rs = render(vs, *l, p, ring, 0.25, 14000);
	CHECK(rs.l[7000] == 0.f);
}

TEST_CASE("Loop tail sustains a slice through a long slot, from its own loop points when set") {
	auto loop = steps();
	PhraseParams p;
	p.length = 1;
	VoiceParams vp;
	vp.tail = TailMode::Loop;
	Voice v;
	auto r = render(v, *loop, p, vp, 0.5, 30000);  // slot 24000, audio 12000
	CHECK(r.l[11000] == doctest::Approx(0.1f).epsilon(0.01));
	CHECK(r.l[20000] == doctest::Approx(0.1f).epsilon(0.01));  // still looping past the audio end
	CHECK(r.l[25000] == 0.f);                                  // gated at the slot end

	auto l = std::make_shared<Loop>(*loop);
	l->slices[0].p.loopStart = 0.1f;
	l->slices[0].p.loopEnd = 0.2f;
	l->slices[0].p.pingPong = true;
	auto lp = loopPoints(*l, 0, SlicePlayer::span(*l->audio, 0, 12000, 0, 0, false), vp);
	CHECK(lp.start == doctest::Approx(4800));
	CHECK(lp.end == doctest::Approx(9600));
	CHECK(lp.pingPong);
	auto a = loopPoints(*loop, 0, SlicePlayer::span(*loop->audio, 0, 12000, 0, 0, false), vp);
	CHECK(a.start == 3000);  // no features yet: a quarter in
	CHECK(a.end == 12000);
}

// One 0.25 s slice: a loud 10 ms attack settling into a 220 Hz tone at 0.15, analysed.
static std::shared_ptr<Loop> pluck() {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = 12000;
	for (size_t i = 0; i < a->frames; i++) {
		double t = i / 48000.0;
		a->data[0].push_back(float(0.15 * std::sin(2 * M_PI * 220 * t) * (t < 0.01 ? 4 - 3 * t / 0.01 : 1)));
	}
	auto l = std::make_shared<Loop>(*makeLoop(a, {0, 12000}, 240, true, {}));
	l->features = std::make_shared<const std::vector<SliceFeatures>>(analyzeSlices(*l));
	return l;
}

TEST_CASE("Auto-loop tail sustains a pitched slice seamlessly on its searched loop") {
	auto l = pluck();
	REQUIRE((*l->features)[0].loop.score > 0.9f);
	PhraseParams p;
	p.length = 1;
	VoiceParams vp;
	vp.tail = TailMode::AutoLoop;
	Voice v;
	auto r = render(v, *l, p, vp, 0.5, 30000);  // slot 24000, audio 12000
	float maxStep = 0, peak = 0;
	for (size_t i = 2000; i < 23000; i++) {
		maxStep = std::max(maxStep, std::fabs(r.l[i + 1] - r.l[i]));
		if (i > 14000) peak = std::max(peak, std::fabs(r.l[i]));
	}
	CHECK(maxStep < 0.006f);  // a 220 Hz sine at 0.15 moves at most 0.0043 per sample
	CHECK(peak == doctest::Approx(0.15f).epsilon(0.03));
	CHECK(r.l[25000] == 0.f);
}

TEST_CASE("Stretch tail fills the slot, keeping the attack's timing and the pitch") {
	auto l = pluck();
	PhraseParams p;
	p.length = 1;
	VoiceParams vp;
	vp.tail = TailMode::Stretch;
	for (double beat : {0.5, 0.15}) {  // slot 24000 (stretch) and 7200 (squeeze); audio 12000
		Voice v;
		const size_t slot = size_t(48000 * beat);
		auto r = render(v, *l, p, vp, beat, slot + 4000);
		CHECK(std::fabs(r.l[200]) > 0.3f * std::fabs(l->audio->data[0][200]));  // attack plays as recorded
		float maxStep = 0, peak = 0;
		for (size_t i = 1000; i < slot - 600; i++) maxStep = std::max(maxStep, std::fabs(r.l[i + 1] - r.l[i]));
		for (size_t i = slot - 1200; i < slot - 600; i++) peak = std::max(peak, std::fabs(r.l[i]));
		CHECK(maxStep < 0.03f);
		CHECK(peak == doctest::Approx(0.15f).epsilon(0.05));  // still sounding just before the slot ends
		CHECK(r.l[slot + 1000] == 0.f);
	}
	auto plan = stretchPlan(*l, 0, SlicePlayer::span(*l->audio, 0, 12000, 0, 0, false), vp, 24000, 1);
	CHECK(plan.period == doctest::Approx(48000 / 220.0).epsilon(0.01));
	CHECK(plan.from == doctest::Approx(double((*l->features)[0].transientEnd)));
	vp.stretchTail = 0.5f;
	vp.transientPreserve = 0;
	auto half = stretchPlan(*l, 0, SlicePlayer::span(*l->audio, 0, 12000, 0, 0, false), vp, 24000, 1);
	CHECK(half.from == doctest::Approx(6000));  // only the second half of the slice is stretched
	CHECK(half.outSamples == doctest::Approx(18000));
}

TEST_CASE("sends carry the voice's mono mix at each slice's own level") {
	auto l = std::make_shared<Loop>(*steps(2));
	l->slices[0].p.sendA = 0.5f;
	l->slices[1].p.sendB = 1;
	PhraseParams p;
	p.length = 2;
	Voice v;
	std::vector<VoiceOut> o;
	for (size_t i = 0; i < 24000; i++) o.push_back(v.process(i / 48000.0 / 0.25, 0.25, i < 10, i == 0, 0, *l, p, {}, 48000));
	CHECK(o[6000].sendA == doctest::Approx(0.05f).epsilon(0.02));
	CHECK(o[6000].sendB == 0.f);
	CHECK(o[18000].sendA == doctest::Approx(0.f).epsilon(0.001));
	CHECK(o[18000].sendB == doctest::Approx(0.2f).epsilon(0.02));
}
