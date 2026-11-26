#pragma once
#include "audio.hpp"
#include "sinc.hpp"

namespace spools {

// Plays one span of an Audio buffer at a variable rate, forward or reversed, with click-free starts and stops.
class SlicePlayer {
public:
	struct Span {
		double start = 0, end = 0;  // source frames, end exclusive
		bool reverse = false;
	};

	static constexpr float kDeclick = 0.001f;  // seconds

	// Trims are in seconds (positive moves later) and are clamped to the audio.
	static Span span(const Audio& a, size_t start, size_t end, float startTrim, float endTrim, bool reverse);

	void start(const Audio* audio, Span s, double rate, float engineRate);
	void setRate(double r) { rate = r; }
	void setQuality(const SincTable* t) { sinc = t; }
	// Fades out over `seconds` (at least the declick time) and goes idle.
	void stop(float seconds = kDeclick);

	bool active() const { return audio != nullptr; }
	double position() const { return pos; }  // source frame
	double progress() const;                  // 0..1 through the span
	bool stopping() const { return fadeStep < 0; }

	// Returns false once idle. Writes channel 0/1 (mono files duplicate).
	bool process(float& left, float& right);

private:
	const Audio* audio = nullptr;
	const SincTable* sinc = &SincTable::hq();
	Span s;
	double pos = 0, rate = 1;
	float gain = 0, fadeStep = 0, declickStep = 0, sampleRate = 48000;
	float read(int channel, double x) const;
};

}
