#include <doctest/doctest.h>
#include <vector>
#include "sequencer.hpp"

using namespace spools;

// 8 slices of one beat each (8 s at 60 BPM, 100 Hz "audio").
static std::shared_ptr<Loop> eightBeats() {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 100;
	a->channels = 1;
	a->frames = 800;
	a->data[0].assign(800, 0.f);
	return sliceEvenly(*makeLoop(a, {0, 800}, 60, true, {}), 8);
}

struct Hit { double beat; SeqEvent e; };

// Steps time in 1/100 beat; gate is high for beats in [gateOn, gateOff).
static std::vector<Hit> play(NoteSequencer& s, const Loop& loop, const PhraseParams& p, double gateOn, double gateOff,
	double until, int semis = 0) {
	std::vector<Hit> hits;
	bool prev = false;
	for (int i = 0; i <= int(until * 100); i++) {
		double b = i / 100.0;
		bool g = b >= gateOn - 1e-9 && b < gateOff - 1e-9;
		SeqEvent e = s.process(b, g, g && !prev, semis, loop, p);
		prev = g;
		if (e.slice >= 0 || e.endOfPhrase || e.release) hits.push_back({b, e});
	}
	return hits;
}

static std::vector<int> slices(const std::vector<Hit>& h) {
	std::vector<int> v;
	for (auto& x : h) if (x.e.slice >= 0) v.push_back(x.e.slice);
	return v;
}

TEST_CASE("a trigger plays `length` slices from start + offset, on the beat") {
	auto loop = eightBeats();
	NoteSequencer s;
	PhraseParams p;
	p.startSlice = 2;
	p.length = 3;
	auto h = play(s, *loop, p, 0.5, 0.55, 6);
	REQUIRE(h.size() == 4);
	CHECK(slices(h) == std::vector<int>{2, 3, 4});
	CHECK(h[0].e.phraseStart);
	CHECK(h[1].beat == doctest::Approx(1.5));
	CHECK(h[2].beat == doctest::Approx(2.5));
	CHECK(h[3].e.release);
	CHECK(h[3].e.endOfPhrase);
	CHECK(h[3].beat == doctest::Approx(3.5));
	CHECK_FALSE(s.playing());
}

TEST_CASE("semitone offsets wrap around the region, both ways") {
	auto loop = eightBeats();
	PhraseParams p;
	p.startSlice = 1;
	p.length = 2;
	NoteSequencer s;
	CHECK(slices(play(s, *loop, p, 0, 0.1, 3, 6)) == std::vector<int>{7, 0});
	CHECK(slices(play(s, *loop, p, 0, 0.1, 3, -3)) == std::vector<int>{6, 7});
	auto narrowed = std::make_shared<Loop>(*loop);
	narrowed->regionStart = 2;
	narrowed->regionEnd = 4;
	p.startSlice = 0;  // clamps to the region start
	CHECK(slices(play(s, *narrowed, p, 0, 0.1, 4, 4)) == std::vector<int>{3, 4});
	p.regionStart = 5;  // explicit region overrides the loop's, reversed bounds are swapped
	p.regionEnd = 3;
	CHECK(slices(play(s, *narrowed, p, 0, 0.1, 4, 2)) == std::vector<int>{5, 3});
}

TEST_CASE("held gate repeats the phrase up to loop count; release finishes the slice") {
	auto loop = eightBeats();
	PhraseParams p;
	p.length = 2;
	p.loopCount = 3;
	NoteSequencer s;
	auto h = play(s, *loop, p, 0, 100, 20);
	CHECK(slices(h) == std::vector<int>{0, 1, 0, 1, 0, 1});
	int eocs = 0;
	for (auto& x : h) eocs += x.e.endOfPhrase;
	CHECK(eocs == 3);
	CHECK(h.back().e.release);
	CHECK(h.back().beat == doctest::Approx(6));

	p.loopCount = 0;
	p.length = 4;
	h = play(s, *loop, p, 0, 5.3, 20);  // released during the 6th slice
	CHECK(slices(h) == std::vector<int>{0, 1, 2, 3, 0, 1});
	CHECK(h.back().e.release);
	CHECK(h.back().beat == doctest::Approx(6));
}

TEST_CASE("directions") {
	auto loop = eightBeats();
	PhraseParams p;
	p.startSlice = 2;
	p.length = 3;
	p.loopCount = 2;
	NoteSequencer s;
	p.direction = Direction::Reverse;
	CHECK(slices(play(s, *loop, p, 0, 100, 10)) == std::vector<int>{2, 1, 0, 2, 1, 0});
	p.direction = Direction::PingPong;
	CHECK(slices(play(s, *loop, p, 0, 100, 10)) == std::vector<int>{2, 3, 4, 4, 3, 2});
	p.direction = Direction::Random;
	p.loopCount = 20;
	for (int x : slices(play(s, *loop, p, 0, 100, 80))) CHECK((x >= 2 && x <= 4));
}

TEST_CASE("launch quantize delays the start to the next division") {
	auto loop = eightBeats();
	PhraseParams p;
	p.launchQuantize = 1.0;
	NoteSequencer s;
	auto h = play(s, *loop, p, 0.3, 0.35, 3);
	REQUIRE(!h.empty());
	CHECK(h[0].beat == doctest::Approx(1.0));
	h = play(s, *loop, p, 2.0, 2.05, 4);  // already on the grid: immediate
	CHECK(h[0].beat == doctest::Approx(2.0));
}

TEST_CASE("retrigger restarts the phrase; tempo is the caller's beat rate") {
	auto loop = eightBeats();
	PhraseParams p;
	p.length = 4;
	NoteSequencer s;
	std::vector<int> got;
	bool prev = false;
	for (int i = 0; i <= 600; i++) {
		double b = i / 100.0;
		bool g = (b < 0.05) || (b >= 2.5 && b < 2.55);
		SeqEvent e = s.process(b, g, g && !prev, g && b > 1 ? 5 : 0, *loop, p);
		prev = g;
		if (e.slice >= 0) got.push_back(e.slice);
	}
	CHECK(got == std::vector<int>{0, 1, 2, 5, 6, 7, 0});
}
