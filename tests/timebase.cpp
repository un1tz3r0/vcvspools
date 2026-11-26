#include <doctest/doctest.h>
#include "timebase.hpp"

using namespace spools;

static const double sr = 48000, dt = 1 / sr;

// Runs `seconds` with pulses every `interval` seconds (first pulse at t=0).
static void pulses(TimeBase& tb, double interval, double seconds, double jitter = 0) {
	long n = long(seconds * sr), next = 0, k = 0;
	for (long i = 0; i < n; i++) {
		bool edge = i == next;
		if (edge) next = long(std::llround((++k) * interval * sr + ((k % 2) ? jitter : -jitter) * sr));
		tb.process(true, edge, dt);
	}
}

TEST_CASE("free-runs at the fallback tempo without a clock") {
	TimeBase tb;
	tb.setFallbackBpm(90);
	for (int i = 0; i < 48000; i++) tb.process(false, false, dt);
	CHECK(tb.bpm() == doctest::Approx(90));
	CHECK(tb.phase() == doctest::Approx(1.5));
	CHECK_FALSE(tb.clocked());
}

TEST_CASE("measures tempo from pulses at several PPQN") {
	for (int ppqn : {1, 4, 24}) {
		CAPTURE(ppqn);
		TimeBase tb;
		tb.setPpqn(ppqn);
		pulses(tb, 60.0 / 140 / ppqn, 2.0);
		CHECK(tb.clocked());
		CHECK(tb.bpm() == doctest::Approx(140).epsilon(0.002));
	}
}

TEST_CASE("phase locks to pulses and never overshoots a late pulse") {
	TimeBase tb;
	tb.setPpqn(4);
	pulses(tb, 0.125, 1.0 + 1.5 * dt);  // 120 BPM, 9 pulses at 0..1 s
	CHECK(tb.phase() == doctest::Approx(2.0).epsilon(1e-9));
	for (int i = 0; i < 48000; i++) tb.process(true, false, dt);  // clock stops
	CHECK(tb.phase() < 2.25);
	CHECK(tb.phase() > 2.24);
	tb.process(true, true, dt);
	CHECK(tb.phase() == doctest::Approx(2.25));
}

TEST_CASE("median rejects jitter, big changes are followed at once") {
	TimeBase tb;
	pulses(tb, 0.5, 4.0, 0.004);
	CHECK(tb.bpm() == doctest::Approx(120).epsilon(0.02));
	pulses(tb, 0.25, 0.6);
	CHECK(tb.bpm() == doctest::Approx(240).epsilon(0.01));
}

TEST_CASE("unplugging the clock falls back") {
	TimeBase tb;
	tb.setFallbackBpm(100);
	pulses(tb, 0.25, 1.1);
	CHECK(tb.bpm() == doctest::Approx(240).epsilon(0.01));
	tb.process(false, false, dt);
	CHECK(tb.bpm() == doctest::Approx(100));
	CHECK_FALSE(tb.clocked());
}
