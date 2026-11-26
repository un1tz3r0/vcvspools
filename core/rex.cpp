#include "rex.hpp"
#include <algorithm>
#include <cstring>

namespace spools {

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
bool is(const uint8_t* tag, const char* s) { return std::memcmp(tag, s, 4) == 0; }

struct BitReader {
	const uint8_t* p;
	size_t n, pos = 0;
	int left = 0;
	uint8_t cur = 0;
	bool overrun = false;

	int bit() {
		if (!left) {
			if (pos >= n) { overrun = true; return 0; }
			cur = p[pos++];
			left = 8;
		}
		return (cur >> --left) & 1;
	}
	uint32_t bits(int k) {
		uint32_t v = 0;
		while (k-- > 0) v = v << 1 | uint32_t(bit());
		return v;
	}
};

// One DWOP channel: five adaptive predictors (orders 0-4) in a doubled representation,
// chosen per sample by minimum running energy, residual coded as unary quotient + adaptive range remainder.
struct Dwop {
	int32_t S[5] = {};
	uint32_t e[5] = {2560, 2560, 2560, 2560, 2560};
	uint32_t rv = 2;
	int ba = 0;

	// Returns false on a malformed stream.
	bool next(BitReader& br, int32_t& sample) {
		int p = 0;
		for (int i = 1; i < 5; i++) if (e[i] < e[p]) p = i;
		uint32_t cs = (e[p] * 3 + 36) >> 7, acc = 0;
		for (int qc = 7, guard = 0; !br.bit();) {
			acc += cs;
			if (--qc == 0) { cs <<= 2; qc = 7; }
			if (++guard > 50000 || br.overrun) return false;
		}
		int nb = ba;
		if (cs >= rv) {
			while (cs >= rv) {
				rv <<= 1;
				if (!rv) return false;
				nb++;
			}
		}
		else {
			nb++;
			for (uint32_t t = rv;;) {
				rv = t;
				t >>= 1;
				nb--;
				if (cs >= t) break;
			}
		}
		uint32_t ext = nb > 0 ? br.bits(nb) : 0, co = rv - cs;
		uint32_t val = acc + (ext < co ? ext : co + (ext - co) * 2 + uint32_t(br.bit()));
		ba = nb;
		int32_t d = int32_t(val ^ uint32_t(-int32_t(val & 1)));

		// Energy index p is prediction order p: d replaces the p-th difference,
		// lower orders integrate up to the sample, higher orders re-difference.
		int32_t o[5];
		std::memcpy(o, S, sizeof o);
		S[p] = d;
		for (int k = p - 1; k >= 0; k--) S[k] = o[k] + S[k + 1];
		for (int k = p + 1; k < 5; k++) S[k] = S[k - 1] - o[k - 1];

		for (int i = 0; i < 5; i++) e[i] = e[i] + uint32_t(S[i] ^ (S[i] >> 31)) - (e[i] >> 5);
		sample = S[0] >> 1;
		return true;
	}
};

}

double RexFile::loopBeats() const {
	return (bars * meterNum + beats) * 4.0 / std::max(1, meterDen);
}

bool readRex(const uint8_t* data, size_t size, RexFile& out, std::string* error) {
	auto fail = [&](const char* msg) { if (error) *error = msg; return false; };
	out = RexFile{};
	if (size < 12 || !is(data, "CAT ")) return fail("Not a REX2 file");

	int channels = 1, sampleRate = 44100, bytesPerSample = 2;
	uint32_t totalFrames = 0;
	const uint8_t* sdat = nullptr;
	size_t sdatLen = 0;
	std::vector<RexFile::Slice> slices;

	auto walk = [&](auto&& self, size_t off, size_t end) -> void {
		while (off + 8 <= end) {
			const uint8_t* tag = data + off;
			size_t len = be32(data + off + 4);
			if (len > end - off - 8) break;
			const uint8_t* p = data + off + 8;
			if (is(tag, "CAT ") && len >= 4) self(self, off + 12, off + 8 + len);
			else if (is(tag, "GLOB") && len >= 20) {
				out.bars = be16(p + 4);
				out.beats = p[6];
				out.meterNum = p[7] ? p[7] : 4;
				out.meterDen = p[8] ? p[8] : 4;
				out.bpm = be32(p + 16) / 1000.0;
			}
			else if (is(tag, "HEAD") && len >= 6) bytesPerSample = p[5];
			else if (is(tag, "SINF") && len >= 10) {
				if (p[0] == 1 || p[0] == 2) channels = p[0];
				if (be16(p + 4)) sampleRate = be16(p + 4);
				totalFrames = be32(p + 6);
			}
			else if (is(tag, "SLCE") && len >= 8) {
				uint32_t start = be32(p), length = be32(p + 4);
				if (length > 1) slices.push_back({start, length});  // length 1 = transient marker
			}
			else if (is(tag, "SDAT") && !sdat) { sdat = p; sdatLen = len; }
			off += 8 + len + (len & 1);
		}
	};
	walk(walk, 12, size);
	if (!sdat || !sdatLen) return fail("REX file has no audio data");
	if (bytesPerSample != 2 && bytesPerSample != 3) return fail("Unsupported REX sample format");

	size_t frames = totalFrames ? totalFrames : sdatLen * 2;
	if (frames > size_t(kMaxSourceSeconds * sampleRate)) return fail("REX file is too long");

	auto a = std::make_shared<Audio>();
	a->sampleRate = float(sampleRate);
	a->channels = channels;
	for (int c = 0; c < channels; c++) a->data[c].resize(frames);
	const float scale = 1.f / float(bytesPerSample == 3 ? 1 << 23 : 1 << 15);

	BitReader br{sdat, sdatLen};
	Dwop ch[2];
	size_t n = 0;
	for (int32_t l, r; n < frames; n++) {
		if (!ch[0].next(br, l)) break;
		a->data[0][n] = l * scale;
		if (channels == 2) {
			if (!ch[1].next(br, r)) break;
			a->data[1][n] = (l + r) * scale;  // right channel is coded as R - L
		}
	}
	if (n == 0) return fail("REX audio failed to decode");
	for (int c = 0; c < channels; c++) a->data[c].resize(n);
	a->frames = n;

	std::sort(slices.begin(), slices.end(), [](auto& x, auto& y) { return x.start < y.start; });
	slices.erase(std::remove_if(slices.begin(), slices.end(), [&](auto& s) { return s.start >= n; }), slices.end());
	for (auto& s : slices) s.length = uint32_t(std::min<size_t>(s.length, n - s.start));
	if (slices.empty()) slices.push_back({0, uint32_t(n)});

	out.audio = a;
	out.slices = std::move(slices);
	out.bitsPerSample = bytesPerSample * 8;
	double seconds = (out.loopEnd() - out.loopStart()) / double(sampleRate);
	out.nativeBpm = out.loopBeats() > 0 && seconds > 0 ? out.loopBeats() * 60.0 / seconds : out.bpm;
	return true;
}

}
