#include "sliceplayer.hpp"
#include <algorithm>
#include <cmath>

namespace spools {

SlicePlayer::Span SlicePlayer::span(const Audio& a, size_t start, size_t end, float startTrim, float endTrim, bool reverse) {
	double s = std::clamp(start + double(startTrim) * a.sampleRate, 0.0, double(a.frames));
	double e = std::clamp(end + double(endTrim) * a.sampleRate, 0.0, double(a.frames));
	double minLen = std::max(1.0, double(kDeclick) * a.sampleRate);
	if (e - s < minLen) e = std::min(double(a.frames), s + minLen), s = std::max(0.0, e - minLen);
	return {s, e, reverse};
}

void SlicePlayer::start(const Audio* a, Span sp, double r, float engineRate) {
	audio = a;
	s = sp;
	rate = r;
	pos = s.reverse ? s.end - 1 : s.start;
	gain = 0;
	sampleRate = engineRate;
	declickStep = 1.f / std::max(1.f, kDeclick * engineRate);
	fadeStep = declickStep;
}

void SlicePlayer::stop(float seconds) {
	if (audio) fadeStep = -1.f / std::max(1.f, std::max(seconds, kDeclick) * sampleRate);
}

double SlicePlayer::progress() const {
	double len = s.end - s.start;
	return len > 0 ? std::clamp((s.reverse ? s.end - 1 - pos : pos - s.start) / len, 0.0, 1.0) : 1.0;
}

float SlicePlayer::read(int c, double x) const {
	const float* d = audio->channel(c);
	long i0 = long(std::floor(x));
	const int n = sinc->taps();
	const float* k = sinc->kernel(x - i0, rate);
	long first = i0 - n / 2 + 1;
	float acc = 0;
	if (first >= 0 && first + n <= long(audio->frames)) {
		for (int t = 0; t < n; t++) acc += k[t] * d[first + t];
	}
	else {
		for (int t = 0; t < n; t++) {
			long j = first + t;
			if (j >= 0 && j < long(audio->frames)) acc += k[t] * d[j];
		}
	}
	return acc;
}

bool SlicePlayer::process(float& l, float& r) {
	l = r = 0;
	if (!audio) return false;

	// Fade out ahead of the span end so playback never cuts mid-cycle.
	double remaining = s.reverse ? pos - s.start + 1 : s.end - pos;
	double samplesLeft = remaining / std::max(rate, 1e-9);
	if (fadeStep > 0 && samplesLeft * declickStep <= gain) fadeStep = -declickStep;

	gain = std::clamp(gain + fadeStep, 0.f, 1.f);
	if ((fadeStep < 0 && gain <= 0.f) || remaining <= 0) {
		audio = nullptr;
		return false;
	}
	l = read(0, pos) * gain;
	r = audio->channels > 1 ? read(1, pos) * gain : l;
	pos += s.reverse ? -rate : rate;
	return true;
}

}
