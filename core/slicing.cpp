#include "slicing.hpp"
#include <algorithm>
#include <cmath>

namespace spools {

std::vector<size_t> computeSlices(const Audio& a, const OnsetEnvelope& env, double bpm, double phase, const SliceSettings& s) {
	const double sr = a.sampleRate, step = s.gridBeats * 60.0 / std::max(bpm, 1.0) * sr;
	double first = std::fmod(phase * sr, step);
	if (first < 0) first += step;
	if (first > step / 2) first -= step;  // a grid line just before 0 belongs at 0
	auto nearestGrid = [&](double f) { return first + std::round((f - first) / step) * step; };

	std::vector<double> pos;
	if (s.mode != SliceMode::Onsets)
		for (double f = first; f < double(a.frames); f += step) pos.push_back(f);
	if (s.mode != SliceMode::Grid) {
		OnsetOptions o;
		o.sensitivity = s.mode == SliceMode::Hybrid ? s.sensitivity * 0.6f : s.sensitivity;  // hybrid: strong hits only
		o.minGap = s.minGap;
		for (size_t on : pickOnsets(env, a, o)) {
			double g = nearestGrid(double(on));
			if (s.mode == SliceMode::Hybrid && std::fabs(g - double(on)) < step * 0.25) continue;  // the grid already has it
			pos.push_back(s.mode == SliceMode::Onsets ? double(on) + (g - double(on)) * s.snap : double(on));
		}
	}

	std::vector<size_t> out{0};
	for (double p : pos) {
		if (p < 0 || p >= double(a.frames)) continue;
		size_t f = size_t(std::lround(p));
		if (s.mode != SliceMode::Onsets || s.snap > 0) f = snapToZeroCrossing(a, f, 64);
		out.push_back(f);
	}
	std::sort(out.begin(), out.end());
	const size_t gap = std::max<size_t>(size_t(s.minGap * sr), 1);
	std::vector<size_t> merged;
	for (size_t f : out)
		if ((merged.empty() || f - merged.back() >= gap) && f + gap <= a.frames) merged.push_back(f);
	return merged;
}

// The flux envelope's peak lags or leads the true attack by a few ms; pull the grid onto the median onset offset.
static double alignPhase(const Audio& a, const OnsetEnvelope& env, double bpm, double phase, double gridBeats) {
	const double sr = a.sampleRate, step = gridBeats * 60.0 / std::max(bpm, 1.0) * sr, first = phase * sr;
	std::vector<double> d;
	for (size_t on : pickOnsets(env, a, OnsetOptions{})) {
		double off = double(on) - (first + std::round((double(on) - first) / step) * step);
		if (std::fabs(off) < step * 0.25) d.push_back(off);
	}
	if (d.empty()) return phase;
	std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
	return phase + d[d.size() / 2] / sr;
}

std::shared_ptr<Loop> autoSlice(const Loop& old, const SliceSettings& s, bool detectTempo) {
	auto env = old.onsets;
	if (!env) env = std::make_shared<const OnsetEnvelope>(onsetEnvelope(*old.audio));
	double bpm = old.bpm, phase = 0;
	if (detectTempo && !old.bpmKnown) {
		TempoEstimate t = estimateTempo(*env, old.audio->seconds(), old.meterNum);
		bpm = t.bpm;
		phase = t.phase;  // still an estimate: bpmKnown stays false, so a new crop re-estimates
	}
	else if (s.mode != SliceMode::Onsets || s.snap > 0) phase = gridPhase(*env, bpm);
	if (s.mode != SliceMode::Onsets || s.snap > 0) phase = alignPhase(*old.audio, *env, bpm, phase, s.gridBeats);
	std::vector<size_t> starts;
	for (size_t f : computeSlices(*old.audio, *env, bpm, phase, s)) starts.push_back(f + old.crop.start);
	auto loop = makeLoop(old.source, old.crop, bpm, old.bpmKnown, starts, old.origin, old.sourcePath);
	loop->meterNum = old.meterNum;
	loop->meterDen = old.meterDen;
	loop->onsets = env;
	return loop;
}

}
