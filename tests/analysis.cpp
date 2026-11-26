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
