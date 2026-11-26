#include <cmath>
#include "Page.hpp"
#include "core/color.hpp"

using namespace spools;
using namespace theme;

namespace {

constexpr float kHeader = 18, kFooter = 15;

NVGcolor withAlpha(NVGcolor c, float a) { c.a = a; return c; }

// Draws a small outlined label, right-aligned at x; returns its left edge.
float badge(const DisplayContext& ctx, float right, float baseline, const std::string& s, NVGcolor c) {
	nvgFontSize(ctx.vg, kBody);
	float w = nvgTextBounds(ctx.vg, 0, 0, s.c_str(), nullptr, nullptr) + 10;
	nvgBeginPath(ctx.vg);
	nvgRoundedRect(ctx.vg, right - w + 0.5f, baseline - 11.5f, w - 1, 15, 3);
	nvgStrokeColor(ctx.vg, c);
	nvgStrokeWidth(ctx.vg, 1);
	nvgStroke(ctx.vg);
	ctx.text(right - w / 2, baseline, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kBody, c, s);
	return right - w;
}

struct LoopPage : Page {
	const char* title() const override { return "LOOP"; }

	void draw(const DisplayContext& ctx) override {
		NVGcontext* vg = ctx.vg;
		const float ax = ctx.area.pos.x, ay = ctx.area.pos.y, w = ctx.area.size.x, h = ctx.area.size.y;
		Spools* module = ctx.module;
		const auto& loop = ctx.loop;
		if (!loop) {
			std::string msg = !module ? "SPOOLS" : module->loading ? "Loading…" : "Right-click › Load sample…";
			ctx.text(ax + w / 2, ay + h / 2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 16, kText, msg);
			if (module) {
				std::string err = module->getStatus();
				if (!err.empty()) ctx.text(ax + w / 2, ay + h / 2 + 22, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kBody, kDim, err);
			}
			return;
		}
		const Audio& a = *loop->audio;
		const size_t n = loop->slices.size();

		// Slice hues: evenly spaced OKLCH hues at one lightness, so no slice looks louder than another.
		std::vector<NVGcolor> hue(n);
		for (size_t i = 0; i < n; i++) {
			Rgb c = oklch(0.78f, 0.13f, sliceHue(i, n));
			hue[i] = nvgRGBf(c.r, c.g, c.b);
		}

		// Header: file name left; badges, slice count and the BPM readout right.
		std::string name = loop->sourcePath.empty() ? "untitled" : system::getFilename(loop->sourcePath);
		const float hy = ay + 15;
		float right = ax + w - kPad;
		nvgFontSize(vg, 16);
		std::string bpm = string::f("%.1f%s", module->currentBpm.load(), loop->bpmKnown ? "" : "?");
		ctx.text(right, hy, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, 16, kText, bpm);
		right -= nvgTextBounds(vg, 0, 0, bpm.c_str(), nullptr, nullptr) + 4;
		std::string info = string::f("%zu slice%s  %.2f s   BPM", n, n == 1 ? "" : "s", a.seconds());
		nvgFontSize(vg, kReadout);
		ctx.text(right, hy, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kReadout, kDim, info);
		right -= nvgTextBounds(vg, 0, 0, info.c_str(), nullptr, nullptr) + 8;
		if (loop->origin == Origin::Rex) right = badge(ctx, right, hy, "REX unofficial", kAccent) - 6;
		if (module->playMode == Spools::kLoopMode) right = badge(ctx, right, hy, "LOOP MODE", kText) - 6;
		nvgSave(vg);
		nvgScissor(vg, ax, ay, right - ax - 4, kHeader);
		ctx.text(ax + kPad, hy, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kReadout, kText, name);
		nvgRestore(vg);
		std::string err = module->getStatus();
		if (!err.empty()) ctx.text(ax + w / 2, hy, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kBody, kText, "⚠ " + err);

		const float top = ay + kHeader, bottom = ay + h - kFooter, mid = (top + bottom) / 2, amp = (bottom - top) / 2 - 2;
		const float x0 = ax + kPad, x1 = ax + w - kPad, span = x1 - x0;
		auto frameX = [&](double f) { return x0 + float(f / double(a.frames)) * span; };

		int r0 = 0, rn = int(n);
		{
			PhraseParams p;
			float fs = module->params[Spools::REGION_START_PARAM].getValue(), fe = module->params[Spools::REGION_END_PARAM].getValue();
			if (fs != 0.f || fe != 1.f) { p.regionStart = module->regionIndex(fs, n); p.regionEnd = module->regionIndex(fe, n); }
			p.region(*loop, r0, rn);
		}
		auto inRegion = [&](size_t i) { return int(i) >= r0 && int(i) < r0 + rn; };
		for (size_t i = 0; i < n; i++) {
			float xa = frameX(double(loop->slices[i].start)), xb = frameX(double(loop->sliceEnd(i)));
			ctx.fillRect(xa, top, xb - xa, bottom - top, withAlpha(hue[i], inRegion(i) ? 0.10f : 0.03f));
		}

		// Beat and bar grid at the loop's own tempo.
		const double framesPerBeat = a.sampleRate * 60.0 / loop->bpm;
		const int beatsPerBar = std::max(1, loop->meterNum * 4 / std::max(1, loop->meterDen));
		if (framesPerBeat / a.frames * span >= 4)
			for (int b = 1; b * framesPerBeat < a.frames; b++) {
				bool bar = b % beatsPerBar == 0;
				ctx.fillRect(std::round(frameX(b * framesPerBeat)), top, 1, bottom - top, nvgRGBA(0xff, 0xff, 0xff, bar ? 0x38 : 0x14));
			}

		// Waveform: one min/max bar per pixel, coloured by slice.
		size_t si = 0;
		for (float x = x0; x < x1; x += 1.f) {
			size_t f0 = size_t(double(x - x0) / span * a.frames), f1 = std::max(f0 + 1, size_t(double(x + 1 - x0) / span * a.frames));
			while (si + 1 < n && loop->slices[si + 1].start <= f0) si++;
			float lo = 0, hi = 0;
			for (int c = 0; c < a.channels; c++) {
				auto pk = loop->mips.peak(c, f0, f1, a);
				lo = std::min(lo, pk.lo);
				hi = std::max(hi, pk.hi);
			}
			ctx.fillRect(x, mid - clamp(hi, -1.f, 1.f) * amp, 1.f, std::max(1.f, (hi - lo) * amp), withAlpha(hue[si], inRegion(si) ? 0.95f : 0.30f));
		}

		// Region brackets.
		if (rn < int(n)) {
			float ra = frameX(double(loop->slices[size_t(r0)].start)), rb = frameX(double(loop->sliceEnd(size_t(r0 + rn - 1))));
			for (float x : {ra, rb - 2}) ctx.fillRect(x, top, 2, bottom - top, withAlpha(kAccent, 0.9f));
			ctx.fillRect(ra, top, rb - ra, 2, withAlpha(kAccent, 0.9f));
		}

		// Slice boundaries with index labels.
		for (size_t i = 0; i < n; i++) {
			float x = frameX(double(loop->slices[i].start));
			nvgBeginPath(vg);
			nvgMoveTo(vg, x + 0.5f, top);
			nvgLineTo(vg, x + 0.5f, bottom);
			nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x50));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			float next = frameX(double(loop->sliceEnd(i)));
			if (next - x >= 18) ctx.text(x + 3, bottom + 12, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 11, kDim, string::f("%zu", i + 1));
		}

		// Playheads.
		for (int c = 0; c < module->activeChannels; c++) {
			float pos = module->playheads[c];
			int s = module->playingSlice[c];
			if (pos < 0 || s < 0) continue;
			if (size_t(s) >= n) continue;
			ctx.fillRect(frameX(pos) - 1, top, 2, bottom - top, hue[size_t(s)]);
			float sa = frameX(double(loop->slices[size_t(s)].start));
			ctx.fillRect(sa, bottom - 3, frameX(double(loop->sliceEnd(size_t(s)))) - sa, 3, hue[size_t(s)]);
		}
	}
};

}

std::unique_ptr<Page> makeLoopPage() { return std::make_unique<LoopPage>(); }
