#include <doctest/doctest.h>
#include "envelope.hpp"

using namespace spools;

static const float kDt = 1.f / 48000;

static int samplesUntil(Adsr& e, const AdsrTimes& t, Adsr::Stage st, int limit = 1000000) {
	for (int i = 0; i < limit; i++) {
		e.process(t, kDt);
		if (e.state() == st) return i + 1;
	}
	return -1;
}

TEST_CASE("ADSR stages land on their set times") {
	AdsrTimes t{0.01f, 0.1f, 0.5f, 0.2f};
	Adsr e;
	e.trigger();
	CHECK(samplesUntil(e, t, Adsr::Decay) == doctest::Approx(480).epsilon(0.01));
	CHECK(e.value() == 1);
	for (int i = 0; i < 4800; i++) e.process(t, kDt);  // one decay time: within -60 dB of sustain
	CHECK(e.value() == doctest::Approx(0.5).epsilon(0.002));
	samplesUntil(e, t, Adsr::Sustain);
	e.release();
	CHECK(samplesUntil(e, t, Adsr::Idle) == doctest::Approx(0.2 * 48000 * (1 - std::log(2.0) / std::log(1000.0))).epsilon(0.01));
	CHECK(e.value() == 0);
}

TEST_CASE("zero times are instant and the default is a plain gate") {
	AdsrTimes t;  // A 0, S 1, R 0
	Adsr e;
	e.trigger();
	CHECK(e.process(t, kDt) == 1);
	for (int i = 0; i < 100; i++) CHECK(e.process(t, kDt) == 1);
	e.release();
	CHECK(e.process(t, kDt) == 0);
	CHECK(e.idle());
}

TEST_CASE("retrigger restarts the attack from the current level") {
	AdsrTimes t{0.01f, 0.05f, 0.f, 0.05f};
	Adsr e;
	e.trigger();
	for (int i = 0; i < 2400; i++) e.process(t, kDt);
	float mid = e.value();
	CHECK(mid < 0.5f);
	e.trigger();
	float next = e.process(t, kDt);
	CHECK(next > mid);
	CHECK(next < mid + 0.01f);
	e.trigger(true);
	CHECK(e.process(t, kDt) < 0.01f);
}
