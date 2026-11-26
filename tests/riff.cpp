#include <doctest/doctest.h>
#include "audio.hpp"
#include "file.hpp"
#include "fixtures.hpp"
#include "riff.hpp"

using namespace spools;

TEST_CASE("reads acid, cue labels and smpl loop written by another tool") {
	std::vector<uint8_t> bytes;
	REQUIRE(readFile(fixture("acid_loop.wav"), bytes));
	LoopMeta m;
	REQUIRE(readWavMeta(bytes.data(), bytes.size(), m));
	CHECK(m.hasAcid);
	CHECK(m.bpm == doctest::Approx(120.0));
	CHECK(m.beats == 2);
	CHECK(m.meterNum == 4);
	CHECK(m.meterDen == 4);
	CHECK(m.rootNote == 60);
	CHECK_FALSE(m.oneShot);
	REQUIRE(m.cues.size() == 3);
	CHECK(m.cues[0].frame == 0);
	CHECK(m.cues[0].label == "a");
	CHECK(m.cues[1].frame == 11025);
	CHECK(m.cues[1].label == "b");
	CHECK(m.cues[2].frame == 16538);
	CHECK(m.hasLoop);
	CHECK(m.loopStart == 0);
	CHECK(m.loopEnd == 22050);
	CHECK(m.spls.empty());
}

TEST_CASE("plain WAV has no metadata; non-WAV is rejected") {
	std::vector<uint8_t> bytes;
	REQUIRE(readFile(fixture("tone_pcm16.wav"), bytes));
	LoopMeta m;
	REQUIRE(readWavMeta(bytes.data(), bytes.size(), m));
	CHECK_FALSE(m.hasAcid);
	CHECK(m.cues.empty());
	CHECK_FALSE(m.hasLoop);
	CHECK(m.rootNote == -1);
	REQUIRE(readFile(fixture("tone.flac"), bytes));
	CHECK_FALSE(readWavMeta(bytes.data(), bytes.size(), m));
}

TEST_CASE("truncated chunks do not read out of bounds") {
	std::vector<uint8_t> bytes;
	REQUIRE(readFile(fixture("acid_loop.wav"), bytes));
	LoopMeta m;
	for (size_t n = 12; n < 300; n++) readWavMeta(bytes.data(), n, m);
}

TEST_CASE("export round-trips audio and metadata") {
	std::string err;
	auto src = decodeAudioFile(fixture("tone_pcm16.wav"), &err);
	REQUIRE(bool(src));
	LoopMeta out;
	out.hasAcid = true;
	out.bpm = 97.5;
	out.beats = 8;
	out.meterNum = 7;
	out.meterDen = 8;
	out.rootNote = 48;
	out.cues = {{0, "kick"}, {2000, ""}, {5000, "snare roll"}};
	out.hasLoop = true;
	out.loopStart = 100;
	out.loopEnd = 9000;
	out.spls = R"({"slices":[1,2,3]})";

	for (auto fmt : {SampleFormat::Float32, SampleFormat::Pcm24, SampleFormat::Pcm16}) {
		CAPTURE(int(fmt));
		auto bytes = writeWav(*src, out, fmt);
		CHECK(bytes.size() % 2 == 0);
		LoopMeta in;
		REQUIRE(readWavMeta(bytes.data(), bytes.size(), in));
		CHECK(in.bpm == doctest::Approx(97.5));
		CHECK(in.beats == 8);
		CHECK(in.meterNum == 7);
		CHECK(in.meterDen == 8);
		CHECK(in.rootNote == 48);
		REQUIRE(in.cues.size() == 3);
		for (int i = 0; i < 3; i++) {
			CHECK(in.cues[i].frame == out.cues[i].frame);
			CHECK(in.cues[i].label == out.cues[i].label);
		}
		CHECK(in.hasLoop);
		CHECK(in.loopStart == 100);
		CHECK(in.loopEnd == 9000);
		CHECK(in.spls == out.spls);

		auto back = decodeAudio(bytes.data(), bytes.size(), sniffFormat(bytes.data(), bytes.size()), &err);
		REQUIRE(bool(back));
		CHECK(back->frames == src->frames);
		CHECK(back->channels == 2);
		float tol = fmt == SampleFormat::Pcm16 ? 1.f / 32767 : 1.f / 8388607 * 2;
		for (size_t i = 0; i < src->frames; i += 13)
			CHECK(std::abs(back->data[1][i] - src->data[1][i]) <= tol);
	}
}

TEST_CASE("export without metadata writes only fmt and data") {
	Audio a;
	a.sampleRate = 48000;
	a.channels = 1;
	a.frames = 3;
	a.data[0] = {0.f, 0.5f, -2.f};
	auto bytes = writeWav(a, LoopMeta{}, SampleFormat::Pcm16);
	CHECK(bytes.size() == 12 + 8 + 16 + 8 + 6);
	std::string err;
	auto back = decodeAudio(bytes.data(), bytes.size(), Format::Wav, &err);
	REQUIRE(bool(back));
	CHECK(back->data[0][2] == doctest::Approx(-1.f).epsilon(1e-4));  // clipped
}
