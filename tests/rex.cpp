#include <doctest/doctest.h>
#include <cmath>
#include "audio.hpp"
#include "file.hpp"
#include "fixtures.hpp"
#include "rex.hpp"

using namespace spools;

static RexFile loadRex(const std::string& name) {
	std::vector<uint8_t> bytes;
	REQUIRE(readFile(fixture(name), bytes));
	CHECK(sniffFormat(bytes.data(), bytes.size()) == Format::Rex);
	RexFile rex;
	std::string err;
	INFO(err);
	REQUIRE(readRex(bytes.data(), bytes.size(), rex, &err));
	return rex;
}

TEST_CASE("REX2 decodes losslessly against the encoder's source PCM") {
	for (auto [rx2, wav, channels] : {std::tuple{"rex/mono.rx2", "rex/mono_src.wav", 1}, {"rex/stereo.rx2", "rex/stereo_src.wav", 2}}) {
		CAPTURE(rx2);
		RexFile rex = loadRex(rx2);
		auto src = decodeAudioFile(fixture(wav), nullptr);
		REQUIRE(bool(src));
		REQUIRE(bool(rex.audio));
		CHECK(rex.audio->channels == channels);
		CHECK(rex.audio->sampleRate == 44100.f);
		REQUIRE(rex.audio->frames == src->frames);
		size_t mismatches = 0;
		for (int c = 0; c < channels; c++)
			for (size_t i = 0; i < src->frames; i++)
				mismatches += rex.audio->data[c][i] != src->data[c][i];
		CHECK(mismatches == 0);
	}
}

TEST_CASE("REX2 metadata: tempo, meter, slices") {
	RexFile rex = loadRex("rex/stereo.rx2");
	CHECK(rex.bpm == doctest::Approx(240.0));
	CHECK(rex.bars == 1);
	CHECK(rex.beats == 0);
	CHECK(rex.meterNum == 4);
	CHECK(rex.meterDen == 4);
	CHECK(rex.loopBeats() == doctest::Approx(4.0));
	CHECK(rex.nativeBpm == doctest::Approx(240.0));
	CHECK(rex.bitsPerSample == 16);
	REQUIRE(rex.slices.size() == 4);
	for (uint32_t i = 0; i < 4; i++) {
		CHECK(rex.slices[i].start == i * 11025);
		CHECK(rex.slices[i].length == 11025);
	}
	CHECK(rex.loopStart() == 0);
	CHECK(rex.loopEnd() == 44100);
}

TEST_CASE("corrupt REX input fails cleanly") {
	std::vector<uint8_t> bytes;
	REQUIRE(readFile(fixture("rex/mono.rx2"), bytes));
	RexFile rex;
	std::string err;
	for (size_t n : {size_t(0), size_t(11), size_t(40), size_t(160), bytes.size() / 2}) {
		CAPTURE(n);
		bool ok = readRex(bytes.data(), n, rex, &err);
		if (ok) CHECK(rex.audio->frames > 0);
	}
	auto junk = bytes;
	for (size_t i = 200; i < junk.size(); i += 7) junk[i] ^= 0x5A;
	readRex(junk.data(), junk.size(), rex, &err);  // must not crash or hang
	CHECK_FALSE(readRex(bytes.data() + 4, bytes.size() - 4, rex, &err));
	CHECK(err == "Not a REX2 file");
}
