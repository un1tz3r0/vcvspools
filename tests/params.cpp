#include <doctest/doctest.h>
#include "voice.hpp"

using namespace spools;

TEST_CASE("resolve sums global and per-slice values and clamps them") {
	VoiceParams g;
	g.pitch = 40;
	g.gain = -6;
	g.pan = 0.8f;
	g.cutoff = 7;
	g.res = 0.5f;
	g.overlap = 0.01f;
	g.tail = TailMode::Cut;
	SliceParams s;
	s.pitch = 12;
	s.gain = 3;
	s.pan = 0.5f;
	s.cutoff = -3;
	s.res = 0.7f;
	s.xfade = 0.02f;
	s.reverse = true;
	Resolved r = resolve(g, s);
	CHECK(r.pitch == 48);
	CHECK(r.gain == -3);
	CHECK(r.pan == 1);
	CHECK(r.cutoff == 4);
	CHECK(r.res == 1);
	CHECK(r.fade == doctest::Approx(0.03f));
	CHECK(r.reverse);
	CHECK(r.tail == TailMode::Cut);
	s.tail = TailMode::Ring;
	CHECK(resolve(g, s).tail == TailMode::Ring);
}

TEST_CASE("Smoother glides with the set time constant") {
	Smoother sm;
	sm.snap(0);
	sm.target = 1;
	const float c = Smoother::coef(0.005f, 4.f / 48000);
	for (int i = 0; i < 60; i++) sm.step(c);  // 5 ms
	CHECK(sm.value == doctest::Approx(1 - std::exp(-1.f)).epsilon(0.01));
}
