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

}
