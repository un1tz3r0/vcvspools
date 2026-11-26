#include "filter.hpp"
#include <algorithm>
#include <cmath>

namespace spools {

static constexpr float kPi = 3.14159265358979f;

void Svf::setup(float g_, float k_) {
	g = g_;
	k = k_;
	a1 = 1 / (1 + g * (g + k));
	a2 = g * a1;
	a3 = g * a2;
}

float cutoffHz(float volts, float sr) {
	return std::clamp(261.6256f * std::exp2(volts), 10.f, 0.49f * sr);
}

void VoiceFilter::set(FilterType t, float volts, float res, float sr) {
	if (t != type) reset();
	type = t;
	if (t == FilterType::Off) return;
	res = std::clamp(res, 0.f, 1.f);
	const float g = std::tan(kPi * cutoffHz(volts, sr) / sr);
	float k0, k1 = 0;
	if (t == FilterType::LP24) {
		k0 = 1.8478f;                       // 2 cos(pi/8)
		k1 = 0.7654f * (1 - 0.97f * res);   // 2 cos(3pi/8), down to Q ~ 43
	}
	else k0 = 2 * (1 - 0.985f * res);       // Q 0.5 .. ~33
	for (auto& ch : s) {
		ch[0].setup(g, k0);
		ch[1].setup(g, k1);
	}
}

void VoiceFilter::reset() {
	for (auto& ch : s)
		for (auto& f : ch) f.reset();
}

}
