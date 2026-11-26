#include "edit.hpp"
#include <algorithm>

namespace spools {

static std::shared_ptr<Loop> copyWithBeats(const Loop& old, std::vector<Slice> slices) {
	auto l = std::make_shared<Loop>(old);
	l->slices = std::move(slices);
	l->features = nullptr;  // slice extents changed
	for (auto& s : l->slices) s.beat = l->frameToBeat(s.start);
	const size_t last = l->slices.size() - 1;
	l->regionStart = std::min(l->regionStart, last);
	l->regionEnd = std::min(std::max(l->regionEnd, l->regionStart), last);
	return l;
}

size_t sliceAt(const Loop& loop, size_t frame) {
	auto it = std::upper_bound(loop.slices.begin(), loop.slices.end(), frame, [](size_t f, const Slice& s) { return f < s.start; });
	return it == loop.slices.begin() ? 0 : size_t(it - loop.slices.begin()) - 1;
}

std::shared_ptr<Loop> moveBoundary(const Loop& old, size_t i, size_t frame) {
	if (i == 0 || i >= old.slices.size()) return std::make_shared<Loop>(old);
	const size_t gap = std::max<size_t>(1, size_t(kMinSliceSeconds * old.audio->sampleRate));
	const size_t lo = old.slices[i - 1].start + gap, hi = old.sliceEnd(i) > gap ? old.sliceEnd(i) - gap : 0;
	auto slices = old.slices;
	if (lo <= hi) slices[i].start = std::clamp(frame, lo, hi);
	return copyWithBeats(old, std::move(slices));
}

std::shared_ptr<Loop> addSlice(const Loop& old, size_t frame) {
	const size_t i = sliceAt(old, frame);
	const size_t gap = std::max<size_t>(1, size_t(kMinSliceSeconds * old.audio->sampleRate));
	if (frame < old.slices[i].start + gap || frame + gap > old.sliceEnd(i)) return std::make_shared<Loop>(old);
	auto slices = old.slices;
	Slice s = slices[i];
	s.start = frame;
	slices.insert(slices.begin() + long(i) + 1, s);
	auto l = copyWithBeats(old, std::move(slices));
	if (old.regionEnd >= i) l->regionEnd = old.regionEnd + 1;
	if (old.regionStart > i) l->regionStart = old.regionStart + 1;
	return l;
}

std::shared_ptr<Loop> removeSlice(const Loop& old, size_t i) {
	if (i == 0 || i >= old.slices.size()) return std::make_shared<Loop>(old);
	auto slices = old.slices;
	slices.erase(slices.begin() + long(i));
	auto l = copyWithBeats(old, std::move(slices));
	l->regionStart = old.regionStart >= i ? old.regionStart - 1 : old.regionStart;
	l->regionEnd = old.regionEnd >= i ? old.regionEnd - 1 : old.regionEnd;
	l->regionStart = std::min(l->regionStart, l->regionEnd);
	return l;
}

std::shared_ptr<Loop> setRegion(const Loop& old, size_t a, size_t b) {
	auto l = std::make_shared<Loop>(old);
	const size_t last = old.slices.size() - 1;
	l->regionStart = std::min(std::min(a, b), last);
	l->regionEnd = std::min(std::max(a, b), last);
	return l;
}

std::shared_ptr<Loop> setSliceParams(const Loop& old, const std::vector<size_t>& idx, const std::vector<SliceParams>& params) {
	auto l = std::make_shared<Loop>(old);
	for (size_t k = 0; k < idx.size() && k < params.size(); k++)
		if (idx[k] < l->slices.size()) l->slices[idx[k]].p = params[k];
	return l;
}

}
