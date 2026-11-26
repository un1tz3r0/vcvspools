#pragma once
#include <algorithm>
#include <cmath>

namespace spools {

// Beat clock in quarter notes. With a clock patched, tempo comes from pulse spacing and the
// phase is locked to pulses (never running past the next expected pulse). Otherwise it free-runs at the fallback BPM.
class TimeBase {
	int ppqn = 1;
	double fallbackBpm = 120;
	double period = 0.5;    // seconds per quarter note
	double recent[3] = {};  // last pulse intervals, newest first
	int intervals = 0;
	double sinceEdge = 0;
	long edges = 0;
	double beat = 0, beatAtEdge = 0;
	bool wasClocked = false;

	void setPeriod(double interval) {
		recent[2] = recent[1]; recent[1] = recent[0]; recent[0] = interval;
		intervals = std::min(intervals + 1, 3);
		double est = interval;
		if (intervals == 3) {
			double a = recent[0], b = recent[1], c = recent[2];
			est = std::max(std::min(a, b), std::min(std::max(a, b), c));  // median of three
		}
		double guess = est * ppqn;
		if (std::fabs(interval * ppqn - period) > 0.25 * period) {  // tempo jump: follow at once
			guess = interval * ppqn;
			recent[1] = recent[2] = interval;
			intervals = 1;
		}
		period = guess;
	}

public:
	void setPpqn(int p) { ppqn = std::max(1, p); }
	void setFallbackBpm(double bpm) { fallbackBpm = std::clamp(bpm, 1.0, 1000.0); }
	int getPpqn() const { return ppqn; }

	void reset() { beat = beatAtEdge = 0; edges = 0; sinceEdge = 0; }

	// Call once per sample.
	void process(bool clockConnected, bool clockEdge, double dt) {
		if (!clockConnected) {
			if (wasClocked) { intervals = 0; edges = 0; }
			wasClocked = false;
			period = 60.0 / fallbackBpm;
			beat += dt / period;
			return;
		}
		if (!wasClocked) { wasClocked = true; edges = 0; intervals = 0; period = 60.0 / fallbackBpm; }
		sinceEdge += dt;
		if (clockEdge) {
			if (edges > 0) setPeriod(sinceEdge);
			// Snap to the pulse grid: edge n sits at n/ppqn beats past the first edge we saw after a reset.
			beat = beatAtEdge = edges == 0 ? beat : beatAtEdge + 1.0 / ppqn;
			edges++;
			sinceEdge = 0;
			return;
		}
		if (edges == 0) { beat += dt / period; return; }
		beat = beatAtEdge + std::min(sinceEdge / period, 0.999 / ppqn);
	}

	double beatPeriod() const { return period; }
	double bpm() const { return 60.0 / period; }
	double phase() const { return beat; }
	bool clocked() const { return wasClocked && edges > 1; }
};

}
