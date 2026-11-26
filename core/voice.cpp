#include "voice.hpp"
#include <algorithm>
#include <cmath>
#include "units.hpp"

namespace spools {

void Voice::reset() {
	seq.reset();
	players[0].kill();
	players[1].kill();
	lastSlice = -1;
	filter.reset();
	filterCountdown = 0;
	amp[0].reset();
	amp[1].reset();
	fenv.reset();
}

void Voice::begin(int slice, bool phraseStart, double slotSeconds, const Loop& l, const VoiceParams& vp, float engineRate) {
	const Slice& s = l.slices[size_t(slice)];
	players[cur].stop(std::max(vp.overlap + s.p.xfade, 0.f));
	const int prev = cur;
	cur ^= 1;
	if (phraseStart || !vp.retriggerPerNote) {
		amp[cur].trigger(true);  // a fresh player starts silent anyway; its own declick covers the edge
		fenv.trigger();
	}
	else amp[cur] = amp[prev];  // the note's envelope carries on into the next slice

	auto span = SlicePlayer::span(*l.audio, s.start, l.sliceEnd(size_t(slice)), vp.startTrim + s.p.startTrim,
		vp.endTrim + s.p.endTrim, vp.reverse != s.p.reverse);
	pitch = vp.pitch + s.p.pitch;
	rateBase = l.audio->sampleRate / engineRate;
	players[cur].setQuality(vp.quality);
	players[cur].start(l.audio.get(), span, rateBase * semitonesToRatio(pitch), engineRate);

	float g = dbToGain(vp.gain + s.p.gain), pan = std::clamp(vp.pan + s.p.pan, -1.f, 1.f);
	if (l.audio->channels == 1) {  // equal-power pan, unity at centre
		float a = (pan + 1) * 0.25f * 3.14159265f;
		gainL[cur] = g * std::cos(a) * 1.41421356f;
		gainR[cur] = g * std::sin(a) * 1.41421356f;
	}
	else {  // balance
		gainL[cur] = g * std::min(1.f, 1 - pan);
		gainR[cur] = g * std::min(1.f, 1 + pan);
	}
	TailMode tail = s.p.tail == TailMode::Inherit ? vp.tail : s.p.tail;
	slotLeft = tail == TailMode::Cut ? slotSeconds * std::clamp(vp.duration, 0.01f, 1.f) : -1;
	lastSlice = slice;
	released = false;
	loop = &l;
}

VoiceOut Voice::process(double beat, double beatPeriod, bool gate, bool rising, int semis,
	const Loop& l, const PhraseParams& phrase, const VoiceParams& vp, float engineRate) {
	VoiceOut out;
	if (loop && loop != &l) reset();  // loop replaced: drop players that point at the old audio
	loop = &l;

	SeqEvent e = seq.process(beat, gate, rising, semis, l, phrase);
	if (e.slice >= 0) {
		begin(e.slice, e.phraseStart, e.sliceBeats * beatPeriod, l, vp, engineRate);
		out.trig = true;
	}
	if (e.release && !released) {
		noteOff(vp);
		released = true;
	}
	out.eoc = e.endOfPhrase;

	if (slotLeft >= 0) {
		slotLeft -= 1.0 / engineRate;
		if (slotLeft < 0) noteOff(vp);
	}
	if (vp.pitch + (lastSlice >= 0 ? l.slices[size_t(lastSlice)].p.pitch : 0) != pitch && players[cur].active()) {
		pitch = vp.pitch + l.slices[size_t(lastSlice)].p.pitch;
		players[cur].setRate(rateBase * semitonesToRatio(pitch));
	}

	const float dt = 1.f / engineRate;
	float a, b, l0 = 0, r0 = 0;
	for (int k = 0; k < 2; k++) {
		float env = amp[k].process(vp.ampEnv, dt);
		if (!players[k].active()) continue;
		if (amp[k].idle()) { players[k].kill(); continue; }  // released to -60 dB
		if (!players[k].process(a, b)) continue;
		l0 += a * gainL[k] * env;
		r0 += b * gainR[k] * env;
	}
	const float fe = fenv.process(vp.filterEnv, dt);
	if (--filterCountdown <= 0) {
		const SliceParams* sp = lastSlice >= 0 ? &l.slices[size_t(lastSlice)].p : nullptr;
		filter.set(vp.filter, vp.cutoff + vp.filterEnvAmount * fe + (sp ? sp->cutoff : 0.f), vp.res + (sp ? sp->res : 0.f), engineRate);
		filterCountdown = kFilterEvery;
	}
	out.ampEnv = amp[cur].value();
	out.filterEnv = fe;
	out.left = filter.process(0, l0);
	out.right = filter.process(1, r0);
	out.slice = lastSlice;
	out.playing = players[0].active() || players[1].active();
	return out;
}

// The amp envelope's release replaces the old fixed fade; it is never shorter than the overlap or a declick.
void Voice::noteOff(const VoiceParams& vp) {
	const float fade = std::max(vp.overlap, SlicePlayer::kDeclick);
	if (vp.ampEnv.r < fade) players[cur].stop(fade);
	else amp[cur].release();
	fenv.release();
}

}
