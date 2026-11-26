#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "audio.hpp"

namespace spools {

struct Range {
	size_t start = 0, end = 0;  // frames, end exclusive
	size_t length() const { return end > start ? end - start : 0; }
	bool operator==(const Range& o) const { return start == o.start && end == o.end; }
};

enum class TailMode : uint8_t { Inherit, Cut, Ring, Loop, AutoLoop, Stretch };
enum class Origin : uint8_t { Plain, Acid, Rex };

// Per-slice values, summed with the global offsets at playback (DESIGN §6.7).
struct SliceParams {
	float startTrim = 0, endTrim = 0;  // seconds
	float gain = 0;                    // dB
	float pan = 0;                     // -1..1
	float pitch = 0;                   // semitones
	float cutoff = 0, res = 0;         // octaves, 0..1
	float xfade = 0;                   // seconds
	bool reverse = false;
	TailMode tail = TailMode::Inherit;
};

struct Slice {
	size_t start = 0;  // frame in Loop::audio; the slice ends where the next one starts
	double beat = 0;   // position in quarter notes from the loop start
	SliceParams p;
};

// Min/max pyramid for drawing. Level k summarizes buckets of (kBase << k) frames.
struct WaveformMips {
	static constexpr size_t kBase = 16;
	struct Peak { float lo = 0, hi = 0; };
	std::vector<std::vector<Peak>> levels[2];

	void build(const Audio& a);
	Peak peak(int channel, size_t from, size_t to, const Audio& a) const;  // over [from, to)
};

struct CropSpec {
	enum Unit : uint8_t { Samples, Seconds, Beats } unit = Samples;
	double offset = 0, duration = 0;  // duration <= 0 means "to the end of the file"
	Range resolve(const Audio& source, double bpm) const;
};

struct Loop {
	std::shared_ptr<const Audio> source;  // whole decoded file
	Range crop;                           // into source
	std::shared_ptr<const Audio> audio;   // copy of the cropped region, used for playback
	Origin origin = Origin::Plain;
	std::string sourcePath;
	double bpm = 120;     // native tempo of the material
	bool bpmKnown = false;
	int meterNum = 4, meterDen = 4;
	std::vector<Slice> slices;          // sorted, slices[0].start == 0
	size_t regionStart = 0, regionEnd = 0;  // slice indices, inclusive
	WaveformMips mips;

	size_t sliceEnd(size_t i) const { return i + 1 < slices.size() ? slices[i + 1].start : audio->frames; }
	double beats() const { return audio->seconds() * bpm / 60.0; }
	double sliceBeats(size_t i) const { return (i + 1 < slices.size() ? slices[i + 1].beat : beats()) - slices[i].beat; }
	double frameToBeat(size_t f) const { return f / double(audio->sampleRate) * bpm / 60.0; }
};

// Builds a Loop from decoded source audio: copies the crop, computes beats and mips, and fixes up slices.
// `sliceFrames` are in source frames; those outside the crop are dropped and one is always placed at the crop start.
std::shared_ptr<Loop> makeLoop(std::shared_ptr<const Audio> source, Range crop, double bpm, bool bpmKnown,
	const std::vector<size_t>& sliceFrames, Origin origin = Origin::Plain, std::string path = "");

// Loads any supported file (REX2, WAV with loop metadata, plain audio).
std::shared_ptr<Loop> loadLoop(const std::string& path, std::string* error);
std::shared_ptr<Loop> loadLoop(const uint8_t* data, size_t size, const std::string& pathHint, std::string* error);

// Same loop with a new crop, keeping slices (and their params) that fall inside it.
std::shared_ptr<Loop> recrop(const Loop& loop, Range crop);
// Same loop with `count` equal slices; params reset.
std::shared_ptr<Loop> sliceEvenly(const Loop& loop, size_t count);

}
