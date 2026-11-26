#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "audio.hpp"

namespace spools {

// Loop metadata carried in Acidized/sampler WAV chunks: acid, cue + LIST/adtl/labl, smpl, and Spools' own spls.
struct LoopMeta {
	bool hasAcid = false;
	double bpm = 0;
	uint32_t beats = 0;
	int rootNote = -1;  // MIDI note, -1 = unset
	bool oneShot = false;
	uint16_t meterNum = 4, meterDen = 4;

	struct Cue { uint32_t frame; std::string label; };
	std::vector<Cue> cues;  // sorted by frame

	bool hasLoop = false;
	uint32_t loopStart = 0, loopEnd = 0;  // frames, end exclusive (smpl stores it inclusive)

	std::string spls;  // opaque Spools payload (JSON), empty if absent
};

// Parses metadata chunks from a RIFF/WAVE file. Returns false if the bytes are not RIFF/WAVE.
bool readWavMeta(const uint8_t* data, size_t size, LoopMeta& meta);

enum class SampleFormat { Pcm16, Pcm24, Float32 };

// Serializes audio and metadata as a RIFF/WAVE file.
std::vector<uint8_t> writeWav(const Audio& audio, const LoopMeta& meta, SampleFormat format = SampleFormat::Float32);

}
