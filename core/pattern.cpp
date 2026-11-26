#include "pattern.hpp"
#include <cmath>
#include <algorithm>
#include "units.hpp"

namespace spools {

void Pattern::range(int& first, int& n) const {
	const int last = int(steps.size()) - 1;
	int a = std::clamp(loopStart, 0, std::max(0, last)), b = loopEnd < 0 ? last : std::min(loopEnd, last);
	if (b < a) std::swap(a, b);
	first = a;
	n = steps.empty() ? 0 : b - a + 1;
}

double PatternStep::hitAt(int j) const {
	const int r = std::clamp(ratchets, 1, 8);
	// 1 - (1 - u)^p: with p > 1 the gaps shrink towards the end (an accelerating roll), with p < 1 they grow.
	return 1 - std::pow(1 - double(std::clamp(j, 0, r)) / r, std::exp2(1.5 * std::clamp(ratchetCurve, -1.f, 1.f)));
}

float PatternStep::hitVelocity(int j) const {
	const int r = std::clamp(ratchets, 1, 8);
	const float t = r > 1 ? float(j) / float(r - 1) : 0.f;
	return std::clamp(velocity + velocityRamp * t, 0.f, 1.f);
}

Pattern initFromLoop(const Loop& l) {
	Pattern p;
	PhraseParams none;
	int r0, n;
	none.region(l, r0, n);
	const double origin = l.slices[size_t(r0)].beat;
	auto tick = [&](double beat) { return long(std::lround((beat - origin) / PatternStep::kTickBeats)); };
	for (int i = r0; i < r0 + n; i++) {
		const double end = l.slices[size_t(i)].beat + l.sliceBeats(size_t(i));
		PatternStep s;
		s.slice = i;
		s.ticks = int(std::clamp(tick(end) - tick(l.slices[size_t(i)].beat), 1L, 32L));
		p.steps.push_back(s);
	}
	return p;
}

Pattern shuffled(const Pattern& p, uint32_t seed) {
	Pattern out = p;
	uint32_t r = seed ? seed : 1;
	for (size_t i = out.steps.size(); i > 1; i--) {  // Fisher-Yates over the slices only
		r ^= r << 13; r ^= r >> 17; r ^= r << 5;
		std::swap(out.steps[i - 1].slice, out.steps[r % i].slice);
	}
	return out;
}

Pattern reversed(const Pattern& p) {
	Pattern out = p;
	std::reverse(out.steps.begin(), out.steps.end());
	return out;
}

Pattern withStepInserted(const Pattern& p, size_t at, const PatternStep& s) {
	Pattern out = p;
	if (out.steps.size() >= kMaxPatternSteps) return out;
	out.steps.insert(out.steps.begin() + long(std::min(at, out.steps.size())), s);
	if (out.loopEnd >= 0 && int(at) <= out.loopEnd) out.loopEnd++;
	return out;
}

Pattern withStepRemoved(const Pattern& p, size_t at) {
	Pattern out = p;
	if (out.steps.size() <= 1 || at >= out.steps.size()) return out;
	out.steps.erase(out.steps.begin() + long(at));
	if (out.loopEnd >= 0 && int(at) <= out.loopEnd) out.loopEnd = std::max(0, out.loopEnd - 1);
	if (int(at) < out.loopStart) out.loopStart--;
	return out;
}

SeqEvent PatternSequencer::process(double loopBeat, bool running, bool restart, int semis, const Loop& l, const Pattern& pat) {
	if (restart) { pass = -1; slot = -1; finished = false; }
	if (!running || finished) {
		if (!active) return {};
		active = false;
		SeqEvent e;
		e.release = true;
		return e;
	}
	int first, n;
	pat.range(first, n);
	if (n == 0 || l.slices.empty() || loopBeat < 0) return {};
	double total = 0;
	for (int k = 0; k < n; k++) total += pat.steps[size_t(first + k)].beats();

	const long ps = long(std::floor(loopBeat / total + 1e-9));
	const double within = loopBeat - double(ps) * total, tick = PatternStep::kTickBeats;
	// Which step's timing fills slot k: reversed passes play the steps (and their lengths) backwards.
	auto order = [&](int k) {
		const bool back = pat.direction == Direction::Reverse || (pat.direction == Direction::PingPong && ps % 2);
		return first + (back ? n - 1 - k : k);
	};
	// The slot now sounding: the last one whose (nudged) start has passed. Nudges never reorder steps.
	int k = -1;
	double at = 0, fire = 0;
	for (int j = 0; j < n; j++) {
		const PatternStep& st = pat.steps[size_t(order(j))];
		const double f = std::max(j ? fire : 0.0, at + st.nudge * tick);
		if (within + 1e-9 < f) break;
		k = j;
		fire = f;
		at += st.beats();
	}
	if (k < 0) return {};
	const PatternStep& timing = pat.steps[size_t(order(k))];
	const bool newSlot = ps != pass || k != slot;
	if (newSlot) {
		content = order(k);
		if (pat.direction == Direction::Random) {
			rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
			content = first + int(rng % uint32_t(n));
		}
	}
	const PatternStep& st = pat.steps[size_t(content)];
	// Ratchet hit within the slot.
	const double x = std::clamp((within - fire) / timing.beats(), 0.0, 1.0);
	int j = 0;
	while (j + 1 < std::clamp(st.ratchets, 1, 8) && st.hitAt(j + 1) <= x + 1e-9) j++;
	if (!newSlot && j == hit) return {};

	SeqEvent e;
	if (newSlot && pat.loopCount > 0 && ps >= pat.loopCount) {
		finished = true;
		active = false;
		e.endOfPhrase = e.release = true;
		return e;
	}
	e.endOfPhrase = pass >= 0 && ps != pass;
	e.phraseStart = pass < 0;
	pass = ps;
	slot = k;
	hit = j;
	active = true;

	if (st.slice < 0) {  // rest: whatever is sounding is let go
		e.release = true;
		return e;
	}
	e.slice = wrap(st.slice + semis, int(l.slices.size()));
	e.sliceBeats = timing.beats() * (st.hitAt(j + 1) - st.hitAt(j));
	e.gain = st.hitVelocity(j);
	e.duration = st.duration;
	e.reverse = st.reverse;
	e.pitch = st.pitch;
	return e;
}

}
