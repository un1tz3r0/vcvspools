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
	controlCountdown = targetCountdown = 0;
	sliceOf[0] = sliceOf[1] = -1;
	amp[0].reset();
	amp[1].reset();
	fenv.reset();
}

void Voice::begin(int slice, bool phraseStart, double slotSeconds, const Loop& l, const VoiceParams& vp, float engineRate) {
	const Slice& s = l.slices[size_t(slice)];
	const Resolved r = resolve(vp, s.p);
	players[cur].stop(r.fade);
	const int prev = cur;
	cur ^= 1;
	if (phraseStart || !vp.retriggerPerNote) {
		amp[cur].trigger(true);  // a fresh player starts silent anyway; its own declick covers the edge
		fenv.trigger();
	}
	else amp[cur] = amp[prev];  // the note's envelope carries on into the next slice

	auto span = SlicePlayer::span(*l.audio, s.start, l.sliceEnd(size_t(slice)), r.startTrim, r.endTrim, r.reverse);
	pitch.snap(r.pitch);  // a new slice starts at its own pitch; only changes while it plays glide
	gain[cur].snap(dbToGain(r.gain));
	if (sliceOf[0] < 0 && sliceOf[1] < 0) { cutoff.snap(r.cutoff); res.snap(r.res); }  // first note: no sweep in
	rateBase = l.audio->sampleRate / engineRate;
	players[cur].setQuality(vp.quality);
	players[cur].start(l.audio.get(), span, rateBase * semitonesToRatio(r.pitch), engineRate);
	sliceOf[cur] = slice;

	if (l.audio->channels == 1) {  // equal-power pan, unity at centre
		float a = (r.pan + 1) * 0.25f * 3.14159265f;
		panL[cur] = std::cos(a) * 1.41421356f;
		panR[cur] = std::sin(a) * 1.41421356f;
	}
	else {  // balance
		panL[cur] = std::min(1.f, 1 - r.pan);
		panR[cur] = std::min(1.f, 1 + r.pan);
	}
	slotLeft = r.tail == TailMode::Cut ? slotSeconds * std::clamp(vp.duration, 0.01f, 1.f) : -1;
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

	const float dt = 1.f / engineRate;
	const float fe = fenv.process(vp.filterEnv, dt);
	if (--targetCountdown <= 0) {
		updateTargets(l, vp);
		targetCountdown = kTargetEvery;
	}
	if (--controlCountdown <= 0) {
		const float c = Smoother::coef(kGlide, kControlEvery * dt);
		gain[0].step(c);
		gain[1].step(c);
		if (players[cur].active()) players[cur].setRate(rateBase * semitonesToRatio(pitch.step(c)));
		filter.set(vp.filter, cutoff.step(c) + vp.filterEnvAmount * fe, res.step(c), engineRate);
		controlCountdown = kControlEvery;
	}

	float a, b, l0 = 0, r0 = 0;
	for (int k = 0; k < 2; k++) {
		float env = amp[k].process(vp.ampEnv, dt);
		if (!players[k].active()) continue;
		if (amp[k].idle()) { players[k].kill(); continue; }  // released to -60 dB
		if (!players[k].process(a, b)) continue;
		float g = gain[k].value * env;
		l0 += a * panL[k] * g;
		r0 += b * panR[k] * g;
	}
	out.ampEnv = amp[cur].value();
	out.filterEnv = fe;
	out.left = filter.process(0, l0);
	out.right = filter.process(1, r0);
	out.slice = lastSlice;
	out.playing = players[0].active() || players[1].active();
	return out;
}

void Voice::updateTargets(const Loop& l, const VoiceParams& vp) {
	for (int k = 0; k < 2; k++)
		if (sliceOf[k] >= 0 && size_t(sliceOf[k]) < l.slices.size())
			gain[k].target = dbToGain(resolve(vp, l.slices[size_t(sliceOf[k])].p).gain);
	const SliceParams none;
	const SliceParams& sp = lastSlice >= 0 && size_t(lastSlice) < l.slices.size() ? l.slices[size_t(lastSlice)].p : none;
	const Resolved r = resolve(vp, sp);
	pitch.target = r.pitch;
	cutoff.target = r.cutoff;
	res.target = r.res;
}

// The amp envelope's release replaces the old fixed fade; it is never shorter than the overlap or a declick.
void Voice::noteOff(const VoiceParams& vp) {
	const float fade = std::max(vp.overlap, SlicePlayer::kDeclick);
	if (vp.ampEnv.r < fade) players[cur].stop(fade);
	else amp[cur].release();
	fenv.release();
}

}
