#include "pattern.hpp"
#include <cmath>
#include "units.hpp"

namespace spools {

void Pattern::range(int& first, int& n) const {
	const int last = int(steps.size()) - 1;
	int a = std::clamp(loopStart, 0, std::max(0, last)), b = loopEnd < 0 ? last : std::min(loopEnd, last);
	if (b < a) std::swap(a, b);
	first = a;
	n = steps.empty() ? 0 : b - a + 1;
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
	if (k < 0 || (ps == pass && k == slot)) return {};

	SeqEvent e;
	if (pat.loopCount > 0 && ps >= pat.loopCount) {
		finished = true;
		active = false;
		e.endOfPhrase = e.release = true;
		return e;
	}
	e.endOfPhrase = pass >= 0 && ps != pass;
	e.phraseStart = pass < 0;
	pass = ps;
	slot = k;
	active = true;

	const PatternStep& timing = pat.steps[size_t(order(k))];
	int content = order(k);
	if (pat.direction == Direction::Random) {
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		content = first + int(rng % uint32_t(n));
	}
	const PatternStep& st = pat.steps[size_t(content)];
	if (st.slice < 0) {  // rest: whatever is sounding is let go
		e.release = true;
		return e;
	}
	e.slice = wrap(st.slice + semis, int(l.slices.size()));
	e.sliceBeats = timing.beats();
	e.duration = st.duration;
	e.reverse = st.reverse;
	e.pitch = st.pitch;
	return e;
}

}
