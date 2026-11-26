#include <doctest/doctest.h>
#include <cmath>
#include <random>
#include "features.hpp"
#include "loop.hpp"

using namespace spools;

// A 15 ms noise hit fading into a steady tone.
static std::shared_ptr<Audio> hitThenTone(double hz, double seconds = 0.5) {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = size_t(seconds * 48000);
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1, 1);
	for (size_t i = 0; i < a->frames; i++) {
		double t = i / 48000.0;
		float hit = t < 0.015 ? u(rng) * 0.9f * float(1 - t / 0.015) : 0.f;
		float tone = 0.15f * float(std::sin(2 * M_PI * hz * t)) * float(std::min(1.0, t / 0.01));
		a->data[0].push_back(hit + tone);
	}
	return a;
}

TEST_CASE("transient end falls just after the hit") {
	auto a = hitThenTone(220);
	size_t te = transientEnd(*a, 0, a->frames);
	CHECK(te > size_t(0.006 * 48000));
	CHECK(te < size_t(0.03 * 48000));
}

TEST_CASE("YIN finds the period of the sustained part, with high confidence") {
	for (double hz : {55.0, 110.0, 220.0, 440.0, 1000.0}) {
		auto a = hitThenTone(hz);
		PitchEstimate p = yin(*a, size_t(0.05 * 48000), a->frames);
		CHECK(p.period == doctest::Approx(48000 / hz).epsilon(0.005));
		CHECK(p.confidence > 0.9f);
	}
	auto noise = std::make_shared<Audio>();
	noise->sampleRate = 48000;
	noise->channels = 1;
	std::mt19937 rng(3);
	std::uniform_real_distribution<float> u(-1, 1);
	for (int i = 0; i < 24000; i++) noise->data[0].push_back(u(rng));
	noise->frames = 24000;
	CHECK(yin(*noise, 0, 24000).confidence < 0.6f);
	CHECK(yin(*noise, 0, 100).period == 0);  // too short
}

TEST_CASE("analyzeSlices covers every slice") {
	auto a = hitThenTone(220, 1.0);
	auto l = makeLoop(a, {0, a->frames}, 120, true, {24000});
	auto f = analyzeSlices(*l);
	REQUIRE(f.size() == 2);
	CHECK(f[0].pitch.period == doctest::Approx(48000 / 220.0).epsilon(0.01));
	CHECK(f[1].pitch.period == doctest::Approx(48000 / 220.0).epsilon(0.01));
}
