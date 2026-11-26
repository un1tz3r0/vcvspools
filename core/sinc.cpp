#include "sinc.hpp"
#include <cmath>

namespace spools {

constexpr float SincTable::kCutoffs[];

SincTable::SincTable(int taps) : n(taps), table(size_t(kNumCutoffs) * (kPhases + 1) * taps) {
	const double pi = 3.14159265358979323846;
	for (int c = 0; c < kNumCutoffs; c++)
		for (int p = 0; p <= kPhases; p++) {
			float* k = &table[(size_t(c) * (kPhases + 1) + p) * n];
			double frac = double(p) / kPhases, sum = 0;
			for (int t = 0; t < n; t++) {
				double x = (t - (n / 2 - 1)) - frac;  // distance from the read position
				double w = 0.42 + 0.5 * std::cos(pi * x / (n / 2)) + 0.08 * std::cos(2 * pi * x / (n / 2));
				double s = x == 0 ? 1.0 : std::sin(pi * kCutoffs[c] * x) / (pi * x) / kCutoffs[c];
				k[t] = float(std::fabs(x) >= n / 2 ? 0 : w * s);
				sum += k[t];
			}
			for (int t = 0; t < n; t++) k[t] = float(k[t] / sum);  // unity DC gain
		}
}

const float* SincTable::kernel(double frac, double rate) const {
	int c = 0;
	double want = rate > 1 ? 1 / rate : 1;
	while (c + 1 < kNumCutoffs && kCutoffs[c] > want + 1e-6) c++;
	int p = int(frac * kPhases + 0.5);
	return &table[(size_t(c) * (kPhases + 1) + p) * n];
}

const SincTable& SincTable::hq() { static const SincTable t(16); return t; }
const SincTable& SincTable::eco() { static const SincTable t(8); return t; }

}
