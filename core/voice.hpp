#pragma once
#include "envelope.hpp"
#include "filter.hpp"
#include "loop.hpp"
#include "params.hpp"
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
	AdsrTimes ampEnv, filterEnv{0, 0.3f, 0, 0.1f};
	float filterEnvAmount = 0;         // octaves at full envelope, -5..5
	float loopXfade = 0.01f;           // seconds, Loop tail crossfade
	float transientPreserve = 1;       // Stretch: fraction of the attack kept at native timing
	float stretchTail = 1;             // Stretch: fraction of the post-attack audio that may be stretched
	bool retriggerPerNote = false;     // envelopes restart on each slice unless set
	const SincTable* quality = &SincTable::hq();
};

struct VoiceOut {
	float left = 0, right = 0;
	int slice = -1;   // slice now playing (or last played)
	bool trig = false, eoc = false, playing = false;
	float ampEnv = 0, filterEnv = 0;
};

// Loop points for slice i. Loop: its manual points, else from the transient end (a quarter in when features
// are not ready) to the end of the span. AutoLoop: the searched loop, else as Loop without manual points.
SlicePlayer::LoopPoints loopPoints(const Loop& l, size_t i, const SlicePlayer::Span& span, const VoiceParams& vp,
	TailMode tail = TailMode::Loop);

// Stretch-tail plan for slice i to last `samples` engine samples at `rate` source frames per sample.
SlicePlayer::StretchPlan stretchPlan(const Loop& l, size_t i, const SlicePlayer::Span& span, const VoiceParams& vp,
	double samples, double rate);

// One note-mode voice: a phrase sequencer driving two slice players so a new slice can start
// while the previous one fades out underneath it.
class Voice {
public:
	// Note mode: runs this voice's own phrase sequencer.
	VoiceOut process(double beat, double beatPeriod, bool gate, bool rising, int semitones,
		const Loop& loop, const PhraseParams& phrase, const VoiceParams& vp, float engineRate);
	// Any mode: plays what a sequencer decided for this sample.
	VoiceOut play(const SeqEvent& e, double beatPeriod, const Loop& loop, const VoiceParams& vp, float engineRate);
	void reset();
	const SlicePlayer& player() const { return players[cur]; }

private:
	NoteSequencer seq;
	SlicePlayer players[2];
	int cur = 0, lastSlice = -1;
	double slotLeft = 0;  // seconds until a Cut-mode slice is gated off
	float panL[2] = {1, 1}, panR[2] = {1, 1}, rateBase = 0;
	int sliceOf[2] = {-1, -1};               // slice each player is sounding
	Smoother gain[2], pitch, cutoff, res;    // gain is linear
	int controlCountdown = 0, targetCountdown = 0;
	bool released = false;
	// Cached control-rate work: the glide coefficient, the last pitch sent and the last filter setting.
	float glideRate = 0, glideCoef = 0, lastPitch = 0;
	struct { FilterType type = FilterType::Off; float cutoff = -99, res = -1, rate = 0; } filterSet;
	float eventPitch = 0;  // the playing pattern step's pitch offset
	int quiet = 0;  // samples of silence with no player active
	static constexpr int kQuietAfter = 4800;  // the filter's tail at the highest resonance is shorter
	const Loop* loop = nullptr;
	VoiceFilter filter;
	Adsr amp[2], fenv;  // one amp envelope per player, so an outgoing slice keeps its own
	// Continuous parameters glide (5 ms) on a 4-sample control tick; their targets are re-resolved every 32.
	static constexpr int kControlEvery = 4, kTargetEvery = 32;
	static constexpr float kGlide = 0.005f;
	void updateTargets(const Loop& l, const VoiceParams& vp);

	void begin(const SeqEvent& e, double slotSeconds, const Loop& l, const VoiceParams& vp, float engineRate);
	void noteOff(const VoiceParams& vp);
};

}
