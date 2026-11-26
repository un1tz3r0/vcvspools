#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "audio.hpp"

namespace spools {

// Unofficial REX2 (.rx2) reader: IFF container + DWOP lossless codec, from the public
// reverse-engineered description in schwung-rex (MIT). Not affiliated with Reason Studios.
struct RexFile {
	std::shared_ptr<Audio> audio;  // whole decoded buffer, including the lead-in before the first slice
	struct Slice { uint32_t start, length; };
	std::vector<Slice> slices;  // playable slices only (transient markers dropped), sorted
	double bpm = 0;          // playback tempo stored in GLOB
	double nativeBpm = 0;    // tempo implied by loop length in beats over slice audio length
	int bars = 0, beats = 0, meterNum = 4, meterDen = 4;
	int bitsPerSample = 16;

	double loopBeats() const;  // loop length in quarter notes
	uint32_t loopStart() const { return slices.empty() ? 0 : slices.front().start; }
	uint32_t loopEnd() const { return slices.empty() ? 0 : slices.back().start + slices.back().length; }
};

bool readRex(const uint8_t* data, size_t size, RexFile& out, std::string* error);

}
