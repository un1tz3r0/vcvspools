#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "sliceplayer.hpp"

using namespace spools;

static Audio sine(double freq, double sr, size_t n, int channels = 1) {
	Audio a;
	a.sampleRate = float(sr);
	a.channels = channels;
	a.frames = n;
	for (int c = 0; c < channels; c++)
		for (size_t i = 0; i < n; i++) a.data[c].push_back(float(std::sin(2 * M_PI * freq * (c + 1) * i / sr)));
	return a;
}

static std::vector<float> run(SlicePlayer& p, size_t max = 1 << 20) {
	std::vector<float> out;
	float l, r;
	while (out.size() < max && p.process(l, r)) out.push_back(l);
	return out;
}

TEST_CASE("1:1 playback reproduces samples after the declick ramp") {
	Audio a = sine(440, 48000, 4800);
	SlicePlayer p;
	p.start(&a, SlicePlayer::span(a, 1000, 3000, 0, 0, false), 1.0, 48000);
	auto out = run(p);
	CHECK(out.size() == doctest::Approx(2000).epsilon(0.001));
	CHECK(std::fabs(out[0]) < 0.05f);
	for (size_t i = 60; i < 1900; i += 37) CHECK(out[i] == doctest::Approx(a.data[0][1000 + i]).epsilon(1e-4));
	CHECK(std::fabs(out.back()) < 0.05f);
}

TEST_CASE("fractional rates interpolate accurately") {
	Audio a = sine(1000, 48000, 48000);
	for (double rate : {0.5, 0.7071, 1.4142}) {
		CAPTURE(rate);
		SlicePlayer p;
		p.start(&a, SlicePlayer::span(a, 100, 40000, 0, 0, false), rate, 48000);
		auto out = run(p, 4000);
		double maxErr = 0;
		for (size_t i = 100; i < out.size(); i++) {
			double x = 100 + i * rate;
			maxErr = std::max(maxErr, std::fabs(out[i] - std::sin(2 * M_PI * 1000 * x / 48000)));
		}
		CHECK(maxErr < 2e-3);
	}
}

TEST_CASE("fast reads are band-limited") {
	Audio a = sine(18000, 48000, 48000);
	SlicePlayer p;
	p.start(&a, SlicePlayer::span(a, 0, 48000, 0, 0, false), 2.0, 48000);
	auto out = run(p, 8000);
	double e = 0;
	for (size_t i = 500; i < out.size(); i++) e += out[i] * out[i];
	CHECK(std::sqrt(e / (out.size() - 500)) < 0.07);  // input RMS is 0.707
}

TEST_CASE("reverse plays the span backwards") {
	Audio a = sine(300, 48000, 4800);
	SlicePlayer p;
	p.start(&a, SlicePlayer::span(a, 1000, 2000, 0, 0, true), 1.0, 48000);
	auto out = run(p);
	REQUIRE(out.size() > 900);
	for (size_t i = 60; i < 900; i += 41) CHECK(out[i] == doctest::Approx(a.data[0][1999 - i]).epsilon(1e-4));
}

TEST_CASE("trims move the span and clamp to the audio") {
	Audio a = sine(300, 1000, 1000);
	auto s = SlicePlayer::span(a, 200, 400, -0.05f, 0.1f, false);
	CHECK(s.start == doctest::Approx(150));
	CHECK(s.end == doctest::Approx(500));
	s = SlicePlayer::span(a, 900, 1000, 0.5f, 0, false);  // collapses: keep at least the declick length
	CHECK(s.end == doctest::Approx(1000));
	CHECK(s.end - s.start >= 1);
	s = SlicePlayer::span(a, 0, 100, -1.f, 0, false);
	CHECK(s.start == 0);
}

TEST_CASE("stop fades out over the requested time, then idles") {
	Audio a = sine(200, 48000, 48000, 2);
	SlicePlayer p;
	p.start(&a, SlicePlayer::span(a, 0, 48000, 0, 0, false), 1.0, 48000);
	float l, r;
	for (int i = 0; i < 1000; i++) p.process(l, r);
	p.stop(0.01f);
	CHECK(p.stopping());
	int n = 0;
	while (p.process(l, r)) n++;
	CHECK(n == doctest::Approx(480).epsilon(0.02));
	CHECK_FALSE(p.active());
}

TEST_CASE("stereo channels are read independently") {
	Audio a = sine(100, 48000, 4800, 2);
	SlicePlayer p;
	p.start(&a, SlicePlayer::span(a, 0, 4800, 0, 0, false), 1.0, 48000);
	float l = 0, r = 0;
	for (int i = 0; i < 1201; i++) p.process(l, r);
	CHECK(l == doctest::Approx(a.data[0][1200]).epsilon(1e-4));
	CHECK(r == doctest::Approx(a.data[1][1200]).epsilon(1e-4));
}

TEST_CASE("eco quality is usable") {
	Audio a = sine(1000, 48000, 48000);
	SlicePlayer p;
	p.setQuality(&SincTable::eco());
	p.start(&a, SlicePlayer::span(a, 100, 40000, 0, 0, false), 0.7071, 48000);
	auto out = run(p, 4000);
	double maxErr = 0;
	for (size_t i = 100; i < out.size(); i++) maxErr = std::max(maxErr, std::fabs(out[i] - std::sin(2 * M_PI * 1000 * (100 + i * 0.7071) / 48000)));
	CHECK(maxErr < 2e-2);
}
