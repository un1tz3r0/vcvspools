#pragma once
#include <algorithm>
#include <cmath>
#include "loop.hpp"

namespace spools {

// One parameter as it reaches the DSP: global knob + CV·attenuverter (done by the caller) + the slice's own
// offset, clamped to the range the DSP accepts (DESIGN §6.7).
struct Resolved {
	float startTrim, endTrim;  // seconds
	float gain;                // dB
	float pan, pitch, cutoff, res;
	float fade;                // seconds the outgoing slice keeps sounding (overlap + per-slice xfade)
	bool reverse;
	TailMode tail;
};

template <class G>
Resolved resolve(const G& g, const SliceParams& s) {
	return {
		std::clamp(g.startTrim + s.startTrim, -1.f, 1.f),
		std::clamp(g.endTrim + s.endTrim, -1.f, 1.f),
		std::clamp(g.gain + s.gain, -90.f, 12.f),
		std::clamp(g.pan + s.pan, -1.f, 1.f),
		std::clamp(g.pitch + s.pitch, -48.f, 48.f),
		std::clamp(g.cutoff + s.cutoff, -6.f, 8.f),
		std::clamp(g.res + s.res, 0.f, 1.f),
		std::clamp(g.overlap + s.xfade, 0.f, 0.25f),
		g.reverse != s.reverse,
		s.tail == TailMode::Inherit ? g.tail : s.tail,
	};
}

// One-pole glide toward a target, stepped at a fixed control rate.
struct Smoother {
	float value = 0, target = 0;
	void snap(float v) { value = target = v; }
	float step(float coef) { return value += (target - value) * coef; }
	static float coef(float tauSeconds, float stepSeconds) { return 1.f - std::exp(-stepSeconds / tauSeconds); }
};

}
