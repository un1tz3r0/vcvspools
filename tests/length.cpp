#include <doctest/doctest.h>
#include "length.hpp"

using namespace spools;

TEST_CASE("parseLength reads numbers with optional units") {
	auto l = parseLength("1.5s", CropSpec::Samples);
	REQUIRE(l);
	CHECK(l->unit == CropSpec::Seconds);
	CHECK(l->frames(48000, 120) == doctest::Approx(72000));
	CHECK(parseLength("250 ms", CropSpec::Beats)->frames(48000, 120) == doctest::Approx(12000));
	CHECK(parseLength("2 bars", CropSpec::Samples, 3)->value == doctest::Approx(6));
	CHECK(parseLength("4B", CropSpec::Samples)->frames(44100, 120) == doctest::Approx(88200));
	auto d = parseLength(" 1000 ", CropSpec::Samples);
	REQUIRE(d);
	CHECK(d->unit == CropSpec::Samples);
	CHECK(parseLength("2", CropSpec::Beats)->unit == CropSpec::Beats);
	CHECK_FALSE(parseLength("", CropSpec::Samples));
	CHECK_FALSE(parseLength("abc", CropSpec::Samples));
	CHECK_FALSE(parseLength("3 parsecs", CropSpec::Samples));
}

TEST_CASE("formatLength round-trips through parseLength") {
	for (auto u : {CropSpec::Samples, CropSpec::Seconds, CropSpec::Beats}) {
		std::string s = formatLength(66150, u, 44100, 120);
		auto l = parseLength(s, CropSpec::Samples);
		REQUIRE(l);
		CHECK(l->unit == u);
		CHECK(l->frames(44100, 120) == doctest::Approx(66150).epsilon(1e-3));
	}
}

TEST_CASE("TapTempo averages recent taps and restarts after a pause") {
	TapTempo t;
	CHECK(t.tap(10.0) == 0);
	CHECK(t.tap(10.5) == doctest::Approx(120));
	CHECK(t.tap(11.0) == doctest::Approx(120));
	CHECK(t.tap(11.6) == doctest::Approx(60.0 * 3 / 1.6));
	for (int i = 0; i < 20; i++) t.tap(12.0 + i * 0.4);  // window keeps only the last 8
	CHECK(t.tap(12.0 + 20 * 0.4) == doctest::Approx(150));
	CHECK(t.tap(30.0) == 0);  // long pause
	CHECK(t.tap(30.25) == doctest::Approx(240));
}
