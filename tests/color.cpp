#include <doctest/doctest.h>
#include <set>
#include "color.hpp"

using namespace spools;

TEST_CASE("OKLCH converts to sRGB") {
	Rgb w = oklch(1, 0, 0);
	CHECK(w.r == doctest::Approx(1).epsilon(1e-3));
	CHECK(w.g == doctest::Approx(1).epsilon(1e-3));
	CHECK(w.b == doctest::Approx(1).epsilon(1e-3));
	Rgb red = oklch(0.62796f, 0.25768f, 29.2339f / 360);  // sRGB #ff0000
	CHECK(red.r == doctest::Approx(1).epsilon(1e-2));
	CHECK(red.g == doctest::Approx(0).epsilon(1e-2));
	CHECK(red.b == doctest::Approx(0).epsilon(1e-2));
	Rgb grey = oklch(0.5f, 0, 0.3f);
	CHECK(grey.r == doctest::Approx(grey.b));
}

TEST_CASE("slice hues are distinct and spread over the circle") {
	for (size_t n : {1u, 2u, 7u, 16u, 64u}) {
		std::set<float> hues;
		for (size_t i = 0; i < n; i++) {
			float h = sliceHue(i, n);
			CHECK(h >= 0);
			CHECK(h < 1);
			hues.insert(h);
		}
		CHECK(hues.size() == n);
		if (n > 2) CHECK(std::fabs(sliceHue(0, n) - sliceHue(1, n)) > 0.3f);
	}
}
