#pragma once
#include <cmath>

namespace spools {

// Loop-mode transport (DESIGN §6.3). Turns RUN/CONT/BAR into a loop position in beats that restarts at 0
// on every reset. Works on the TimeBase beat, so call it after TimeBase::process on the same sample.
class Transport {
public:
	struct In {
		bool runConnected = false, run = false;  // RUN unpatched = always running
		bool cont = false, bar = false;           // trigger edges this sample
		bool clocked = false, clockEdge = false;  // CONT/BAR wait for the next clock edge when clocked
	};
	struct Out {
		bool running = false;
		bool reset = false;   // the loop restarted at its start slice on this sample
		double loopBeat = 0;  // beats since the last reset
		double barBeat = 0;   // beats since the bar phase was last aligned
	};

	Out process(double beat, const In& in, double barBeats, int barsPerCycle) {
		Out o;
		const bool running = !in.runConnected || in.run;
		if (running && !wasRunning) restart(beat, o);
		wasRunning = running;

		if (in.cont) pendingCont = true;
		if (in.bar) pendingBar = true;
		const bool now = !in.clocked || in.clockEdge;
		if (now && pendingCont) { restart(beat, o); pendingCont = pendingBar = false; }
		if (now && pendingBar) { barOrigin = beat; pendingBar = false; }

		if (barsPerCycle > 0 && barBeats > 0) {  // automatic CONT at the end of each cycle
			const double cycle = barBeats * barsPerCycle;
			if (beat - barOrigin >= cycle - 1e-9) {
				barOrigin += std::floor((beat - barOrigin) / cycle + 1e-9) * cycle;
				origin = barOrigin;
				o.reset = true;
			}
		}
		o.running = running;
		o.loopBeat = beat - origin;
		o.barBeat = beat - barOrigin;
		return o;
	}

	void reset() { wasRunning = pendingCont = pendingBar = false; origin = barOrigin = 0; }

private:
	double origin = 0, barOrigin = 0;
	bool wasRunning = false, pendingCont = false, pendingBar = false;
	void restart(double beat, Out& o) {
		origin = barOrigin = beat;
		o.reset = true;
	}
};

}
