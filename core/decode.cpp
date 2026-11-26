// All third-party decoders are compiled into this one translation unit with internal linkage,
// so their symbols can never clash with another plugin's copy.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define DRWAV_API static
#define DRWAV_PRIVATE static
#define DRFLAC_API static
#define DRFLAC_PRIVATE static
#define DRMP3_API static
#define DRMP3_PRIVATE static
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_FLAC_NO_STDIO
#define DR_MP3_NO_STDIO
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <dr_libs/dr_wav.h>
#include <dr_libs/dr_flac.h>
#include <dr_libs/dr_mp3.h>
#include <stb/stb_vorbis.c>
#undef L
#undef R
#undef C
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "audio.hpp"
#include "file.hpp"
#include <cstring>
#include <functional>

namespace spools {

Format sniffFormat(const uint8_t* d, size_t n, const std::string& pathHint) {
	auto at = [&](size_t off, const char* tag) { return n >= off + 4 && std::memcmp(d + off, tag, 4) == 0; };
	static const uint8_t w64[8] = {0x72, 0x69, 0x66, 0x66, 0x2E, 0x91, 0xCF, 0x11};
	if ((at(0, "RIFF") || at(0, "RIFX") || at(0, "RF64")) && at(8, "WAVE")) return Format::Wav;
	if (at(0, "FORM") && (at(8, "AIFF") || at(8, "AIFC"))) return Format::Aiff;
	if (n >= 8 && std::memcmp(d, w64, 8) == 0) return Format::W64;
	if (at(0, "fLaC")) return Format::Flac;
	if (at(0, "OggS")) return Format::Vorbis;
	if (at(0, "CAT ") && at(8, "REX2")) return Format::Rex;
	if (n >= 3 && std::memcmp(d, "ID3", 3) == 0) return Format::Mp3;
	if (n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) return Format::Mp3;
	std::string ext = fileExtension(pathHint);
	if (ext == "rex" || ext == "rx2" || ext == "rcy") return Format::Rex;
	if (ext == "mp3") return Format::Mp3;
	return Format::Unknown;
}

const char* formatName(Format f) {
	switch (f) {
		case Format::Wav: return "WAV";
		case Format::Aiff: return "AIFF";
		case Format::W64: return "Wave64";
		case Format::Flac: return "FLAC";
		case Format::Mp3: return "MP3";
		case Format::Vorbis: return "Ogg Vorbis";
		case Format::Rex: return "REX";
		default: return "unknown";
	}
}

using Reader = std::function<size_t(float* interleaved, size_t frames)>;

// Pulls interleaved float chunks from a decoder into planar stereo-or-mono Audio.
static std::shared_ptr<Audio> pull(int channels, double sampleRate, size_t framesHint, const Reader& read, std::string* error) {
	auto fail = [&](const std::string& msg) { if (error) *error = msg; return std::shared_ptr<Audio>(); };
	if (channels < 1 || sampleRate <= 0) return fail("Unsupported channel count or sample rate");
	const size_t maxFrames = size_t(kMaxSourceSeconds * sampleRate);
	if (framesHint > maxFrames) return fail("File is longer than 30 minutes");

	auto a = std::make_shared<Audio>();
	a->sampleRate = float(sampleRate);
	a->channels = channels > 1 ? 2 : 1;
	for (int c = 0; c < a->channels; c++) a->data[c].reserve(framesHint);

	constexpr size_t kChunk = 4096;
	std::vector<float> buf(kChunk * channels);
	for (size_t got; (got = read(buf.data(), kChunk)) > 0;) {
		for (int c = 0; c < a->channels; c++)
			for (size_t i = 0; i < got; i++) a->data[c].push_back(buf[i * channels + c]);
		if (a->data[0].size() > maxFrames) return fail("File is longer than 30 minutes");
	}
	a->frames = a->data[0].size();
	if (a->frames == 0) return fail("File contains no audio");
	return a;
}

std::shared_ptr<Audio> decodeAudio(const uint8_t* data, size_t size, Format format, std::string* error) {
	auto fail = [&](const std::string& msg) { if (error) *error = msg; return std::shared_ptr<Audio>(); };
	switch (format) {
		case Format::Wav:
		case Format::Aiff:
		case Format::W64: {
			drwav w;
			if (!drwav_init_memory(&w, data, size, nullptr)) return fail(std::string("Invalid ") + formatName(format) + " file");
			auto a = pull(w.channels, w.sampleRate, size_t(w.totalPCMFrameCount),
				[&](float* p, size_t n) { return size_t(drwav_read_pcm_frames_f32(&w, n, p)); }, error);
			drwav_uninit(&w);
			return a;
		}
		case Format::Flac: {
			drflac* f = drflac_open_memory(data, size, nullptr);
			if (!f) return fail("Invalid FLAC file");
			auto a = pull(f->channels, f->sampleRate, size_t(f->totalPCMFrameCount),
				[&](float* p, size_t n) { return size_t(drflac_read_pcm_frames_f32(f, n, p)); }, error);
			drflac_close(f);
			return a;
		}
		case Format::Mp3: {
			drmp3 m;
			if (!drmp3_init_memory(&m, data, size, nullptr)) return fail("Invalid MP3 file");
			auto a = pull(m.channels, m.sampleRate, 0,
				[&](float* p, size_t n) { return size_t(drmp3_read_pcm_frames_f32(&m, n, p)); }, error);
			drmp3_uninit(&m);
			return a;
		}
		case Format::Vorbis: {
			int err = 0;
			stb_vorbis* v = stb_vorbis_open_memory(data, int(size), &err, nullptr);
			if (!v) return fail("Invalid Ogg Vorbis file");
			stb_vorbis_info info = stb_vorbis_get_info(v);
			auto a = pull(info.channels, info.sample_rate, stb_vorbis_stream_length_in_samples(v),
				[&](float* p, size_t n) {
					return size_t(stb_vorbis_get_samples_float_interleaved(v, info.channels, p, int(n * info.channels)));
				}, error);
			stb_vorbis_close(v);
			return a;
		}
		case Format::Rex: return fail("REX files are loaded by the REX importer");
		default: return fail("Unrecognized audio format");
	}
}

std::shared_ptr<Audio> decodeAudioFile(const std::string& path, std::string* error) {
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes, error)) return nullptr;
	return decodeAudio(bytes.data(), bytes.size(), sniffFormat(bytes.data(), bytes.size(), path), error);
}

}
