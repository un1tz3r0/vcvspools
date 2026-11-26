#include <doctest/doctest.h>
#include <cmath>
#include <random>
#include "slicing.hpp"

using namespace spools;

// Hits at the given beats (120 BPM, so 0.5 s per beat), 4 beats long.
static std::shared_ptr<Audio> beatsAudio(const std::vector<double>& beats, double totalBeats = 4) {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 44100;
	a->channels = 1;
	a->frames = size_t(totalBeats * 0.5 * 44100);
	a->data[0].assign(a->frames, 0.f);
	std::mt19937 rng(3);
	std::uniform_real_distribution<float> u(-1, 1);
	for (double b : beats)
		for (size_t i = 0; i < 4000; i++) {
			size_t j = size_t(b * 22050) + i;
			if (j < a->frames) a->data[0][j] += 0.8f * u(rng) * std::exp(-float(i) / 1200.f);
		}
	return a;
}

static bool near(size_t frame, double beat, double tolMs = 3) { return std::fabs(frame / 44100.0 - beat * 0.5) * 1000 < tolMs; }

TEST_CASE("onset mode slices at hits; snap pulls a late hit onto the grid") {
	auto a = beatsAudio({0, 1, 2.08, 3});
	auto env = onsetEnvelope(*a);
	SliceSettings s;
	auto f = computeSlices(*a, env, 120, 0, s);
	REQUIRE(f.size() == 4);
	CHECK(near(f[2], 2.08));
	s.snap = 1;
	f = computeSlices(*a, env, 120, 0, s);
	CHECK(near(f[2], 2.0, 2));
	s.snap = 0.5f;
	f = computeSlices(*a, env, 120, 0, s);
	CHECK(near(f[2], 2.04, 2));
}

TEST_CASE("grid mode ignores content; hybrid adds off-grid hits") {
	auto a = beatsAudio({0, 1.5, 2, 3});
	auto env = onsetEnvelope(*a);
	SliceSettings s;
	s.mode = SliceMode::Grid;
	s.gridBeats = 1;
	auto f = computeSlices(*a, env, 120, 0, s);
	CHECK(f.size() == 4);
	s.mode = SliceMode::Hybrid;
	f = computeSlices(*a, env, 120, 0, s);
	REQUIRE(f.size() == 5);
	CHECK(near(f[2], 1.5));
}

TEST_CASE("autoSlice detects tempo for plain loops and caches the analysis") {
	auto a = beatsAudio({0, 1, 2, 3, 4, 5, 6, 7}, 8);
	auto plain = makeLoop(a, {0, a->frames}, 0, false, {});
	CHECK_FALSE(plain->bpmKnown);
	SliceSettings s;
	auto l = autoSlice(*plain, s);
	CHECK(l->bpmKnown);
	CHECK(l->bpm == doctest::Approx(120).epsilon(1e-9));  // two bars exactly
	CHECK(l->slices.size() == 8);
	for (size_t i = 0; i < l->slices.size(); i++) CHECK(l->slices[i].beat == doctest::Approx(double(i)).epsilon(0.01));
	REQUIRE(bool(l->onsets));

	s.mode = SliceMode::Grid;
	s.gridBeats = 0.5;
	auto g = autoSlice(*l, s);
	CHECK(g->onsets.get() == l->onsets.get());
	CHECK(g->slices.size() == 16);
	CHECK(g->bpm == l->bpm);
	CHECK(near(g->slices[2].start, 1.0));
}
