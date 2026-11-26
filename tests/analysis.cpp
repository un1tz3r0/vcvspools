#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <random>
#include "analysis.hpp"
#include "fft.hpp"

using namespace spools;

static Audio silence(size_t n, float sr = 44100) {
	Audio a;
	a.sampleRate = sr;
	a.channels = 1;
	a.frames = n;
	a.data[0].assign(n, 0.f);
	return a;
}

// Decaying noise bursts ("hits") starting at the given seconds.
static Audio hits(const std::vector<double>& at, double seconds, float sr = 44100, float level = 0.8f) {
	Audio a = silence(size_t(seconds * sr), sr);
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1, 1);
	for (double t : at)
		for (size_t i = 0; i < size_t(0.15 * sr); i++) {
			size_t j = size_t(t * sr) + i;
			if (j < a.frames) a.data[0][j] += level * u(rng) * std::exp(-float(i) / (0.03f * sr));
		}
	return a;
}

TEST_CASE("fft matches a direct DFT") {
	Fft fft(16);
	std::vector<std::complex<float>> x(16), ref(16);
	for (int i = 0; i < 16; i++) x[size_t(i)] = {float(std::sin(i * 0.9) + 0.3 * i), 0.f};
	for (int k = 0; k < 16; k++)
		for (int i = 0; i < 16; i++) ref[size_t(k)] += x[size_t(i)] * std::polar(1.f, float(-2 * M_PI * k * i / 16));
	fft.forward(x);
	for (int k = 0; k < 16; k++) CHECK(std::abs(x[size_t(k)] - ref[size_t(k)]) < 1e-4f);
}

TEST_CASE("onset envelope peaks at each hit") {
	std::vector<double> at = {0.1, 0.35, 0.6, 0.85};
	Audio a = hits(at, 1.0);
	auto env = onsetEnvelope(a);
	CHECK(env.flux.size() == (a.frames + 255) / 256);
	for (double t : at) {
		size_t expect = size_t(std::lround(t * env.frameRate()));
		size_t best = expect;
		for (size_t i = expect - 4; i <= expect + 4; i++) if (env.flux[i] > env.flux[best]) best = i;
		CHECK(std::abs(long(best) - long(expect)) <= 2);
		// The peak dominates the quiet stretch before it.
		CHECK(env.flux[best] > 10 * env.flux[expect - 10] + 1);
	}
}

TEST_CASE("silence and steady tones have no onsets after the start") {
	auto quiet = onsetEnvelope(silence(44100));
	for (float f : quiet.flux) CHECK(f == 0.f);
	Audio tone = silence(44100);
	for (size_t i = 0; i < tone.frames; i++) tone.data[0][i] = 0.5f * std::sin(2 * M_PI * 440 * i / 44100.0);
	auto env = onsetEnvelope(tone);
	float early = *std::max_element(env.flux.begin(), env.flux.begin() + 8);
	float later = *std::max_element(env.flux.begin() + 20, env.flux.end() - 8);
	CHECK(later < 0.05f * early);
}

TEST_CASE("onset picking finds each hit within 2 ms, at a zero crossing") {
	std::vector<double> at = {0.05, 0.3, 0.55, 0.8, 1.05, 1.3, 1.55};
	Audio a = hits(at, 1.8);
	auto env = onsetEnvelope(a);
	auto on = pickOnsets(env, a);
	REQUIRE(on.size() == at.size());
	for (size_t i = 0; i < at.size(); i++) {
		CHECK(std::fabs(double(on[i]) / 44100 - at[i]) < 0.002);
		CHECK(std::fabs(a.data[0][on[i]]) < 0.2f);
	}
}

TEST_CASE("sensitivity trades weak hits; min gap merges flams") {
	Audio a = hits({0.1, 0.5}, 1.2);
	Audio weak = hits({0.9}, 1.2, 44100, 0.06f);
	for (size_t i = 0; i < a.frames; i++) a.data[0][i] += weak.data[0][i];
	auto env = onsetEnvelope(a);
	OnsetOptions o;
	o.sensitivity = 0.0f;
	CHECK(pickOnsets(env, a, o).size() == 2);
	o.sensitivity = 1.0f;
	CHECK(pickOnsets(env, a, o).size() == 3);

	Audio flam = hits({0.2, 0.215, 0.6}, 1.0);
	auto fenv = onsetEnvelope(flam);
	o.sensitivity = 0.8f;
	o.minGap = 0.05;
	CHECK(pickOnsets(fenv, flam, o).size() == 2);
}

TEST_CASE("zero-crossing snap") {
	Audio a = silence(100);
	for (size_t i = 0; i < 100; i++) a.data[0][i] = float(std::sin(2 * M_PI * double(i) / 40.0));  // crossings every 20
	CHECK(snapToZeroCrossing(a, 47) == 40);
	CHECK(snapToZeroCrossing(a, 0) == 0);
	CHECK(snapToZeroCrossing(a, 47, 3) == 47);
}

static std::vector<double> grid(double bpm, double offset, double seconds, int every = 1) {
	std::vector<double> at;
	for (double t = offset; t < seconds - 0.05; t += 60.0 / bpm * every) at.push_back(t);
	return at;
}

TEST_CASE("tempo: bar-exact loop snaps to the exact tempo") {
	double seconds = 8 * 60.0 / 128;  // two bars at 128
	Audio a = hits(grid(128, 0, seconds), seconds);
	auto est = estimateTempo(onsetEnvelope(a), seconds);
	CHECK(est.barFit);
	CHECK(est.bpm == doctest::Approx(128).epsilon(1e-9));
	CHECK(est.phase == 0);
}

TEST_CASE("tempo: offset grid on a non-bar-length file") {
	double seconds = 7.3;
	Audio a = hits(grid(95, 0.1, seconds), seconds);
	auto est = estimateTempo(onsetEnvelope(a), seconds);
	CHECK_FALSE(est.barFit);
	CHECK(est.bpm == doctest::Approx(95).epsilon(0.01));
	CHECK(est.phase == doctest::Approx(0.1).epsilon(0.08));
	CHECK(est.confidence > 0.3);
}

TEST_CASE("tempo: the prior picks 140 over 70, eighth-note hats do not double it") {
	double seconds = 6.1;
	auto at = grid(140, 0, seconds);
	Audio a = hits(at, seconds);
	Audio hats = hits(grid(280, 60.0 / 280, seconds, 2), seconds, 44100, 0.15f);
	for (size_t i = 0; i < a.frames; i++) a.data[0][i] += hats.data[0][i];
	auto est = estimateTempo(onsetEnvelope(a), seconds);
	CHECK(est.bpm == doctest::Approx(140).epsilon(0.01));
}
