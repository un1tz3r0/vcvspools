#pragma once
#include <memory>
#include <vector>
#include "loop.hpp"

namespace spools {

// Slice-map edits from the display. Each returns a new Loop that shares the audio, mips and analysis of
// the old one, so keeping both for undo costs only the slice list. Frames are in Loop::audio.

// The smallest slice an edit may leave behind.
constexpr double kMinSliceSeconds = 0.005;

// Moves the start of slice i (i >= 1) to `frame`, kept at least the minimum length from its neighbours.
std::shared_ptr<Loop> moveBoundary(const Loop& loop, size_t i, size_t frame);
// Splits the slice under `frame`; the new right half copies the left half's settings.
std::shared_ptr<Loop> addSlice(const Loop& loop, size_t frame);
// Merges slice i (i >= 1) into the one before it.
std::shared_ptr<Loop> removeSlice(const Loop& loop, size_t i);
// Sets the region to slices [a, b], in either order.
std::shared_ptr<Loop> setRegion(const Loop& loop, size_t a, size_t b);
// Replaces the per-slice settings of the given slices.
std::shared_ptr<Loop> setSliceParams(const Loop& loop, const std::vector<size_t>& slices, const std::vector<SliceParams>& params);

// Index of the slice containing `frame`.
size_t sliceAt(const Loop& loop, size_t frame);

}
