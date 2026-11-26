#pragma once
#include "filter.hpp"
#include "loop.hpp"
#include "sequencer.hpp"
#include "sliceplayer.hpp"

namespace spools {

// Global (knob + CV) values for one voice; per-slice values are added on top (DESIGN §6.7).
struct VoiceParams {
	float startTrim = 0, endTrim = 0;  // seconds
	float gain = 0;                    // dB
	float pan = 0;                     // -1..1
	float pitch = 0;                   // semitones
	float overlap = 0.005f;            // seconds the outgoing slice keeps sounding under the next
	float duration = 1;                // fraction of the slot a slice is gated for (Cut mode)
	bool reverse = false;
	TailMode tail = TailMode::Ring;
	FilterType filter = FilterType::Off;
	float cutoff = 6;                  // V/oct around C4
	float res = 0;                     // 0..1
	const SincTable* quality = &SincTable::hq();
};

struct VoiceOut {
	float left = 0, right = 0;
	int slice = -1;   // slice now playing (or last played)
	bool trig = false, eoc = false, playing = false;
};

// One note-mode voice: a phrase sequencer driving two slice players so a new slice can start
// while the previous one fades out underneath it.
class Voice {
public:
	VoiceOut process(double beat, double beatPeriod, bool gate, bool rising, int semitones,
		const Loop& loop, const PhraseParams& phrase, const VoiceParams& vp, float engineRate);
	void reset();
	const SlicePlayer& player() const { return players[cur]; }

private:
	NoteSequencer seq;
	SlicePlayer players[2];
	int cur = 0, lastSlice = -1;
	double slotLeft = 0;  // seconds until a Cut-mode slice is gated off
	float gainL[2] = {1, 1}, gainR[2] = {1, 1}, pitch = 0, rateBase = 0;
	bool released = false;
	const Loop* loop = nullptr;
	VoiceFilter filter;
	int filterCountdown = 0;  // coefficients are refreshed every kFilterEvery samples
	static constexpr int kFilterEvery = 4;

	void begin(int slice, double slotSeconds, const Loop& l, const VoiceParams& vp, float engineRate);
};

}
