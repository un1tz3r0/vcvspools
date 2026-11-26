#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include "audio.hpp"

namespace spools {

struct Loop;

struct PitchEstimate {
	double period = 0;     // frames; 0 = none found
	float confidence = 0;  // 0..1 (1 - YIN aperiodicity at the chosen lag)
};

// A loop found by searching (DESIGN §6.5): frames from the slice start, end exclusive. score 0 = none.
struct LoopCandidate {
	size_t start = 0, end = 0;
	float score = 0;  // 0..1
};

// What the tail modes need to know about one slice (DESIGN §5.6).
struct SliceFeatures {
	size_t transientEnd = 0;  // frames from the slice start
	PitchEstimate pitch;      // of the post-transient part
	LoopCandidate loop;       // best automatic loop in the post-transient part
};

// End of the attack: the first point after the level peak where the short-term level has fallen below
// `fraction` of the peak and stopped falling quickly. Searches [start, end) of the mono mix.
size_t transientEnd(const Audio& a, size_t start, size_t end, float fraction = 0.5f);

// YIN fundamental-period estimate over up to `window` frames from `from`.
PitchEstimate yin(const Audio& a, size_t from, size_t to, double minHz = 40, double maxHz = 2000, size_t window = 2048);

// Searches [start + transientEnd, end) for a seamless loop. Lengths are whole periods when the pitch is
// confident, otherwise spread from 20 ms to the whole region. Each is scored on the match between the
// audio before its end and before its start (the crossfade's two sides): correlation, level and
// brightness (zero-crossing rate). Both ends sit on rising zero crossings.
LoopCandidate findLoop(const Audio& a, size_t start, size_t end, size_t transientEnd, PitchEstimate pitch);

SliceFeatures analyzeSlice(const Audio& a, size_t start, size_t end);
std::vector<SliceFeatures> analyzeSlices(const Loop& loop);

}
