#pragma once
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include "loop.hpp"

namespace spools {

// A position or duration typed by the user: "1.5s", "250ms", "4b", "2 bars", "44100" (default unit).
struct Length {
	CropSpec::Unit unit = CropSpec::Samples;
	double value = 0;  // bars are stored as beats
	double frames(double sampleRate, double bpm) const;
};

std::optional<Length> parseLength(const std::string& text, CropSpec::Unit defaultUnit, int beatsPerBar = 4);
std::string formatLength(size_t frames, CropSpec::Unit unit, double sampleRate, double bpm);

// Averages the intervals of up to 8 recent taps; a pause longer than 2 s starts over.
struct TapTempo {
	static constexpr int kMax = 8;
	double times[kMax] = {};
	int count = 0;
	// Returns the tempo so far, or 0 until there are two taps.
	double tap(double seconds) {
		if (count > 0 && seconds - times[(count - 1) % kMax] > 2.0) count = 0;
		times[count % kMax] = seconds;
		count++;
		int n = std::min(count, kMax);
		if (n < 2) return 0;
		double span = times[(count - 1) % kMax] - times[(count - n) % kMax];
		return span > 0 ? 60.0 * (n - 1) / span : 0;
	}
};

}
