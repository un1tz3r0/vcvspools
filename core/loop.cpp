#include "loop.hpp"
#include <algorithm>
#include <cmath>
#include "file.hpp"
#include "rex.hpp"
#include "riff.hpp"

namespace spools {

void WaveformMips::build(const Audio& a) {
	for (int c = 0; c < 2; c++) levels[c].clear();
	for (int c = 0; c < a.channels; c++) {
		std::vector<Peak> lvl((a.frames + kBase - 1) / kBase);
		for (size_t b = 0; b < lvl.size(); b++) {
			auto first = a.data[c].begin() + b * kBase, last = a.data[c].begin() + std::min(a.frames, (b + 1) * kBase);
			auto [lo, hi] = std::minmax_element(first, last);
			lvl[b] = {*lo, *hi};
		}
		while (!lvl.empty()) {
			levels[c].push_back(lvl);
			if (lvl.size() == 1) break;
			std::vector<Peak> up((lvl.size() + 1) / 2);
			for (size_t b = 0; b < up.size(); b++) {
				const Peak& x = lvl[2 * b];
				const Peak& y = 2 * b + 1 < lvl.size() ? lvl[2 * b + 1] : x;
				up[b] = {std::min(x.lo, y.lo), std::max(x.hi, y.hi)};
			}
			lvl = std::move(up);
		}
	}
}

WaveformMips::Peak WaveformMips::peak(int c, size_t from, size_t to, const Audio& a) const {
	c = c < a.channels ? c : 0;
	to = std::min(to, a.frames);
	if (from >= to) return {};
	Peak p{1e30f, -1e30f};
	auto raw = [&](size_t i, size_t j) {
		for (; i < j; i++) { p.lo = std::min(p.lo, a.data[c][i]); p.hi = std::max(p.hi, a.data[c][i]); }
	};
	// Coarsest level whose buckets fit at least twice in the span; ragged edges are read raw.
	size_t span = to - from;
	int k = -1;
	while (k + 1 < int(levels[c].size()) && (kBase << (k + 1)) * 2 <= span) k++;
	if (k < 0) { raw(from, to); return p; }
	size_t bs = kBase << k, b0 = (from + bs - 1) / bs, b1 = to / bs;
	raw(from, std::min(to, b0 * bs));
	for (size_t b = b0; b < b1; b++) { p.lo = std::min(p.lo, levels[c][k][b].lo); p.hi = std::max(p.hi, levels[c][k][b].hi); }
	raw(std::max(from, b1 * bs), to);
	return p;
}

Range CropSpec::resolve(const Audio& src, double bpm) const {
	double toFrames = unit == Samples ? 1.0 : unit == Seconds ? src.sampleRate : src.sampleRate * 60.0 / std::max(bpm, 1e-6);
	auto clampFrames = [&](double f) { return size_t(std::clamp(std::llround(f), 0LL, (long long)src.frames)); };
	size_t s = clampFrames(offset * toFrames);
	size_t e = duration > 0 ? clampFrames(s + duration * toFrames) : src.frames;
	if (e <= s) e = std::min(src.frames, s + 1);
	if (s >= src.frames) s = src.frames - 1;
	return {s, e};
}

std::shared_ptr<Loop> makeLoop(std::shared_ptr<const Audio> source, Range crop, double bpm, bool bpmKnown,
	const std::vector<size_t>& sliceFrames, Origin origin, std::string path) {
	auto loop = std::make_shared<Loop>();
	crop.end = std::min(crop.end, source->frames);
	if (crop.start >= crop.end) crop = {0, source->frames};
	auto audio = std::make_shared<Audio>();
	audio->sampleRate = source->sampleRate;
	audio->channels = source->channels;
	audio->frames = crop.length();
	for (int c = 0; c < source->channels; c++)
		audio->data[c].assign(source->data[c].begin() + crop.start, source->data[c].begin() + crop.end);

	loop->source = std::move(source);
	loop->crop = crop;
	loop->audio = audio;
	loop->origin = origin;
	loop->sourcePath = std::move(path);
	loop->bpm = bpm > 0 ? bpm : 120.0;
	loop->bpmKnown = bpm > 0 && bpmKnown;

	std::vector<size_t> starts{0};
	for (size_t f : sliceFrames)
		if (f > crop.start && f < crop.end) starts.push_back(f - crop.start);
	std::sort(starts.begin(), starts.end());
	starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
	for (size_t s : starts) loop->slices.push_back({s, loop->frameToBeat(s), {}});
	loop->regionStart = 0;
	loop->regionEnd = loop->slices.size() - 1;
	loop->mips.build(*audio);
	return loop;
}

std::shared_ptr<Loop> loadLoop(const uint8_t* data, size_t size, const std::string& path, std::string* error) {
	Format fmt = sniffFormat(data, size, path);
	if (fmt == Format::Rex) {
		RexFile rex;
		if (!readRex(data, size, rex, error)) return nullptr;
		std::vector<size_t> starts;
		for (auto& s : rex.slices) starts.push_back(s.start);
		auto loop = makeLoop(rex.audio, {rex.loopStart(), rex.loopEnd()}, rex.nativeBpm, true, starts, Origin::Rex, path);
		loop->meterNum = rex.meterNum;
		loop->meterDen = rex.meterDen;
		return loop;
	}
	auto audio = decodeAudio(data, size, fmt, error);
	if (!audio) return nullptr;
	LoopMeta meta;
	if (fmt != Format::Wav || !readWavMeta(data, size, meta) || (!meta.hasAcid && meta.cues.empty()))
		return makeLoop(audio, {0, audio->frames}, 0, false, {}, Origin::Plain, path);

	std::vector<size_t> starts;
	for (auto& c : meta.cues) starts.push_back(c.frame);
	double bpm = meta.hasAcid && meta.bpm > 0 ? meta.bpm : 0;
	if (bpm <= 0 && meta.hasAcid && meta.beats > 0) bpm = meta.beats * 60.0 / audio->seconds();
	auto loop = makeLoop(audio, {0, audio->frames}, bpm, bpm > 0, starts, Origin::Acid, path);
	if (meta.hasAcid) { loop->meterNum = meta.meterNum ? meta.meterNum : 4; loop->meterDen = meta.meterDen ? meta.meterDen : 4; }
	if (meta.hasLoop && meta.loopEnd > meta.loopStart) {
		auto first = [&](size_t frame) {  // index of the slice containing `frame`
			size_t i = 0;
			while (i + 1 < loop->slices.size() && loop->slices[i + 1].start <= frame) i++;
			return i;
		};
		loop->regionStart = first(meta.loopStart);
		loop->regionEnd = first(meta.loopEnd - 1);
	}
	return loop;
}

std::shared_ptr<Loop> loadLoop(const std::string& path, std::string* error) {
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes, error)) return nullptr;
	return loadLoop(bytes.data(), bytes.size(), path, error);
}

std::shared_ptr<Loop> recrop(const Loop& old, Range crop) {
	std::vector<size_t> starts;
	for (auto& s : old.slices) starts.push_back(s.start + old.crop.start);
	auto loop = makeLoop(old.source, crop, old.bpm, old.bpmKnown, starts, old.origin, old.sourcePath);
	loop->meterNum = old.meterNum;
	loop->meterDen = old.meterDen;
	for (auto& s : loop->slices)
		for (auto& o : old.slices)
			if (o.start + old.crop.start == s.start + loop->crop.start) s.p = o.p;
	return loop;
}

std::shared_ptr<Loop> sliceEvenly(const Loop& old, size_t count) {
	count = std::max<size_t>(1, count);
	std::vector<size_t> starts;
	for (size_t i = 0; i < count; i++) starts.push_back(old.crop.start + old.audio->frames * i / count);
	auto loop = makeLoop(old.source, old.crop, old.bpm, old.bpmKnown, starts, old.origin, old.sourcePath);
	loop->meterNum = old.meterNum;
	loop->meterDen = old.meterDen;
	return loop;
}

}
