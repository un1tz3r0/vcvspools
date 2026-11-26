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

TEST_CASE("auto-loop: whole periods of a steady tone, on rising zero crossings, after the attack") {
	for (double hz : {110.0, 220.0, 440.0}) {
		auto a = hitThenTone(hz);
		auto f = analyzeSlice(*a, 0, a->frames);
		const auto& lp = f.loop;
		REQUIRE(lp.score > 0.9f);
		CHECK(lp.start >= f.transientEnd);
		CHECK(lp.end <= a->frames);
		const double periods = double(lp.end - lp.start) / (48000 / hz);
		CHECK(std::fabs(periods - std::round(periods)) < 0.02);
		const auto& x = a->data[0];
		for (size_t i : {lp.start, lp.end}) CHECK((x[i - 1] < 0 && x[i] >= 0));
		CHECK(double(lp.end - lp.start) > 0.2 * 48000);  // prefers long loops when they all fit
	}
}

TEST_CASE("auto-loop: noise without pitch still finds a level-matched loop or none; short slices find none") {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	std::mt19937 rng(5);
	std::uniform_real_distribution<float> u(-1, 1);
	for (int i = 0; i < 24000; i++) a->data[0].push_back(0.3f * u(rng));
	a->frames = 24000;
	auto lp = findLoop(*a, 0, 24000, 0, {});
	CHECK(lp.score < 0.5f);
	CHECK(findLoop(*a, 0, 600, 0, {}).score == 0.f);
}
