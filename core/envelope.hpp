#pragma once
#include <cmath>

namespace spools {

struct AdsrTimes {
	float a = 0, d = 0.5f, s = 1, r = 0;  // seconds, except sustain (0..1)
};

// ADSR with exponential segments. Attack aims past 1 so it curves like an analog charge and still lands on
// time; decay and release reach their target to -60 dB in the set time. A retrigger restarts the attack from
// the current level.
class Adsr {
public:
	enum Stage { Idle, Attack, Decay, Sustain, Release };

	void trigger(bool fromZero = false) {
		if (fromZero) level = 0;
		stage = Attack;
	}
	void release() { if (stage != Idle) stage = Release; }
	void reset() { stage = Idle; level = 0; }
	Stage state() const { return stage; }
	bool idle() const { return stage == Idle; }
	float value() const { return level; }

	float process(const AdsrTimes& t, float dt) {
		switch (stage) {
			case Attack:
				if (t.a <= dt) level = 1;
				else level = kAttackTarget + (level - kAttackTarget) * coef(t.a, dt, kAttackSpan);
				if (level >= 1) { level = 1; stage = Decay; }
				break;
			case Decay:
			case Sustain: {
				float s = t.s < 0 ? 0.f : t.s > 1 ? 1.f : t.s;
				if (stage == Sustain && level == s) break;  // settled: nothing to compute
				level = t.d <= dt ? s : s + (level - s) * coef(t.d, dt, kSpan);
				if (std::fabs(level - s) < 1e-4f) { level = s; stage = Sustain; }
				else stage = Decay;
				break;
			}
			case Release:
				level = t.r <= dt ? 0 : level * coef(t.r, dt, kSpan);
				if (level < 1e-3f) { level = 0; stage = Idle; }
				break;
			default: break;
		}
		return level;
	}

private:
	static constexpr float kAttackTarget = 1.2f;
	static constexpr float kAttackSpan = 1.7917595f;  // ln(1.2 / 0.2)
	static constexpr float kSpan = 6.9077553f;        // ln(1000)
	Stage stage = Idle;
	float level = 0;
	// Segment coefficient, recomputed only when the time, rate or segment changes (exp per sample adds up).
	float cSeconds = -1, cDt = 0, cSpan = 0, cValue = 0;
	float coef(float seconds, float dt, float span) {
		if (seconds != cSeconds || dt != cDt || span != cSpan) {
			cSeconds = seconds, cDt = dt, cSpan = span;
			cValue = std::exp(-span * dt / seconds);
		}
		return cValue;
	}
};

}
