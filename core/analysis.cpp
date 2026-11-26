#include "analysis.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include "fft.hpp"

namespace spools {

OnsetEnvelope onsetEnvelope(const Audio& a, size_t n, size_t hop) {
	OnsetEnvelope env;
	env.hop = hop;
	env.sampleRate = a.sampleRate;
	if (!a.frames) return env;

	const size_t frames = (a.frames + hop - 1) / hop;
	Fft fft(n);
	std::vector<float> window(n), prev(n / 2 + 1, 0.f), mag(n / 2 + 1);
	for (size_t i = 0; i < n; i++) window[i] = float(0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * double(i) / double(n)));
	std::vector<std::complex<float>> buf(n);
	env.flux.resize(frames);

	for (size_t t = 0; t < frames; t++) {
		long centre = long(t * hop);
		for (size_t i = 0; i < n; i++) {
			long j = centre - long(n / 2) + long(i);
			float x = 0;
			if (j >= 0 && j < long(a.frames)) {
				for (int c = 0; c < a.channels; c++) x += a.data[c][size_t(j)];
				x /= float(a.channels);
			}
			buf[i] = {x * window[i], 0.f};
		}
		fft.forward(buf);
		float f = 0;
		for (size_t k = 0; k <= n / 2; k++) {
			mag[k] = std::log1p(100.f * std::abs(buf[k]));
			f += std::max(0.f, mag[k] - prev[k]);
		}
		std::swap(prev, mag);
		env.flux[t] = f;
	}

	// High-pass: subtract a centred moving average of ~0.1 s, keep positive part.
	const size_t w = std::max<size_t>(1, size_t(0.05 * env.frameRate()));
	std::vector<double> prefix(frames + 1, 0.0);
	for (size_t t = 0; t < frames; t++) prefix[t + 1] = prefix[t] + env.flux[t];
	std::vector<float> out(frames);
	for (size_t t = 0; t < frames; t++) {
		size_t lo = t > w ? t - w : 0, hi = std::min(frames, t + w + 1);
		float mean = float((prefix[hi] - prefix[lo]) / double(hi - lo));
		out[t] = std::max(0.f, env.flux[t] - mean);
	}
	env.flux = std::move(out);
	return env;
}

static float mono(const Audio& a, size_t i) {
	float x = 0;
	for (int c = 0; c < a.channels; c++) x += a.data[c][i];
	return x;
}

size_t snapToZeroCrossing(const Audio& a, size_t frame, size_t maxBack) {
	if (frame == 0 || frame >= a.frames) return std::min(frame, a.frames);
	size_t lo = frame > maxBack ? frame - maxBack : 0;
	for (size_t i = frame; i > lo; i--) {
		float x0 = mono(a, i - 1), x1 = mono(a, i);
		if (x1 == 0.f || (x0 < 0.f) != (x1 < 0.f)) return std::fabs(x0) < std::fabs(x1) ? i - 1 : i;
	}
	return frame;
}

std::vector<size_t> pickOnsets(const OnsetEnvelope& env, const Audio& a, const OnsetOptions& o) {
	std::vector<size_t> out;
	const auto& f = env.flux;
	if (f.empty()) return out;
	float peak = *std::max_element(f.begin(), f.end());
	if (peak <= 0.f) return out;

	const float delta = (1.f - std::clamp(o.sensitivity, 0.f, 1.f)) * 0.6f + 0.02f;
	const size_t w = std::max<size_t>(2, size_t(0.1 * env.frameRate()));
	const size_t gap = std::max<size_t>(1, size_t(o.minGap * env.frameRate()));
	std::vector<float> window;
	long last = -long(gap) - 1;

	for (size_t t = 0; t < f.size(); t++) {
		float v = f[t] / peak;
		if ((t > 0 && f[t] < f[t - 1]) || (t + 1 < f.size() && f[t] <= f[t + 1])) continue;  // local max only
		size_t lo = t > w ? t - w : 0, hi = std::min(f.size(), t + w + 1);
		window.assign(f.begin() + long(lo), f.begin() + long(hi));
		std::nth_element(window.begin(), window.begin() + long(window.size() / 2), window.end());
		if (v < window[window.size() / 2] / peak + delta) continue;
		if (long(t) - last < long(gap)) continue;
		last = long(t);

		// Refine in the waveform: block peaks of 32 frames around the analysis frame.
		const size_t block = 32;
		long centre = long(env.toFrame(t));
		size_t from = size_t(std::max(0L, centre - 2 * long(env.hop) - 512)), to = std::min(a.frames, size_t(centre + long(env.hop) * 2 + 32));
		std::vector<float> e;
		for (size_t i = from; i < to; i += block) {
			float m = 0;
			for (size_t j = i; j < std::min(to, i + block); j++) m = std::max(m, std::fabs(mono(a, j)));
			e.push_back(m);
		}
		if (e.empty()) continue;
		size_t pb = size_t(std::max_element(e.begin(), e.end()) - e.begin());
		size_t b = pb;
		while (b > 0 && e[b - 1] > 0.1f * e[pb]) b--;
		size_t frame = from + b * block;
		// Within the first block, find the first sample above 10 % of the block peak.
		for (size_t j = frame; j < std::min(to, frame + block); j++)
			if (std::fabs(mono(a, j)) > 0.1f * e[pb]) { frame = j; break; }
		frame = snapToZeroCrossing(a, frame);
		if (out.empty() || frame > out.back()) out.push_back(frame);
	}
	return out;
}

}
