#pragma once
#include <vector>

namespace spools {

// Blackman-windowed sinc interpolation kernels, precomputed for a few cutoffs so that
// reading faster than 1:1 is band-limited. taps must be even.
class SincTable {
public:
	static constexpr int kPhases = 2048;
	static constexpr float kCutoffs[] = {1.f, 0.85f, 0.7f, 0.5f, 0.35f, 0.25f};
	static constexpr int kNumCutoffs = sizeof(kCutoffs) / sizeof(kCutoffs[0]);

	explicit SincTable(int taps);
	int taps() const { return n; }

	// Coefficients for fractional offset `frac` in [0, 1) at read rate `rate` (source frames per output sample).
	// Tap k weights source frame floor(x) - taps/2 + 1 + k.
	const float* kernel(double frac, double rate) const;

	static const SincTable& hq();   // 16 taps
	static const SincTable& eco();  // 8 taps

private:
	int n;
	std::vector<float> table;  // [cutoff][phase][tap]
};

}
