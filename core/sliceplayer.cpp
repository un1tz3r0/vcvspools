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

void SlicePlayer::processGrains(float& l, float& r) {
	float acc[2] = {0, 0};
	for (Grain& g : grains) {
		if (!g.active) continue;
		const double x = g.done / st.grain;
		// Hann grains at 50% overlap sum to one. The first grain enters at full level (it continues the
		// native part) and the last one leaves at full level (the span's own end fade takes over).
		float w = (g.first && x < 0.5) || (g.last && x >= 0.5) ? 1.f : float(std::pow(std::sin(M_PI * x), 2));
		acc[0] += read(0, g.src) * w;
		if (audio->channels > 1) acc[1] += read(1, g.src) * w;
		g.src += rate;
		g.done += rate;
		if (g.done >= st.grain && !g.last) g.active = false;
	}
	l = acc[0];
	r = audio->channels > 1 ? acc[1] : acc[0];
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
	l = read(0, pos) * g;
	r = audio->channels > 1 ? read(1, pos) * g : l;
	if (loopOn && lp.xfade > 0 && pos > lp.end - lp.xfade) {
		// Approaching the loop end: blend in the audio just before the loop start, which it will jump to.
		const double back = pos - (lp.end - lp.start);
		const float t = float((pos - (lp.end - lp.xfade)) / lp.xfade), a = 0.5f + 0.5f * std::cos(3.14159265f * t);
		l = l * a + read(0, back) * g * (1 - a);
		r = audio->channels > 1 ? r * a + read(1, back) * g * (1 - a) : l;
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
