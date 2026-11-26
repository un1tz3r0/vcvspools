#pragma once
#include <vector>
#include "audio.hpp"

namespace spools {

// Spectral-flux onset strength, one value per hop. Frame t is centred on source frame t * hop.
struct OnsetEnvelope {
	std::vector<float> flux;
	size_t hop = 256;
	float sampleRate = 44100;
	double frameRate() const { return sampleRate / double(hop); }
	size_t toFrame(size_t t) const { return t * hop; }
};

// Mono mix -> Hann STFT -> log(1 + 100 |X|) -> half-wave rectified first difference summed over bins,
// then a high-pass (minus a ~0.1 s moving average) so slow level changes do not read as onsets.
OnsetEnvelope onsetEnvelope(const Audio& audio, size_t fftSize = 1024, size_t hop = 256);

struct OnsetOptions {
	float sensitivity = 0.5f;  // 0 = only the strongest hits, 1 = every bump
	double minGap = 0.05;      // seconds between onsets
};

// Peaks of the envelope above an adaptive threshold (local median + delta). Each peak is refined in
// the waveform to where the hit's level starts rising, then moved back to the preceding zero crossing.
// Returns source frames, ascending.
std::vector<size_t> pickOnsets(const OnsetEnvelope& env, const Audio& audio, const OnsetOptions& opts = {});

// Moves `frame` back (up to `maxBack` frames) to the nearest zero crossing of the mono mix.
size_t snapToZeroCrossing(const Audio& audio, size_t frame, size_t maxBack = 256);

}
