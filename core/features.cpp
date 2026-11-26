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

LoopCandidate findLoop(const Audio& a, size_t start, size_t end, size_t te, PitchEstimate pitch) {
	end = std::min(end, a.frames);
	if (end <= start + te) return {};
	std::vector<float> x(end - start);
	for (size_t i = 0; i < x.size(); i++) x[i] = mono(a, start + i);
	const double sr = a.sampleRate;
	const bool pitched = pitch.period >= 2 && pitch.confidence > 0.5f;
	const double period = pitched ? pitch.period : 0;
	const long n = long(x.size());
	const long W = std::clamp(long(pitched ? 2 * period : 0.01 * sr), 32L, 2048L);
	const double minLen = pitched ? std::ceil(0.02 * sr / period) * period : 0.02 * sr;

	// Nearest rising zero crossing (x[i-1] < 0 <= x[i]) within r of p, or -1.
	auto crossing = [&](long p, long r) {
		for (long d = 0; d <= r; d++)
			for (long i : {p - d, p + d})
				if (i > 0 && i < n && x[size_t(i - 1)] < 0 && x[size_t(i)] >= 0) return i;
		return -1L;
	};
	auto stats = [&](long from, double& rms, double& zcr) {
		double e = 0;
		int z = 0;
		for (long i = from; i < from + W; i++) {
			e += double(x[size_t(i)]) * x[size_t(i)];
			if (i > from && (x[size_t(i - 1)] < 0) != (x[size_t(i)] < 0)) z++;
		}
		rms = std::sqrt(e / double(W));
		zcr = double(z) / double(W);
	};
	auto ncc = [&](long p, long q) {
		double pq = 0, pp = 0, qq = 0;
		for (long i = 0; i < W; i++) {
			double u = x[size_t(p + i)], v = x[size_t(q + i)];
			pq += u * v; pp += u * u; qq += v * v;
		}
		return pp > 0 && qq > 0 ? pq / std::sqrt(pp * qq) : 0.0;
	};
	auto ratio = [](double u, double v) { return std::max(u, v) > 0 ? std::min(u, v) / std::max(u, v) : 1.0; };

	const long radius = pitched ? long(period) : long(0.005 * sr);
	const long e = crossing(n - 1 - long(0.001 * sr), radius);
	const long s0 = long(te) + W;  // the window before the start stays after the attack
	if (e < 0 || double(e - s0) < minLen) return {};
	const double maxLen = double(e - s0);

	std::vector<double> lengths;
	if (pitched) {
		const long k0 = long(std::lround(minLen / period)), k1 = long(maxLen / period);
		const long step = std::max(1L, (k1 - k0) / 23);
		for (long k = k0; k <= k1; k += step) lengths.push_back(double(k) * period);
	}
	else
		for (int i = 0; i < 16; i++) lengths.push_back(minLen * std::pow(maxLen / minLen, i / 15.0));

	double eRms, eZcr;
	stats(e - W, eRms, eZcr);
	LoopCandidate best;
	for (double len : lengths) {
		const long st = crossing(long(std::lround(double(e) - len)), pitched ? std::max(1L, long(period / 4)) : long(0.002 * sr));
		if (st < s0 || e - st < 4) continue;
		double sRms, sZcr;
		stats(st - W, sRms, sZcr);
		const double score = std::max(0.0, ncc(e - W, st - W)) * std::sqrt(ratio(eRms, sRms) * ratio(eZcr, sZcr))
			* (0.9 + 0.1 * double(e - st) / maxLen);  // prefer longer loops: less audible repetition
		if (score > best.score) best = {size_t(st), size_t(e), float(score)};
	}
	return best;
}

SliceFeatures analyzeSlice(const Audio& a, size_t start, size_t end) {
	SliceFeatures f;
	f.transientEnd = transientEnd(a, start, end);
	f.pitch = yin(a, start + f.transientEnd, end);
	f.loop = findLoop(a, start, end, f.transientEnd, f.pitch);
	return f;
}

std::vector<SliceFeatures> analyzeSlices(const Loop& l) {
	std::vector<SliceFeatures> out;
	for (size_t i = 0; i < l.slices.size(); i++) out.push_back(analyzeSlice(*l.audio, l.slices[i].start, l.sliceEnd(i)));
	return out;
}

}
