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

}
