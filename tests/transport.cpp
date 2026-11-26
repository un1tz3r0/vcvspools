#include <doctest/doctest.h>
#include "transport.hpp"

using namespace spools;

TEST_CASE("RUN starts the loop from zero; unpatched RUN means always running") {
	Transport t;
	Transport::In in;
	auto o = t.process(3.0, in, 4, 0);
	CHECK(o.running);
	CHECK(o.reset);
	CHECK(t.process(3.5, in, 4, 0).loopBeat == doctest::Approx(0.5));
	CHECK_FALSE(t.process(3.6, in, 4, 0).reset);

	in.runConnected = true;
	in.run = false;
	CHECK_FALSE(t.process(4.0, in, 4, 0).running);
	in.run = true;
	o = t.process(6.0, in, 4, 0);
	CHECK(o.reset);
	CHECK(o.loopBeat == 0);
}

TEST_CASE("CONT restarts at once when free-running, or on the next clock edge when clocked") {
	Transport t;
	Transport::In in;
	t.process(0, in, 4, 0);
	in.cont = true;
	auto o = t.process(5.25, in, 4, 0);
	CHECK(o.reset);
	CHECK(o.loopBeat == 0);

	in.clocked = true;
	o = t.process(6.1, in, 4, 0);  // CONT arrives between clock edges
	CHECK_FALSE(o.reset);
	in.cont = false;
	CHECK(t.process(6.2, in, 4, 0).loopBeat == doctest::Approx(0.95));
	in.clockEdge = true;
	o = t.process(6.5, in, 4, 0);
	CHECK(o.reset);
	CHECK(t.process(6.75, in, 4, 0).loopBeat == doctest::Approx(0.25));
}

TEST_CASE("bars per cycle injects CONT at exact bar multiples; BAR realigns only the bar phase") {
	Transport t;
	Transport::In in;
	t.process(1.0, in, 4, 2);
	int resets = 0;
	double last = 0;
	for (int i = 1; i <= 2000; i++) {
		double beat = 1.0 + i * 0.01;
		auto o = t.process(beat, in, 4, 2);
		if (o.reset) { resets++; last = beat; CHECK(o.loopBeat == doctest::Approx(0).epsilon(1e-9)); }
	}
	CHECK(resets == 2);          // at beats 9 and 17 (8-beat cycle from beat 1)
	CHECK(last == doctest::Approx(17));

	in.bar = true;
	auto o = t.process(21.5, in, 4, 2);
	CHECK_FALSE(o.reset);
	CHECK(o.loopBeat == doctest::Approx(4.5));  // slice position kept
	CHECK(o.barBeat == 0);
	in.bar = false;
	CHECK_FALSE(t.process(29.4, in, 4, 2).reset);
	CHECK(t.process(29.5, in, 4, 2).reset);    // next cycle counts from the BAR
}
