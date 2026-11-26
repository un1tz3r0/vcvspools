#include <doctest/doctest.h>
#include "units.hpp"

using namespace spools;

TEST_CASE("unit conversions") {
	CHECK(dbToGain(0.f) == doctest::Approx(1.f));
	CHECK(dbToGain(-6.0206f) == doctest::Approx(0.5f).epsilon(1e-4));
	CHECK(semitonesToRatio(12.f) == doctest::Approx(2.f));
	CHECK(semitonesToRatio(-24.f) == doctest::Approx(0.25f));
	for (int st = -60; st <= 60; st++)
		CHECK(voltsToSemitones(semitonesToVolts(st)) == st);
}

TEST_CASE("wrap is euclidean") {
	CHECK(wrap(0, 4) == 0);
	CHECK(wrap(5, 4) == 1);
	CHECK(wrap(-1, 4) == 3);
	CHECK(wrap(-9, 4) == 3);
	CHECK(wrap(3, 0) == 0);
}
