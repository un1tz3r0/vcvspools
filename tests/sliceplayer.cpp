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

TEST_CASE("loops: a forward loop with crossfade plays seamlessly past the span end") {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = 4800;
	for (size_t i = 0; i < a->frames; i++) a->data[0].push_back(0.5f * float(std::sin(2 * M_PI * i / 48.0)));  // 1 kHz
	SlicePlayer p;
	p.start(a.get(), {0, 4800, false}, 1.0, 48000);
	p.setLoop({1200, 2400 + 7, false, 96});  // a loop length that is NOT a whole number of periods
	REQUIRE(p.looping());
	float l, r, prev = 0, maxStep = 0;
	for (int i = 0; i < 48000; i++) {
		REQUIRE(p.process(l, r));
		if (i > 100) maxStep = std::max(maxStep, std::fabs(l - prev));
		prev = l;
	}
	CHECK(maxStep < 0.5f * 2 * float(M_PI) / 48 * 1.6f);  // never steeper than the sine (plus crossfade wobble)
	CHECK(p.position() >= 1200);
	CHECK(p.position() < 2407);
	p.stop(0.005f);
	int n = 0;
	while (p.process(l, r)) n++;
	CHECK(n == doctest::Approx(240).epsilon(0.02));
}

TEST_CASE("loops: ping-pong turns at both ends; reversed spans ignore loops") {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = 1000;
	for (size_t i = 0; i < a->frames; i++) a->data[0].push_back(float(i) / 1000.f);  // ramp
	SlicePlayer p;
	p.start(a.get(), {0, 1000, false}, 1.0, 48000);
	p.setLoop({400, 600, true, 0});
	float l, r;
	double lo = 1e9, hi = 0;
	for (int i = 0; i < 2000; i++) {
		p.process(l, r);
		if (i > 700) { lo = std::min(lo, p.position()); hi = std::max(hi, p.position()); }
	}
	CHECK(lo == doctest::Approx(400).epsilon(0.01));
	CHECK(hi == doctest::Approx(600).epsilon(0.01));

	SlicePlayer q;
	q.start(a.get(), {0, 1000, true}, 1.0, 48000);
	q.setLoop({400, 600, false, 0});
	CHECK_FALSE(q.looping());
}
