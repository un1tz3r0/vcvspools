#include "sequencer.hpp"
#include <algorithm>
#include "units.hpp"

namespace spools {

int NoteSequencer::sliceAt(int k, const Loop& loop, const PhraseParams& p) {
	int r0 = int(loop.regionStart), n = int(loop.regionEnd) - r0 + 1, len = std::max(1, p.length);
	int off;
	switch (p.direction) {
		case Direction::Reverse: off = -k; break;
		case Direction::PingPong: off = (pass % 2) ? len - 1 - k : k; break;
		case Direction::Random:
			rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
			off = int(rng % uint32_t(len));
			break;
		default: off = k;
	}
	return r0 + wrap(first + off, n);
}

SeqEvent NoteSequencer::startSlice(double at, const Loop& loop, const PhraseParams& p, bool phraseStart) {
	slice = sliceAt(step, loop, p);
	SeqEvent e;
	e.slice = slice;
	e.sliceBeats = loop.sliceBeats(size_t(slice));
	e.phraseStart = phraseStart;
	nextAt = at + e.sliceBeats;
	return e;
}

SeqEvent NoteSequencer::process(double beat, bool gate, bool rising, int semis, const Loop& loop, const PhraseParams& p) {
	if (beat < lastBeat) {  // time base was reset: keep the schedule relative
		double d = lastBeat - beat;
		nextAt -= d;
		pendingAt -= d;
	}
	lastBeat = beat;

	if (rising) {
		pending = true;
		pendingSemis = semis;
		double q = p.launchQuantize;
		pendingAt = q > 0 ? std::ceil(beat / q - 1e-9) * q : beat;
	}

	if (pending && beat >= pendingAt - 1e-12) {
		pending = false;
		int r0 = int(loop.regionStart), n = int(loop.regionEnd) - r0 + 1;
		int start = std::clamp(p.startSlice, r0, r0 + n - 1) - r0;
		first = wrap(start + pendingSemis, n);
		step = pass = 0;
		gateMode = false;
		gateReleased = !gate;
		state = Playing;
		return startSlice(beat, loop, p, true);
	}

	if (state != Playing) return {};
	if (!gate && gateMode) gateReleased = true;
	if (beat < nextAt) return {};

	// Slice boundary.
	if (step == 0 && pass == 0 && gate) gateMode = true;
	if (gateMode && gateReleased) {
		state = Idle;
		SeqEvent e;
		e.release = true;
		return e;
	}
	step++;
	int len = std::max(1, p.length);
	bool eoc = false;
	if (step >= len) {
		eoc = true;
		step = 0;
		pass++;
		bool again = gateMode && gate && (p.loopCount <= 0 || pass < p.loopCount);
		if (!again) {
			state = Idle;
			SeqEvent e;
			e.endOfPhrase = e.release = true;
			return e;
		}
	}
	SeqEvent e = startSlice(nextAt, loop, p, false);
	e.endOfPhrase = eoc;
	return e;
}

}
