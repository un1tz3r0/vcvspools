#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spools {

// Planar float audio, 1 or 2 channels, at the file's native sample rate.
struct Audio {
	float sampleRate = 0.f;
	int channels = 0;
	size_t frames = 0;
	std::vector<float> data[2];

	const float* channel(int c) const { return data[c < channels ? c : 0].data(); }
	double seconds() const { return sampleRate > 0.f ? frames / double(sampleRate) : 0.0; }
};

enum class Format { Unknown, Wav, Aiff, W64, Flac, Mp3, Vorbis, Rex };

Format sniffFormat(const uint8_t* data, size_t size, const std::string& pathHint = "");
const char* formatName(Format f);

// Decoders refuse files longer than this, to keep a stray long recording from eating all RAM.
constexpr double kMaxSourceSeconds = 30.0 * 60.0;

// Returns nullptr and sets *error on failure. Files with more than 2 channels keep the first two.
std::shared_ptr<Audio> decodeAudio(const uint8_t* data, size_t size, Format format, std::string* error);
std::shared_ptr<Audio> decodeAudioFile(const std::string& path, std::string* error);

}
