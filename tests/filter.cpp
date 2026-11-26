#include <doctest/doctest.h>
#include <cmath>
#include "filter.hpp"

using namespace spools;

static constexpr float kSr = 48000;

// Steady-state gain in dB of a sine at `hz` through a fresh filter.
static double gainDb(FilterType t, float volts, float res, double hz) {
	VoiceFilter f;
	f.set(t, volts, res, kSr);
	double in = 0, out = 0;
	const int n = 48000;
	for (int i = 0; i < n; i++) {
		float x = float(std::sin(2 * M_PI * hz * i / kSr));
		float y = f.process(0, x);
		if (i >= n / 2) in += double(x) * x, out += double(y) * y;
	}
	return 10 * std::log10(out / in);
}

TEST_CASE("cutoff follows 1 V/oct around C4") {
	CHECK(cutoffHz(0, kSr) == doctest::Approx(261.63).epsilon(1e-4));
	CHECK(cutoffHz(2, kSr) == doctest::Approx(1046.5).epsilon(1e-4));
	CHECK(cutoffHz(10, kSr) == doctest::Approx(0.49 * kSr));
}

TEST_CASE("12 dB modes have the expected slopes") {
	const float v = 2;  // ~1046.5 Hz
	const double fc = cutoffHz(v, kSr);
	CHECK(gainDb(FilterType::LP12, v, 0, fc / 10) == doctest::Approx(0).epsilon(0.1));
	CHECK(gainDb(FilterType::LP12, v, 0, fc) == doctest::Approx(-6.02).epsilon(0.02));
	CHECK(gainDb(FilterType::LP12, v, 0, fc * 10) < -38);
	CHECK(gainDb(FilterType::HP12, v, 0, fc * 10) == doctest::Approx(0).epsilon(0.1));
	CHECK(gainDb(FilterType::HP12, v, 0, fc / 10) < -38);
	CHECK(std::fabs(gainDb(FilterType::BP12, v, 0.5f, fc)) < 0.1);
	CHECK(gainDb(FilterType::BP12, v, 0.5f, fc / 10) < -15);
	CHECK(gainDb(FilterType::Notch, v, 0, fc) < -40);
	CHECK(std::fabs(gainDb(FilterType::Notch, v, 0, fc * 8)) < 0.5);
	CHECK(gainDb(FilterType::Off, v, 0, fc * 10) == doctest::Approx(0).epsilon(1e-6));
}

TEST_CASE("24 dB low-pass is Butterworth at zero resonance and resonates when asked") {
	const float v = 2;
	const double fc = cutoffHz(v, kSr);
	CHECK(gainDb(FilterType::LP24, v, 0, fc / 10) == doctest::Approx(0).epsilon(0.1));
	CHECK(gainDb(FilterType::LP24, v, 0, fc) == doctest::Approx(-3.01).epsilon(0.05));
	CHECK(gainDb(FilterType::LP24, v, 0, fc * 10) < -75);
	CHECK(gainDb(FilterType::LP24, v, 0.9f, fc) > 12);
	CHECK(gainDb(FilterType::LP12, v, 0.9f, fc) > 12);
}

TEST_CASE("filter stays bounded under fast cutoff sweeps at full resonance") {
	VoiceFilter f;
	float peak = 0;
	for (int i = 0; i < 96000; i++) {
		if (i % 4 == 0) f.set(FilterType::LP24, 8 * float(std::sin(i * 0.001)), 1, kSr);
		float y = f.process(1, (i / 50) % 2 ? 1.f : -1.f);
		REQUIRE(std::isfinite(y));
		peak = std::max(peak, std::fabs(y));
	}
	CHECK(peak < 200);
}
