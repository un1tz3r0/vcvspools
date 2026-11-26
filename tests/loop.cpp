#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <thread>
#include "exchange.hpp"
#include "fixtures.hpp"
#include "loop.hpp"
#include "worker.hpp"

using namespace spools;

static std::shared_ptr<Audio> ramp(size_t n, float sr = 1000) {
	auto a = std::make_shared<Audio>();
	a->sampleRate = sr;
	a->channels = 2;
	a->frames = n;
	for (size_t i = 0; i < n; i++) {
		a->data[0].push_back(std::sin(i * 0.37f) * (i % 100) / 100.f);
		a->data[1].push_back(-float(i) / n);
	}
	return a;
}

TEST_CASE("crop units resolve to frames and clamp") {
	auto a = ramp(10000);  // 10 s at 1 kHz
	CHECK(CropSpec{CropSpec::Samples, 100, 400}.resolve(*a, 120) == Range{100, 500});
	CHECK(CropSpec{CropSpec::Seconds, 1.5, 2}.resolve(*a, 120) == Range{1500, 3500});
	CHECK(CropSpec{CropSpec::Beats, 2, 4}.resolve(*a, 120) == Range{1000, 3000});  // 0.5 s per beat
	CHECK(CropSpec{CropSpec::Seconds, 8, 0}.resolve(*a, 120) == Range{8000, 10000});
	CHECK(CropSpec{CropSpec::Seconds, 9, 50}.resolve(*a, 120) == Range{9000, 10000});
	CHECK(CropSpec{CropSpec::Samples, -50, 10}.resolve(*a, 120) == Range{0, 10});
	CHECK(CropSpec{CropSpec::Samples, 20000, 10}.resolve(*a, 120).length() == 1);
}

TEST_CASE("makeLoop copies the crop and places slices and beats") {
	auto a = ramp(10000);
	auto loop = makeLoop(a, {1000, 5000}, 120, true, {500, 1000, 2000, 3000, 6000});
	CHECK(loop->audio->frames == 4000);
	CHECK(loop->audio->data[1][0] == a->data[1][1000]);
	REQUIRE(loop->slices.size() == 3);
	CHECK(loop->slices[1].start == 1000);
	CHECK(loop->slices[2].beat == doctest::Approx(4.0));
	CHECK(loop->sliceEnd(2) == 4000);
	CHECK(loop->beats() == doctest::Approx(8.0));
	CHECK(loop->sliceBeats(2) == doctest::Approx(4.0));
	CHECK(loop->regionEnd == 2);
}

TEST_CASE("waveform mips agree with brute force") {
	auto a = ramp(12345);
	WaveformMips m;
	m.build(*a);
	CHECK(m.levels[0].back().size() == 1);
	for (auto [f, t] : {std::pair<size_t, size_t>{0, 12345}, {3, 9}, {17, 4000}, {1000, 1033}, {12000, 99999}, {50, 50}}) {
		for (int c = 0; c < 2; c++) {
			auto p = m.peak(c, f, t, *a);
			float lo = 1e30f, hi = -1e30f;
			for (size_t i = f; i < std::min<size_t>(t, 12345); i++) { lo = std::min(lo, a->data[c][i]); hi = std::max(hi, a->data[c][i]); }
			if (f >= t) { CHECK(p.lo == 0); continue; }
			CHECK(p.lo == lo);
			CHECK(p.hi == hi);
		}
	}
}

TEST_CASE("loadLoop: plain, Acidized and REX files") {
	std::string err;
	auto plain = loadLoop(fixture("tone_pcm16.wav"), &err);
	REQUIRE(bool(plain));
	CHECK(plain->origin == Origin::Plain);
	CHECK_FALSE(plain->bpmKnown);
	CHECK(plain->slices.size() == 1);

	auto acid = loadLoop(fixture("acid_loop.wav"), &err);
	REQUIRE(bool(acid));
	CHECK(acid->origin == Origin::Acid);
	CHECK(acid->bpm == doctest::Approx(120));
	REQUIRE(acid->slices.size() == 3);
	CHECK(acid->slices[1].start == 11025);
	CHECK(acid->slices[1].beat == doctest::Approx(0.5));
	CHECK(acid->beats() == doctest::Approx(1.0));
	CHECK(acid->regionStart == 0);
	CHECK(acid->regionEnd == 2);

	auto rex = loadLoop(fixture("rex/stereo.rx2"), &err);
	REQUIRE(bool(rex));
	CHECK(rex->origin == Origin::Rex);
	CHECK(rex->bpm == doctest::Approx(240));
	REQUIRE(rex->slices.size() == 4);
	for (int i = 0; i < 4; i++) CHECK(rex->slices[i].beat == doctest::Approx(i));
	CHECK(rex->beats() == doctest::Approx(4));

	CHECK_FALSE(bool(loadLoop(fixture("nope.wav"), &err)));
}

TEST_CASE("recrop keeps slices inside and their params; sliceEvenly") {
	auto loop = makeLoop(ramp(8000), {0, 8000}, 60, true, {2000, 4000, 6000});
	auto edited = std::make_shared<Loop>(*loop);
	edited->slices[2].p.gain = -6;
	auto r = recrop(*edited, {3000, 8000});
	REQUIRE(r->slices.size() == 3);  // new start + 4000 + 6000
	CHECK(r->slices[1].start == 1000);
	CHECK(r->slices[1].p.gain == -6);
	CHECK(r->slices[0].p.gain == 0);
	auto e = sliceEvenly(*loop, 16);
	CHECK(e->slices.size() == 16);
	CHECK(e->slices[8].start == 4000);
	CHECK(e->sliceBeats(3) == doctest::Approx(0.5));
}

TEST_CASE("exchange: publish, acquire, retire") {
	Exchange<int> ex;
	CHECK(ex.acquire() == nullptr);
	auto a = std::make_shared<const int>(1);
	std::weak_ptr<const int> wa = a;
	ex.publish(std::move(a));
	CHECK(*ex.acquire() == 1);
	CHECK(*ex.acquire() == 1);
	ex.publish(std::make_shared<const int>(2));
	ex.publish(std::make_shared<const int>(3));  // 2 is skipped and freed immediately
	CHECK(ex.ownedCount() == 2);
	CHECK(*ex.acquire() == 3);
	CHECK_FALSE(wa.expired());  // retired, waiting for the producer to collect
	ex.collect();
	CHECK(wa.expired());
	CHECK(*ex.latest() == 3);
	CHECK(ex.ownedCount() == 1);
}

TEST_CASE("exchange under concurrent publish and acquire") {
	Exchange<std::vector<int>> ex;
	std::atomic<bool> done{false};
	std::atomic<long> seen{0};
	std::thread audio([&] {
		while (!done) {
			if (auto* v = ex.acquire()) seen += long(v->size()) == (*v)[0] ? 1 : -1000000;
		}
	});
	Worker worker;
	for (int i = 1; i <= 2000; i++)
		worker.post([&, i] { ex.publish(std::make_shared<const std::vector<int>>(i, i)); });
	worker.wait();
	done = true;
	audio.join();
	CHECK(seen > 0);
	ex.acquire();
	ex.collect();
	CHECK(ex.ownedCount() <= 2);
}

TEST_CASE("worker runs jobs in order and drains on wait") {
	std::vector<int> order;
	{
		Worker w;
		for (int i = 0; i < 100; i++) w.post([&, i] { order.push_back(i); });
		w.wait();
		CHECK(order.size() == 100);
	}
	for (int i = 0; i < 100; i++) CHECK(order[i] == i);
}

TEST_CASE("retempo rescales slice beats and marks the tempo known") {
	auto a = std::make_shared<Audio>();
	a->sampleRate = 48000;
	a->channels = 1;
	a->frames = 96000;
	a->data[0].assign(a->frames, 0.f);
	auto l = makeLoop(a, {0, a->frames}, 120, false, {24000, 48000});
	auto r = retempo(*l, 60);
	CHECK(r->bpmKnown);
	CHECK(r->bpm == 60);
	REQUIRE(r->slices.size() == 3);
	CHECK(r->slices[1].beat == doctest::Approx(0.5));
	CHECK(r->beats() == doctest::Approx(2));
	CHECK(l->slices[1].beat == doctest::Approx(1));  // original untouched
}
