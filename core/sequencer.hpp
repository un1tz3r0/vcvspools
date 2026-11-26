#pragma once
#include <cmath>
#include <cstdint>
#include "loop.hpp"

namespace spools {

enum class Direction : uint8_t { Forward, Reverse, PingPong, Random };

struct PhraseParams {
	int startSlice = 0;     // absolute slice index, clamped into the region
	int length = 1;         // slices per phrase
	Direction direction = Direction::Forward;
	int loopCount = 0;      // passes while the gate is held; 0 = forever
	double launchQuantize = 0;  // beats; 0 = immediate
};

// What a voice should do on this sample.
struct SeqEvent {
	int slice = -1;           // >= 0: start this slice now
	double sliceBeats = 0;    // its time slot in beats
	bool phraseStart = false; // first slice of a newly triggered phrase
	bool endOfPhrase = false; // a pass just completed (EOC)
	bool release = false;     // the phrase is over: let the voice release
};

// Note-mode phrase sequencer for one voice (DESIGN §6.2). All timing is in beats from TimeBase,
// so tempo changes rescale slice start times. A gate released before the first slice ends counts as
// a trigger (the whole phrase plays); a gate held past it plays until released, then finishes the current slice.
class NoteSequencer {
public:
	SeqEvent process(double beat, bool gate, bool rising, int semitones, const Loop& loop, const PhraseParams& p);
	bool playing() const { return state != Idle; }
	int currentSlice() const { return slice; }
	void reset() { state = Idle; pending = false; slice = -1; }
	void seed(uint32_t s) { rng = s ? s : 1; }

private:
	enum State { Idle, Playing } state = Idle;
	bool pending = false, gateMode = false, gateReleased = false;
	double pendingAt = 0, nextAt = 0, lastBeat = 0;
	int pendingSemis = 0, first = 0, step = 0, pass = 0, slice = -1;
	uint32_t rng = 0x9E3779B9u;

	int sliceAt(int k, const Loop& loop, const PhraseParams& p);
	SeqEvent startSlice(double at, const Loop& loop, const PhraseParams& p, bool phraseStart);
};

}
