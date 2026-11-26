#pragma once
#include <memory>
#include "analysis.hpp"
#include "loop.hpp"

namespace spools {

enum class SliceMode : uint8_t { Onsets, Grid, Hybrid };

struct SliceSettings {
	SliceMode mode = SliceMode::Onsets;
	double gridBeats = 0.25;  // grid step in quarter notes (0.25 = 1/16)
	float sensitivity = 0.5f;
	float snap = 0.f;         // 0 = onsets where found, 1 = fully on the grid
	double minGap = 0.05;     // seconds
};

// Slice start frames within `audio` (always including 0), ascending.
std::vector<size_t> computeSlices(const Audio& audio, const OnsetEnvelope& env, double bpm, double phaseSeconds,
	const SliceSettings& s);

// Re-slices a loop. With `detectTempo`, a tempo that is not already known (REX/Acid/manual) is estimated first;
// it stays marked unknown.
// The onset envelope is computed once per crop and cached on the loop.
std::shared_ptr<Loop> autoSlice(const Loop& loop, const SliceSettings& s, bool detectTempo = true);

}
