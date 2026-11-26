#pragma once
#include <algorithm>
#include <cmath>

namespace spools {

struct Rgb { float r, g, b; };  // sRGB, 0..1

// OKLCH (lightness 0..1, chroma, hue in turns) to gamma-encoded sRGB, clipped to gamut (Björn Ottosson, 2020).
inline Rgb oklch(float L, float C, float hTurns) {
	const float h = hTurns * 6.2831853f, a = C * std::cos(h), b = C * std::sin(h);
	float l = L + 0.3963377774f * a + 0.2158037573f * b;
	float m = L - 0.1055613458f * a - 0.0638541728f * b;
	float s = L - 0.0894841775f * a - 1.2914855480f * b;
	l = l * l * l; m = m * m * m; s = s * s * s;
	auto enc = [](float x) {
		x = std::clamp(x, 0.f, 1.f);
		return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f;
	};
	return {enc(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
		enc(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
		enc(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s)};
}

// Slice i of n: hues spread evenly around the circle, alternating halves so neighbours differ strongly.
inline float sliceHue(size_t i, size_t n) {
	n = std::max<size_t>(n, 1);
	size_t k = (i % 2) ? (n + 1) / 2 + i / 2 : i / 2;
	return float(k) / float(n);
}

}
