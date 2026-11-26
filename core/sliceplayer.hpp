#pragma once
#include "audio.hpp"
#include <cstdint>
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

	struct LoopPoints {
		double start = 0, end = 0;  // source frames inside the span
		bool pingPong = false;
		double xfade = 0;           // frames; forward loops only
	};

	// Granular stretch/squeeze: from source frame `from`, the rest of the span up to `to` is played as
	// overlapping Hann grains of `grain` source frames, spread over `outSamples` engine samples. Grains keep
	// the playback rate, so pitch is unchanged. With a period, grain starts stay in phase with each other.
	struct StretchPlan {
		double from = 0, to = 0, outSamples = 0, grain = 0, period = 0;
		uint32_t seed = 1;
	};

	void start(const Audio* audio, Span s, double rate, float engineRate);
	// Forward spans only; returns false (and plays normally) when the region or the time is too short.
	bool setStretch(const StretchPlan& plan);
	bool stretching() const { return stretchOn; }
	// Loops [start, end) until stop(); forward spans only. Call right after start().
	void setLoop(LoopPoints lp);
	bool looping() const { return loopOn; }
	void setRate(double r) { if (r != rate) { rate = r; band = sinc->band(r); } }
	void setQuality(const SincTable* t) { sinc = t; band = t->band(rate); }
	// Fades out over `seconds` (at least the declick time) and goes idle.
	void stop(float seconds = kDeclick);

	void kill() { audio = nullptr; }  // immediate, for when the audio itself goes away
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
	LoopPoints lp;
	bool loopOn = false;
	double dir = 1;
	float gain = 0, fadeStep = 0, declickStep = 0, sampleRate = 48000;
	int band = 0;  // sinc cutoff for the current rate
	void read(double x, float& l, float& r) const;  // both channels (r = l for mono)

	struct Grain { double src = 0, done = 0; bool active = false, first = false, last = false; };
	StretchPlan st;
	Grain grains[2];
	bool stretchOn = false, inGrains = false, spawnDone = false;
	double outDone = 0, nextSpawn = 0;
	int newest = 0;
	uint32_t rng = 1;
	void spawn();
	void processGrains(float& l, float& r);
};

}
