#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "pattern.hpp"
#include "voice.hpp"

using namespace spools;

// 4 slices over 4 beats (one second at 240 BPM), each a constant level 0.1*(i+1).
static std::shared_ptr<Loop> steps4() {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = 48000;
	for (size_t i = 0; i < 48000; i++) a->data[0].push_back(0.1f * float(i / 12000 + 1));
	return sliceEvenly(*makeLoop(a, {0, 48000}, 240, true, {}), 4);
}

struct Hit { double beat; SeqEvent e; };

static std::vector<Hit> drive(PatternSequencer& s, const Loop& l, const Pattern& p, double beats, int semis = 0) {
	std::vector<Hit> out;
	for (int i = 0; i <= int(beats * 1000); i++) {
		const double b = i / 1000.0;
		SeqEvent e = s.process(b, true, i == 0, semis, l, p);
		if (e.slice >= 0 || e.release || e.endOfPhrase) out.push_back({b, e});
	}
	return out;
}

TEST_CASE("init from loop: one step per slice on the 1/32 grid, adding up to the loop") {
	auto l = steps4();
	Pattern p = initFromLoop(*l);
	REQUIRE(p.steps.size() == 4);
	for (int i = 0; i < 4; i++) {
		CHECK(p.steps[size_t(i)].slice == i);
		CHECK(p.steps[size_t(i)].ticks == 8);
	}
	auto uneven = std::make_shared<Loop>(*l);
	uneven->slices[1].beat = 1.3;  // 10.4 ticks from the start
	uneven->slices[2].beat = 2.05;
	Pattern q = initFromLoop(*uneven);
	int total = 0;
	for (auto& s : q.steps) total += s.ticks;
	CHECK(total == 32);
	CHECK(q.steps[0].ticks == 10);
}

TEST_CASE("pattern steps play in order with their own lengths, rests, reverse, pitch and duration") {
	auto l = steps4();
	Pattern p;
	p.steps = {{2, 4}, {-1, 4}, {0, 8, 0, 0.5f, true, 3}};
	PatternSequencer s;
	auto h = drive(s, *l, p, 2.4);
	REQUIRE(h.size() == 4);
	CHECK(h[0].beat == 0);
	CHECK(h[0].e.slice == 2);
	CHECK(h[0].e.phraseStart);
	CHECK(h[0].e.sliceBeats == 0.5);
	CHECK(h[1].beat == doctest::Approx(0.5));
	CHECK(h[1].e.release);  // the rest
	CHECK(h[2].beat == doctest::Approx(1.0));
	CHECK(h[2].e.slice == 0);
	CHECK(h[2].e.reverse);
	CHECK(h[2].e.pitch == 3);
	CHECK(h[2].e.duration == 0.5f);
	CHECK(h[3].beat == doctest::Approx(2.0));
	CHECK(h[3].e.endOfPhrase);
	CHECK(h[3].e.slice == 2);
}

TEST_CASE("nudge, direction, V/oct offset, range and loop count") {
	auto l = steps4();
	Pattern p = initFromLoop(*l);  // 4 steps of 1 beat
	p.steps[1].nudge = 0.5f;       // half a tick late
	PatternSequencer s;
	auto h = drive(s, *l, p, 1.9);
	REQUIRE(h.size() == 2);
	CHECK(h[1].beat == doctest::Approx(1.0625).epsilon(0.001));

	p.steps[1].nudge = 0;
	p.direction = Direction::Reverse;
	PatternSequencer r;
	auto hr = drive(r, *l, p, 3.5, 1);
	REQUIRE(hr.size() == 4);
	for (int i = 0; i < 4; i++) CHECK(hr[size_t(i)].e.slice == (3 - i + 1) % 4);  // reversed, shifted up one slice

	p.direction = Direction::Forward;
	p.loopStart = 1;
	p.loopEnd = 2;
	p.loopCount = 1;
	PatternSequencer c;
	auto hc = drive(c, *l, p, 5);
	REQUIRE(hc.size() == 3);
	CHECK(hc[0].e.slice == 1);
	CHECK(hc[1].e.slice == 2);
	CHECK(hc[2].beat == doctest::Approx(2.0));
	CHECK(hc[2].e.release);
	CHECK(hc[2].e.endOfPhrase);
}

TEST_CASE("a step's duration and pitch reach the voice") {
	auto l = steps4();
	VoiceParams vp;
	vp.tail = TailMode::Cut;
	Voice v;
	SeqEvent e;
	e.slice = 0;
	e.sliceBeats = 1;
	e.duration = 0.5f;
	e.pitch = 12;
	std::vector<float> out;
	for (int i = 0; i < 12000; i++) {
		out.push_back(v.play(e, 0.25, *l, vp, 48000).left);
		e = {};
	}
	CHECK(out[2000] == doctest::Approx(0.1f).epsilon(0.02));
	CHECK(out[7000] == 0.f);  // gated at half of the 12000-sample slot
	CHECK(v.player().position() > 5000);  // an octave up reads twice as fast
}
