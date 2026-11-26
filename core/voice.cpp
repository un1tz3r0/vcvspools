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
	quiet = 0;
	filterSet = {};
}

SlicePlayer::LoopPoints loopPoints(const Loop& l, size_t i, const SlicePlayer::Span& span, const VoiceParams& vp, TailMode tail) {
	const Slice& s = l.slices[i];
	const double sr = l.audio->sampleRate, from = double(s.start);
	SlicePlayer::LoopPoints lp;
	lp.pingPong = s.p.pingPong;
	lp.xfade = vp.loopXfade * sr;
	const bool known = l.features && i < l.features->size();
	if (tail == TailMode::AutoLoop && known && (*l.features)[i].loop.score > 0) {
		lp.start = from + double((*l.features)[i].loop.start);
		lp.end = from + double((*l.features)[i].loop.end);
	}
	else if (tail != TailMode::AutoLoop && s.p.hasLoopPoints()) {
		lp.start = from + s.p.loopStart * sr;
		lp.end = from + s.p.loopEnd * sr;
	}
	else {
		lp.start = known ? from + double((*l.features)[i].transientEnd) : span.start + (span.end - span.start) / 4;
		lp.end = span.end;
	}
	return lp;
}

SlicePlayer::StretchPlan stretchPlan(const Loop& l, size_t i, const SlicePlayer::Span& span, const VoiceParams& vp,
	double samples, double rate) {
	const Slice& s = l.slices[i];
	const double sr = l.audio->sampleRate;
	const SliceFeatures* f = l.features && i < l.features->size() ? &(*l.features)[i] : nullptr;
	const double attackEnd = double(s.start) + (f ? double(f->transientEnd) : 0.25 * double(l.sliceEnd(i) - s.start));
	const double kept = span.start + std::max(0.0, attackEnd - span.start) * std::clamp(vp.transientPreserve, 0.f, 1.f);
	SlicePlayer::StretchPlan p;
	p.to = span.end;
	p.from = std::min(span.end, span.end - std::clamp(vp.stretchTail, 0.f, 1.f) * std::max(0.0, span.end - kept));
	p.outSamples = samples - (p.from - span.start) / rate;
	const bool pitched = f && f->pitch.period > 0 && f->pitch.confidence > 0.5f;
	p.period = pitched ? f->pitch.period : 0;
	// About three periods, kept to 10..100 ms and to whole periods; 40 ms without a pitch.
	p.grain = pitched ? std::max(1.0, std::round(std::clamp(3 * p.period, 0.01 * sr, 0.1 * sr) / p.period)) * p.period : 0.04 * sr;
	p.seed = uint32_t(i * 2654435761u + 1);
	return p;
}

void Voice::begin(const SeqEvent& e, double slotSeconds, const Loop& l, const VoiceParams& vp, float engineRate) {
	const int slice = e.slice;
	const bool phraseStart = e.phraseStart;
	const Slice& s = l.slices[size_t(slice)];
	Resolved r = resolve(vp, s.p);
	eventPitch = e.pitch;
	r.pitch = std::clamp(r.pitch + e.pitch, -48.f, 48.f);
	r.reverse = r.reverse != e.reverse;
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
	send[0].snap(s.p.sendA);
	send[1].snap(s.p.sendB);
	cv[0] = s.p.cvA;
	cv[1] = s.p.cvB;
	velocity[cur] = e.gain;
	gain[cur].snap(dbToGain(r.gain) * e.gain);
	if (sliceOf[0] < 0 && sliceOf[1] < 0) { cutoff.snap(r.cutoff); res.snap(r.res); }  // first note: no sweep in
	rateBase = l.audio->sampleRate / engineRate;
	players[cur].setQuality(vp.quality);
	players[cur].start(l.audio.get(), span, rateBase * semitonesToRatio(r.pitch), engineRate);
	const double gate = slotSeconds * std::clamp(vp.duration * e.duration, 0.05f, 1.f);
	lastPitch = r.pitch;
	quiet = 0;
	if (r.tail == TailMode::Loop || r.tail == TailMode::AutoLoop) players[cur].setLoop(loopPoints(l, size_t(slice), span, vp, r.tail));
	if (r.tail == TailMode::Stretch)
		players[cur].setStretch(stretchPlan(l, size_t(slice), span, vp, gate * engineRate, rateBase * semitonesToRatio(r.pitch)));
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
	// Every mode but Ring is gated at DURATION x the slot; Ring plays until the next slice or the note ends.
	slotLeft = r.tail != TailMode::Ring ? gate : -1;
	lastSlice = slice;
	released = false;
	loop = &l;
}

VoiceOut Voice::process(double beat, double beatPeriod, bool gate, bool rising, int semis,
	const Loop& l, const PhraseParams& phrase, const VoiceParams& vp, float engineRate) {
	if (loop && loop != &l) reset();  // loop replaced: drop players that point at the old audio
	return play(seq.process(beat, gate, rising, semis, l, phrase), beatPeriod, l, vp, engineRate);
}

VoiceOut Voice::play(const SeqEvent& e, double beatPeriod, const Loop& l, const VoiceParams& vp, float engineRate) {
	VoiceOut out;
	if (loop && loop != &l) reset();
	loop = &l;

	if (e.slice >= 0) {
		begin(e, e.sliceBeats * beatPeriod, l, vp, engineRate);
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
	if (quiet >= kQuietAfter && !players[0].active() && !players[1].active()) {
		// Nothing has sounded for a while and the filter has rung out: only the envelope outputs move.
		amp[0].process(vp.ampEnv, dt);
		amp[1].process(vp.ampEnv, dt);
		out.ampEnv = amp[cur].value();
		out.filterEnv = fe;
		out.slice = lastSlice;
		out.cvA = cv[0];
		out.cvB = cv[1];
		controlCountdown = targetCountdown = 0;  // refresh everything on the next sound
		return out;
	}
	if (--targetCountdown <= 0) {
		updateTargets(l, vp);
		targetCountdown = kTargetEvery;
	}
	if (--controlCountdown <= 0) {
		if (engineRate != glideRate) { glideRate = engineRate; glideCoef = Smoother::coef(kGlide, kControlEvery * dt); }
		const float c = glideCoef;
		gain[0].step(c);
		gain[1].step(c);
		send[0].step(c);
		send[1].step(c);
		const float st = pitch.step(c);
		if (players[cur].active() && st != lastPitch) {
			lastPitch = st;
			players[cur].setRate(rateBase * semitonesToRatio(st));
		}
		const float fc = cutoff.step(c) + vp.filterEnvAmount * fe, fr = res.step(c);
		if (vp.filter != filterSet.type || fc != filterSet.cutoff || fr != filterSet.res || engineRate != filterSet.rate) {
			filterSet = {vp.filter, fc, fr, engineRate};
			filter.set(vp.filter, fc, fr, engineRate);
		}
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
	const float mono = 0.5f * (out.left + out.right);
	out.sendA = mono * send[0].value;
	out.sendB = mono * send[1].value;
	out.cvA = cv[0];
	out.cvB = cv[1];
	out.slice = lastSlice;
	out.playing = players[0].active() || players[1].active();
	quiet = !out.playing && std::fabs(out.left) + std::fabs(out.right) < 1e-6f ? quiet + 1 : 0;
	return out;
}

void Voice::updateTargets(const Loop& l, const VoiceParams& vp) {
	for (int k = 0; k < 2; k++)
		if (sliceOf[k] >= 0 && size_t(sliceOf[k]) < l.slices.size())
			gain[k].target = dbToGain(resolve(vp, l.slices[size_t(sliceOf[k])].p).gain) * velocity[k];
	const SliceParams none;
	const SliceParams& sp = lastSlice >= 0 && size_t(lastSlice) < l.slices.size() ? l.slices[size_t(lastSlice)].p : none;
	const Resolved r = resolve(vp, sp);
	pitch.target = std::clamp(r.pitch + eventPitch, -48.f, 48.f);
	send[0].target = std::clamp(sp.sendA, 0.f, 1.f);
	send[1].target = std::clamp(sp.sendB, 0.f, 1.f);
	if (lastSlice >= 0) { cv[0] = std::clamp(sp.cvA, -10.f, 10.f); cv[1] = std::clamp(sp.cvB, -10.f, 10.f); }
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
