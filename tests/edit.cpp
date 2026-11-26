#include <doctest/doctest.h>
#include "edit.hpp"

using namespace spools;

// 1 s at 48 kHz, 120 BPM (2 beats), four equal slices, region 1..2.
static std::shared_ptr<Loop> four() {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = 48000;
	a->data[0].assign(48000, 0.f);
	auto l = sliceEvenly(*makeLoop(a, {0, 48000}, 120, true, {}), 4);
	auto r = setRegion(*l, 2, 1);
	return std::const_pointer_cast<Loop>(std::shared_ptr<const Loop>(r));
}

TEST_CASE("moving a boundary keeps neighbours a minimum length apart and recomputes beats") {
	auto l = four();
	l->slices[1].p.pitch = 3;
	auto m = moveBoundary(*l, 1, 6000);
	CHECK(m->slices[1].start == 6000);
	CHECK(m->slices[1].beat == doctest::Approx(0.25));
	CHECK(m->slices[1].p.pitch == 3);
	CHECK(m->audio.get() == l->audio.get());
	CHECK(m->mips.get() == l->mips.get());
	CHECK(moveBoundary(*l, 1, 0)->slices[1].start == 240);       // 5 ms after slice 0
	CHECK(moveBoundary(*l, 1, 30000)->slices[1].start == 23760);  // 5 ms before slice 2
	CHECK(moveBoundary(*l, 0, 100)->slices[0].start == 0);
}

TEST_CASE("adding and removing slices keep settings and the region") {
	auto l = four();
	l->slices[1].p.gain = -6;
	auto a = addSlice(*l, 18000);
	REQUIRE(a->slices.size() == 5);
	CHECK(a->slices[2].start == 18000);
	CHECK(a->slices[2].p.gain == -6);
	CHECK(a->regionStart == 1);
	CHECK(a->regionEnd == 3);
	CHECK(addSlice(*l, 12100)->slices.size() == 4);  // too close to an existing boundary

	auto r = removeSlice(*a, 2);
	CHECK(r->slices.size() == 4);
	CHECK(r->regionStart == 1);
	CHECK(r->regionEnd == 2);
	auto r1 = removeSlice(*l, 1);
	CHECK(r1->slices.size() == 3);
	CHECK(r1->regionStart == 0);
	CHECK(r1->regionEnd == 1);
	CHECK(removeSlice(*l, 0)->slices.size() == 4);
}

TEST_CASE("sliceAt and setSliceParams") {
	auto l = four();
	CHECK(sliceAt(*l, 0) == 0);
	CHECK(sliceAt(*l, 11999) == 0);
	CHECK(sliceAt(*l, 12000) == 1);
	CHECK(sliceAt(*l, 47999) == 3);
	SliceParams p;
	p.pan = 0.5f;
	auto s = setSliceParams(*l, {0, 3}, {p, p});
	CHECK(s->slices[0].p.pan == 0.5f);
	CHECK(s->slices[3].p.pan == 0.5f);
	CHECK(s->slices[1].p.pan == 0);
}
