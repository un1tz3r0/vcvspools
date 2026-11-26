#include "riff.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace spools {

namespace {

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
float f32(const uint8_t* p) { uint32_t u = u32(p); float f; std::memcpy(&f, &u, 4); return f; }

// Calls fn(tag, payload, size) for each chunk in [p, end), honouring the RIFF pad byte.
template <typename F>
void eachChunk(const uint8_t* p, const uint8_t* end, F fn) {
	while (end - p >= 8) {
		uint32_t n = u32(p + 4);
		const uint8_t* body = p + 8;
		size_t avail = size_t(end - body);
		fn(p, body, std::min<size_t>(n, avail));
		if (n >= avail) break;
		p = body + n + (n & 1);
	}
}

bool is(const uint8_t* tag, const char* s) { return std::memcmp(tag, s, 4) == 0; }

struct Writer {
	std::vector<uint8_t> b;
	void tag(const char* s) { b.insert(b.end(), s, s + 4); }
	void u16(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
	void bytes(const void* p, size_t n) { auto c = static_cast<const uint8_t*>(p); b.insert(b.end(), c, c + n); }
	size_t open(const char* t) { tag(t); u32(0); return b.size(); }
	void close(size_t start) {
		uint32_t n = uint32_t(b.size() - start);
		for (int i = 0; i < 4; i++) b[start - 4 + i] = uint8_t(n >> (8 * i));
		if (n & 1) b.push_back(0);
	}
};

}

bool readWavMeta(const uint8_t* data, size_t size, LoopMeta& meta) {
	meta = LoopMeta{};
	if (size < 12 || !is(data, "RIFF") || !is(data + 8, "WAVE")) return false;
	std::map<uint32_t, uint32_t> cueFrames;
	std::map<uint32_t, std::string> labels;
	uint32_t loopStart = 0, loopEndIncl = 0;

	eachChunk(data + 12, data + size, [&](const uint8_t* tag, const uint8_t* p, size_t n) {
		if (is(tag, "acid") && n >= 24) {
			uint32_t flags = u32(p);
			meta.hasAcid = true;
			meta.oneShot = flags & 0x01;
			meta.rootNote = (flags & 0x02) ? int(u16(p + 4)) : -1;
			meta.beats = u32(p + 12);
			meta.meterDen = u16(p + 16);
			meta.meterNum = u16(p + 18);
			meta.bpm = f32(p + 20);
		}
		else if (is(tag, "cue ") && n >= 4) {
			uint32_t count = std::min<uint32_t>(u32(p), uint32_t((n - 4) / 24));
			for (uint32_t i = 0; i < count; i++) {
				const uint8_t* c = p + 4 + 24 * i;
				cueFrames[u32(c)] = u32(c + 20);  // dwSampleOffset; dwPosition is unreliable across writers
			}
		}
		else if (is(tag, "LIST") && n >= 4 && is(p, "adtl")) {
			eachChunk(p + 4, p + n, [&](const uint8_t* t, const uint8_t* q, size_t m) {
				if (is(t, "labl") && m >= 4)
					labels[u32(q)] = std::string(reinterpret_cast<const char*>(q + 4), strnlen(reinterpret_cast<const char*>(q + 4), m - 4));
			});
		}
		else if (is(tag, "smpl") && n >= 36) {
			if (meta.rootNote < 0) meta.rootNote = int(u32(p + 12));
			if (u32(p + 28) > 0 && n >= 60) {
				loopStart = u32(p + 36 + 8);
				loopEndIncl = u32(p + 36 + 12);
				meta.hasLoop = loopEndIncl >= loopStart;
			}
		}
		else if (is(tag, "spls")) {
			meta.spls.assign(reinterpret_cast<const char*>(p), n);
		}
	});

	for (auto& [id, frame] : cueFrames) meta.cues.push_back({frame, labels.count(id) ? labels[id] : ""});
	std::stable_sort(meta.cues.begin(), meta.cues.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
	if (meta.hasLoop) { meta.loopStart = loopStart; meta.loopEnd = loopEndIncl + 1; }
	return true;
}

std::vector<uint8_t> writeWav(const Audio& a, const LoopMeta& meta, SampleFormat format) {
	const uint16_t ch = uint16_t(a.channels), bits = format == SampleFormat::Pcm16 ? 16 : format == SampleFormat::Pcm24 ? 24 : 32;
	const uint16_t align = uint16_t(ch * bits / 8);
	const uint32_t sr = uint32_t(std::lround(a.sampleRate));
	Writer w;
	size_t riff = w.open("RIFF");
	w.tag("WAVE");

	size_t c = w.open("fmt ");
	w.u16(format == SampleFormat::Float32 ? 3 : 1);
	w.u16(ch); w.u32(sr); w.u32(sr * align); w.u16(align); w.u16(bits);
	if (format == SampleFormat::Float32) w.u16(0);
	w.close(c);
	if (format == SampleFormat::Float32) { c = w.open("fact"); w.u32(uint32_t(a.frames)); w.close(c); }

	if (meta.hasAcid) {
		c = w.open("acid");
		w.u32((meta.oneShot ? 0x01 : 0) | (meta.rootNote >= 0 ? 0x02 : 0));
		w.u16(uint16_t(meta.rootNote >= 0 ? meta.rootNote : 60)); w.u16(0x8000); w.f32(0.f);
		w.u32(meta.beats); w.u16(meta.meterDen); w.u16(meta.meterNum); w.f32(float(meta.bpm));
		w.close(c);
	}
	if (!meta.cues.empty()) {
		c = w.open("cue ");
		w.u32(uint32_t(meta.cues.size()));
		for (size_t i = 0; i < meta.cues.size(); i++) {
			w.u32(uint32_t(i + 1)); w.u32(meta.cues[i].frame); w.tag("data"); w.u32(0); w.u32(0); w.u32(meta.cues[i].frame);
		}
		w.close(c);
		size_t list = w.open("LIST");
		w.tag("adtl");
		for (size_t i = 0; i < meta.cues.size(); i++) {
			size_t l = w.open("labl");
			w.u32(uint32_t(i + 1));
			w.bytes(meta.cues[i].label.c_str(), meta.cues[i].label.size() + 1);
			w.close(l);
		}
		w.close(list);
	}
	if (meta.hasLoop || meta.rootNote >= 0) {
		c = w.open("smpl");
		w.u32(0); w.u32(0); w.u32(uint32_t(std::lround(1e9 / sr)));
		w.u32(uint32_t(meta.rootNote >= 0 ? meta.rootNote : 60)); w.u32(0); w.u32(0); w.u32(0);
		w.u32(meta.hasLoop ? 1 : 0); w.u32(0);
		if (meta.hasLoop) { w.u32(0); w.u32(0); w.u32(meta.loopStart); w.u32(meta.loopEnd - 1); w.u32(0); w.u32(0); }
		w.close(c);
	}
	if (!meta.spls.empty()) { c = w.open("spls"); w.bytes(meta.spls.data(), meta.spls.size()); w.close(c); }

	c = w.open("data");
	w.b.reserve(w.b.size() + a.frames * align);
	for (size_t i = 0; i < a.frames; i++)
		for (int k = 0; k < ch; k++) {
			float x = a.data[k][i];
			if (format == SampleFormat::Float32) { w.f32(x); continue; }
			x = std::clamp(x, -1.f, 1.f);
			if (format == SampleFormat::Pcm16) { w.u16(uint16_t(int16_t(std::lround(x * 32767.f)))); continue; }
			int32_t v = int32_t(std::lround(x * 8388607.f));
			w.b.push_back(uint8_t(v)); w.b.push_back(uint8_t(v >> 8)); w.b.push_back(uint8_t(v >> 16));
		}
	w.close(c);
	w.close(riff);
	return std::move(w.b);
}

}
