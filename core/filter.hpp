#pragma once
#include <cstdint>

namespace spools {

enum class FilterType : uint8_t { Off, LP12, BP12, HP12, Notch, LP24 };

// Zero-delay-feedback (TPT) state-variable filter, one channel (Zavalishin, "The Art of VA Filter Design").
struct Svf {
	float g = 0, k = 2, a1 = 0, a2 = 0, a3 = 0;
	float ic1 = 0, ic2 = 0;
	struct Out { float lp, bp, hp; };
	void setup(float g, float k);
	Out process(float x) {
		float v3 = x - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2 * v1 - ic1;
		ic2 = 2 * v2 - ic2;
		return {v2, v1, x - k * v1 - v2};
	}
	void reset() { ic1 = ic2 = 0; }
};

// Per-voice stereo multimode filter. Cutoff is in V/oct around C4 (261.63 Hz), resonance 0..1.
// LP24 cascades two SVFs tuned as a 4-pole Butterworth, with the resonance on the second stage.
struct VoiceFilter {
	FilterType type = FilterType::Off;
	Svf s[2][2];  // [channel][stage]

	void set(FilterType t, float cutoffVolts, float res, float sampleRate);
	float process(int ch, float x) {
		switch (type) {
			case FilterType::LP12: return s[ch][0].process(x).lp;
			case FilterType::BP12: { Svf& f = s[ch][0]; return f.k * f.process(x).bp; }  // unity at the peak
			case FilterType::HP12: return s[ch][0].process(x).hp;
			case FilterType::Notch: { Svf& f = s[ch][0]; Svf::Out o = f.process(x); return o.lp + o.hp; }
			case FilterType::LP24: return s[ch][1].process(s[ch][0].process(x).lp).lp;
			default: return x;
		}
	}
	void reset();
};

float cutoffHz(float volts, float sampleRate);

}
