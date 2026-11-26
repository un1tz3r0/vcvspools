#include "fft.hpp"
#include <cmath>

namespace spools {

Fft::Fft(size_t size) : n(size), rev(size), twiddle(size / 2) {
	int bits = 0;
	while ((size_t(1) << bits) < n) bits++;
	for (size_t i = 0; i < n; i++) {
		size_t r = 0;
		for (int b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
		rev[i] = r;
	}
	for (size_t k = 0; k < n / 2; k++) twiddle[k] = std::polar(1.f, float(-2 * 3.14159265358979323846 * double(k) / double(n)));
}

void Fft::forward(std::vector<std::complex<float>>& x) const {
	for (size_t i = 0; i < n; i++)
		if (i < rev[i]) std::swap(x[i], x[rev[i]]);
	for (size_t len = 2; len <= n; len <<= 1) {
		size_t step = n / len;
		for (size_t i = 0; i < n; i += len)
			for (size_t k = 0; k < len / 2; k++) {
				auto t = twiddle[k * step] * x[i + k + len / 2];
				x[i + k + len / 2] = x[i + k] - t;
				x[i + k] += t;
			}
	}
}

}
