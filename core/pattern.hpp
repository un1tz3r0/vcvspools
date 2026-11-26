#pragma once
#include <cstdint>
#include <vector>
#include "sequencer.hpp"

namespace spools {

// One step of a slice-remap pattern (DESIGN §11). Lengths are in ticks of a 1/32 note (1/8 beat).
struct PatternStep {
	static constexpr double kTickBeats = 0.125;
	int slice = 0;          // -1 = rest
	int ticks = 4;          // 1..32 (a 1/32 note to a 4/4 bar)
	float nudge = 0;        // ticks, -1..1: moves the start without moving the grid
	float duration = 1;     // fraction of the step that the slice is gated for
	bool reverse = false;
	float pitch = 0;        // semitones
	double beats() const { return ticks * kTickBeats; }
};

struct Pattern {
	std::vector<PatternStep> steps;
	int loopStart = 0, loopEnd = -1;  // inclusive step range; -1 = the last step
	int loopCount = 0;                // passes, 0 = forever
	Direction direction = Direction::Forward;

	void range(int& first, int& n) const;  // the steps that play
};

// One step per slice of the loop's region, in order, with lengths on the 1/32 grid (rounded so they still
// add up to the region's length).
Pattern initFromLoop(const Loop& loop);

// Pattern-mode sequencer: like LoopSequencer, positioned from the transport's loop beat. V/oct shifts the slice
// of every step; direction reorders the steps of each pass (random picks a step's content for each slot).
class PatternSequencer {
public:
	SeqEvent process(double loopBeat, bool running, bool restart, int semitones, const Loop& loop, const Pattern& pattern);
	void reset() { active = false; pass = -1; slot = -1; }
	void seed(uint32_t s) { rng = s ? s : 1; }

private:
	bool active = false, finished = false;
	long pass = -1;
	int slot = -1;
	uint32_t rng = 0x9E3779B9u;
};

}
