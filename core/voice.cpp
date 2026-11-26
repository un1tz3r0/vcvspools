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
}

void Voice::begin(int slice, double slotSeconds, const Loop& l, const VoiceParams& vp, float engineRate) {
	const Slice& s = l.slices[size_t(slice)];
	players[cur].stop(std::max(vp.overlap + s.p.xfade, 0.f));
	cur ^= 1;

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
		begin(e.slice, e.sliceBeats * beatPeriod, l, vp, engineRate);
		out.trig = true;
	}
	if (e.release && !released) {
		players[cur].stop(std::max(vp.overlap, SlicePlayer::kDeclick));
		released = true;
	}
	out.eoc = e.endOfPhrase;

	if (slotLeft >= 0) {
		slotLeft -= 1.0 / engineRate;
		if (slotLeft < 0) players[cur].stop(std::max(vp.overlap, SlicePlayer::kDeclick));
	}
	if (vp.pitch + (lastSlice >= 0 ? l.slices[size_t(lastSlice)].p.pitch : 0) != pitch && players[cur].active()) {
		pitch = vp.pitch + l.slices[size_t(lastSlice)].p.pitch;
		players[cur].setRate(rateBase * semitonesToRatio(pitch));
	}

	float a, b, l0 = 0, r0 = 0;
	for (int k = 0; k < 2; k++) {
		if (!players[k].process(a, b)) continue;
		l0 += a * gainL[k];
		r0 += b * gainR[k];
	}
	out.left = l0;
	out.right = r0;
	out.slice = lastSlice;
	out.playing = players[0].active() || players[1].active();
	return out;
}

}
