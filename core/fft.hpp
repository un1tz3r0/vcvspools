#pragma once
#include <complex>
#include <vector>

namespace spools {

// In-place iterative radix-2 complex FFT for offline analysis. N must be a power of two.
class Fft {
public:
	explicit Fft(size_t n);
	size_t size() const { return n; }
	void forward(std::vector<std::complex<float>>& x) const;

private:
	size_t n;
	std::vector<size_t> rev;
	std::vector<std::complex<float>> twiddle;
};

}
