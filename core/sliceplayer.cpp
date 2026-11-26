#include "sliceplayer.hpp"
#include <algorithm>
#include <array>
#include <type_traits>
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
	band = sinc->band(r);
	pos = s.reverse ? s.end - 1 : s.start;
	loopOn = false;
	stretchOn = inGrains = spawnDone = false;
	grains[0].active = grains[1].active = false;
	dir = 1;
	gain = 0;
	sampleRate = engineRate;
	declickStep = 1.f / std::max(1.f, kDeclick * engineRate);
	fadeStep = declickStep;
}

void SlicePlayer::setLoop(LoopPoints p) {
	p.start = std::max(p.start, s.start);
	p.end = std::min(p.end, s.end);
	loopOn = !s.reverse && p.end - p.start >= 4;
	if (!loopOn) return;
	const double len = p.end - p.start;
	p.xfade = p.pingPong ? 0 : std::clamp(p.xfade, 0.0, std::min(len / 2, p.start - s.start));
	lp = p;
}

bool SlicePlayer::setStretch(const StretchPlan& p) {
	st = p;
	st.from = std::max(p.from, s.start);
	st.to = std::min(p.to, s.end);
	stretchOn = !s.reverse && !loopOn && p.grain >= 8 && st.to - st.from >= 1.5 * p.grain && p.outSamples * rate >= 1.5 * p.grain;
	rng = p.seed | 1;
	return stretchOn;
}

// Starts the next grain where the output's progress maps into the region. With a period, the start moves by
// whole periods from where the newest grain will be, so overlapping grains reinforce instead of beating;
// without one, a small random jitter breaks up the repetition.
void SlicePlayer::spawn() {
	const double hopOut = st.grain / (2 * rate), lastStart = st.to - st.grain;
	const double span = std::max(1.0, st.outSamples - 2 * hopOut);
	double target = st.from + (lastStart - st.from) * std::clamp(outDone / span, 0.0, 1.0);
	const bool first = !grains[0].active && !grains[1].active && outDone == 0;
	if (!first) {
		const double now = grains[newest].src;  // the new grain overlaps the newest one from here
		if (st.period > 0) target = now + std::round((target - now) / st.period) * st.period;
		else {
			rng = rng * 1664525u + 1013904223u;
			target += (double(rng >> 8) / double(1 << 24) - 0.5) * 0.2 * st.grain;
		}
	}
	const bool last = outDone + 2.5 * hopOut >= st.outSamples || target >= lastStart;
	if (last) target = st.period > 0 && !first ? lastStart - std::fmod(lastStart - grains[newest].src, st.period) : lastStart;
	newest = grains[newest].active ? newest ^ 1 : newest;
	grains[newest] = {std::clamp(target, st.from, lastStart), 0, true, first, last};
	spawnDone = last;
	nextSpawn += hopOut;
}

// sin^2(pi x) for x in [0, 1], from a table: grains need one per sample each.
static float hann(double x) {
	static const auto table = [] {
		std::array<float, 513> t{};
		for (size_t i = 0; i < t.size(); i++) t[i] = float(std::pow(std::sin(M_PI * double(i) / 512), 2));
		return t;
	}();
	const double p = std::clamp(x, 0.0, 1.0) * 512;
	const size_t i = std::min(size_t(p), size_t(511));
	const float f = float(p - double(i));
	return table[i] + (table[i + 1] - table[i]) * f;
}

void SlicePlayer::processGrains(float& l, float& r) {
	float acc[2] = {0, 0};
	for (Grain& g : grains) {
		if (!g.active) continue;
		const double x = g.done / st.grain;
		// Hann grains at 50% overlap sum to one. The first grain enters at full level (it continues the
		// native part) and the last one leaves at full level (the span's own end fade takes over).
		const float w = (g.first && x < 0.5) || (g.last && x >= 0.5) ? 1.f : hann(x);
		float a, b;
		read(g.src, a, b);
		acc[0] += a * w;
		acc[1] += b * w;
		g.src += rate;
		g.done += rate;
		if (g.done >= st.grain && !g.last) g.active = false;
	}
	l = acc[0];
	r = acc[1];
	pos = grains[newest].src;
	outDone += 1;
	nextSpawn -= 1;
	if (!spawnDone && nextSpawn <= 0) spawn();
}

void SlicePlayer::stop(float seconds) {
	if (audio) fadeStep = -1.f / std::max(1.f, std::max(seconds, kDeclick) * sampleRate);
}

double SlicePlayer::progress() const {
	double len = s.end - s.start;
	return len > 0 ? std::clamp((s.reverse ? s.end - 1 - pos : pos - s.start) / len, 0.0, 1.0) : 1.0;
}

void SlicePlayer::read(double x, float& l, float& r) const {
	const long i0 = long(std::floor(x)), n = sinc->taps(), first = i0 - n / 2 + 1;
	const float* k = sinc->kernel(x - double(i0), band);
	const float* d0 = audio->channel(0);
	const float* d1 = audio->channels > 1 ? audio->channel(1) : nullptr;
	float a = 0, b = 0;
	if (first >= 0 && first + n <= long(audio->frames)) {
		// Fixed tap counts let the compiler unroll and vectorise the dot products.
		auto dot = [&](auto taps) {
			for (long t = 0; t < taps; t++) a += k[t] * d0[first + t];
			if (d1) for (long t = 0; t < taps; t++) b += k[t] * d1[first + t];
		};
		if (n == 16) dot(std::integral_constant<long, 16>());
		else if (n == 8) dot(std::integral_constant<long, 8>());
		else dot(n);
	}
	else
		for (long t = 0; t < n; t++) {
			const long j = first + t;
			if (j < 0 || j >= long(audio->frames)) continue;
			a += k[t] * d0[j];
			if (d1) b += k[t] * d1[j];
		}
	l = a;
	r = d1 ? b : a;
}

bool SlicePlayer::process(float& l, float& r) {
	l = r = 0;
	if (!audio) return false;

	if (stretchOn && !inGrains && pos >= st.from) {
		inGrains = true;
		outDone = nextSpawn = 0;
		spawn();
	}
	// Fade out ahead of the span end so playback never cuts mid-cycle. A loop never reaches the end, and
	// stretched audio ends with its last grain.
	double remaining = loopOn ? s.end - s.start : s.reverse ? pos - s.start + 1 : s.end - pos;
	if (inGrains) remaining = spawnDone ? st.to - grains[newest].src : st.to - st.from;
	double samplesLeft = remaining / std::max(rate, 1e-9);
	if (fadeStep > 0 && samplesLeft * declickStep <= gain) fadeStep = -declickStep;

	gain = std::clamp(gain + fadeStep, 0.f, 1.f);
	if ((fadeStep < 0 && gain <= 0.f) || remaining <= 0) {
		audio = nullptr;
		return false;
	}
	float g = gain >= 1.f ? 1.f : 0.5f - 0.5f * std::cos(3.14159265f * gain);  // raised-cosine fades
	if (inGrains) {
		processGrains(l, r);
		l *= g;
		r *= g;
		return true;
	}
	read(pos, l, r);
	l *= g;
	r *= g;
	if (loopOn && lp.xfade > 0 && pos > lp.end - lp.xfade) {
		// Approaching the loop end: blend in the audio just before the loop start, which it will jump to.
		const double back = pos - (lp.end - lp.start);
		const float t = float((pos - (lp.end - lp.xfade)) / lp.xfade), a = 0.5f + 0.5f * std::cos(3.14159265f * t);
		float bl, br;
		read(back, bl, br);
		l = l * a + bl * g * (1 - a);
		r = r * a + br * g * (1 - a);
	}
	if (!loopOn) {
		pos += s.reverse ? -rate : rate;
		return true;
	}
	pos += dir * rate;
	if (lp.pingPong) {
		if (dir > 0 && pos >= lp.end) { pos = 2 * lp.end - pos; dir = -1; }
		if (dir < 0 && pos <= lp.start) { pos = 2 * lp.start - pos; dir = 1; }
	}
	else if (pos >= lp.end) pos -= lp.end - lp.start;
	return true;
}

}
