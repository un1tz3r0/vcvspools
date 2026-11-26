#include <doctest/doctest.h>
#include <cmath>
#include "audio.hpp"
#include "file.hpp"
#include "fixtures.hpp"

using namespace spools;

static double rms(const std::vector<float>& x, size_t from, size_t to) {
	double s = 0;
	for (size_t i = from; i < to; i++) s += double(x[i]) * x[i];
	return std::sqrt(s / double(to - from));
}

static std::shared_ptr<Audio> load(const std::string& name) {
	std::string err;
	auto a = decodeAudioFile(fixture(name), &err);
	INFO(name << ": " << err);
	REQUIRE(bool(a));
	return a;
}

TEST_CASE("lossless formats decode sample-accurately") {
	const size_t n = 11025;
	for (const char* name : {"tone_pcm16.wav", "tone_pcm24.wav", "tone_f32.wav", "tone.aiff", "tone.flac"}) {
		CAPTURE(name);
		auto a = load(name);
		CHECK(a->sampleRate == 44100.f);
		CHECK(a->channels == 2);
		CHECK(a->frames == n);
		double tol = std::string(name).find("f32") != std::string::npos ? 1e-6 : 1.0 / 16384;
		for (size_t i = 0; i < n; i += 97) {
			double t = i / 44100.0;
			CHECK(std::fabs(a->data[0][i] - 0.5 * std::sin(2 * M_PI * 440 * t)) < tol);
			CHECK(std::fabs(a->data[1][i] - 0.25 * std::sin(2 * M_PI * 660 * t)) < tol);
		}
	}
}

TEST_CASE("lossy formats decode with the right shape and level") {
	for (const char* name : {"tone.mp3", "tone.ogg"}) {
		CAPTURE(name);
		auto a = load(name);
		CHECK(a->sampleRate == 44100.f);
		CHECK(a->channels == 2);
		CHECK(a->frames == doctest::Approx(11025).epsilon(0.2));
		size_t lo = a->frames / 4, hi = a->frames * 3 / 4;
		CHECK(rms(a->data[0], lo, hi) == doctest::Approx(0.5 / std::sqrt(2)).epsilon(0.1));
		CHECK(rms(a->data[1], lo, hi) == doctest::Approx(0.25 / std::sqrt(2)).epsilon(0.1));
	}
}

TEST_CASE("mono stays mono, extra channels are dropped") {
	auto m = load("tone_mono_48k.wav");
	CHECK(m->channels == 1);
	CHECK(m->sampleRate == 48000.f);
	CHECK(m->frames == 12000);
	CHECK(m->channel(1) == m->channel(0));

	auto q = load("tone_quad.wav");
	auto s = load("tone_pcm16.wav");
	CHECK(q->channels == 2);
	CHECK(q->data[0] == s->data[0]);
	CHECK(q->data[1] == s->data[1]);
}

TEST_CASE("format sniffing") {
	auto sniff = [](const char* bytes, size_t n, const char* path = "") {
		return sniffFormat(reinterpret_cast<const uint8_t*>(bytes), n, path);
	};
	CHECK(sniff("RIFF\0\0\0\0WAVE", 12) == Format::Wav);
	CHECK(sniff("FORM\0\0\0\0AIFC", 12) == Format::Aiff);
	CHECK(sniff("fLaC", 4) == Format::Flac);
	CHECK(sniff("OggS", 4) == Format::Vorbis);
	CHECK(sniff("ID3\3", 4) == Format::Mp3);
	CHECK(sniff("CAT \0\0\0\0REX2", 12) == Format::Rex);
	CHECK(sniff("????", 4, "/x/Loop.RCY") == Format::Rex);
	CHECK(sniff("????", 4, "/x.y/noext") == Format::Unknown);
}

TEST_CASE("decode errors are reported") {
	std::string err;
	CHECK_FALSE(bool(decodeAudioFile(fixture("missing.wav"), &err)));
	CHECK(err.find("Cannot open") == 0);
	const uint8_t junk[16] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
	CHECK_FALSE(bool(decodeAudio(junk, sizeof junk, Format::Wav, &err)));
	CHECK(err == "Invalid WAV file");
	CHECK_FALSE(bool(decodeAudio(junk, sizeof junk, Format::Unknown, &err)));
}

TEST_CASE("file helpers") {
	CHECK(fileExtension("a/b/Loop.WAV") == "wav");
	CHECK(fileExtension("a.dir/file") == "");
	std::vector<uint8_t> data{1, 2, 3}, back;
	std::string path = std::string(SPOOLS_FIXTURES) + "/../../build/host/io-test.bin";
	REQUIRE(writeFile(path, data.data(), data.size()));
	REQUIRE(readFile(path, back));
	CHECK(back == data);
}
