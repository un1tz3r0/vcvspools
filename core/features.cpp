#include "features.hpp"
#include <algorithm>
#include <cmath>
#include "loop.hpp"

namespace spools {

static float mono(const Audio& a, size_t i) {
	return a.channels == 2 ? 0.5f * (a.data[0][i] + a.data[1][i]) : a.data[0][i];
}

size_t transientEnd(const Audio& a, size_t start, size_t end, float fraction) {
	end = std::min(end, a.frames);
	if (end <= start) return 0;
	const size_t hop = std::max<size_t>(1, size_t(a.sampleRate * 0.0025)), win = hop * 2;
	std::vector<float> level;  // RMS over `win`, every `hop`
	for (size_t i = start; i + win <= end; i += hop) {
		double e = 0;
		for (size_t k = i; k < i + win; k++) e += double(mono(a, k)) * mono(a, k);
		level.push_back(float(std::sqrt(e / double(win))));
	}
	const size_t len = end - start;
	if (level.size() < 3) return len;
	const size_t searchPeak = std::min(level.size(), size_t(0.08 * a.sampleRate / double(hop)) + 1);  // attack within 80 ms
	const size_t peak = size_t(std::max_element(level.begin(), level.begin() + long(searchPeak)) - level.begin());
	const float p = level[peak];
	if (p <= 0) return 0;
	for (size_t i = peak + 1; i + 1 < level.size(); i++) {
		const float slope = (level[i] - level[i + 1]) / p;  // fall per hop, relative to the peak
		if (level[i] < fraction * p && slope < 0.02f) return std::min(len, (i + 1) * hop);
	}
	return std::min(len, std::max(win, size_t(0.06 * a.sampleRate)));
}

PitchEstimate yin(const Audio& a, size_t from, size_t to, double minHz, double maxHz, size_t window) {
	to = std::min(to, a.frames);
	const size_t maxLag = size_t(a.sampleRate / minHz), minLag = std::max<size_t>(2, size_t(a.sampleRate / maxHz));
	if (to <= from || to - from < maxLag + 16) return {};
	const size_t w = std::min(window, to - from - maxLag);
	std::vector<float> x(w + maxLag + 1);
	for (size_t i = 0; i < x.size(); i++) x[i] = mono(a, from + i);

	std::vector<double> d(maxLag + 1, 0.0);
	for (size_t lag = 1; lag <= maxLag; lag++) {
		double s = 0;
		for (size_t i = 0; i < w; i++) {
			double diff = double(x[i]) - x[i + lag];
			s += diff * diff;
		}
		d[lag] = s;
	}
	// Cumulative mean normalised difference.
	std::vector<double> dn(maxLag + 1, 1.0);
	double run = 0;
	for (size_t lag = 1; lag <= maxLag; lag++) {
		run += d[lag];
		dn[lag] = run > 0 ? d[lag] * double(lag) / run : 1.0;
	}
	size_t best = 0;
	for (size_t lag = minLag; lag <= maxLag; lag++)
		if (dn[lag] < 0.15) {
			while (lag + 1 <= maxLag && dn[lag + 1] < dn[lag]) lag++;
			best = lag;
			break;
		}
	if (!best) best = size_t(std::min_element(dn.begin() + long(minLag), dn.end()) - dn.begin());
	double period = double(best);
	if (best > minLag && best < maxLag) {  // parabolic refinement
		double l = dn[best - 1], c = dn[best], r = dn[best + 1], den = l - 2 * c + r;
		if (den > 0) period += 0.5 * (l - r) / den;
	}
	return {period, float(std::clamp(1.0 - dn[best], 0.0, 1.0))};
}

SliceFeatures analyzeSlice(const Audio& a, size_t start, size_t end) {
	SliceFeatures f;
	f.transientEnd = transientEnd(a, start, end);
	f.pitch = yin(a, start + f.transientEnd, end);
	return f;
}

std::vector<SliceFeatures> analyzeSlices(const Loop& l) {
	std::vector<SliceFeatures> out;
	for (size_t i = 0; i < l.slices.size(); i++) out.push_back(analyzeSlice(*l.audio, l.slices[i].start, l.sliceEnd(i)));
	return out;
}

}
