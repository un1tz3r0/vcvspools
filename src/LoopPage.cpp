#include <cmath>
#include "Page.hpp"

using namespace spools;
using namespace theme;

namespace {

constexpr float kHeader = 18, kFooter = 15;

NVGcolor sliceColor(size_t i, float alpha) {
	float h = std::fmod(float(i) * 0.618034f, 1.f);  // golden-ratio hue walk keeps neighbours distinct
	return nvgHSLA(h, 0.62f, 0.60f, uint8_t(alpha * 255));
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

		// Header: file name left, tempo and slice count right.
		std::string name = loop->sourcePath.empty() ? "untitled" : system::getFilename(loop->sourcePath);
		const float hy = ay + 14;
		ctx.text(ax + kPad, hy, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kReadout, kText, name);
		std::string info = string::f("%.1f BPM%s   %zu slice%s   %.2f s", module->currentBpm.load(), loop->bpmKnown ? "" : "?",
			n, n == 1 ? "" : "s", a.seconds());
		if (loop->origin == Origin::Rex) info = "REX (unofficial)   " + info;
		if (module->playMode == Spools::kLoopMode) info = "LOOP   " + info;
		ctx.text(ax + w - kPad, hy, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kReadout, kDim, info);
		std::string err = module->getStatus();
		if (!err.empty()) ctx.text(ax + w / 2, hy, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kBody, kText, "⚠ " + err);

		const float top = ay + kHeader, bottom = ay + h - kFooter, mid = (top + bottom) / 2, amp = (bottom - top) / 2 - 2;
		const float x0 = ax + kPad, x1 = ax + w - kPad, span = x1 - x0;
		auto frameX = [&](double f) { return x0 + float(f / double(a.frames)) * span; };

		// Region shading and slice backgrounds.
		int r0 = 0, rn = int(n);
		{
			PhraseParams p;
			float fs = module->params[Spools::REGION_START_PARAM].getValue(), fe = module->params[Spools::REGION_END_PARAM].getValue();
			if (fs != 0.f || fe != 1.f) { p.regionStart = module->regionIndex(fs, n); p.regionEnd = module->regionIndex(fe, n); }
			p.region(*loop, r0, rn);
		}
		for (size_t i = 0; i < n; i++) {
			float xa = frameX(double(loop->slices[i].start)), xb = frameX(double(loop->sliceEnd(i)));
			bool inRegion = int(i) >= r0 && int(i) < r0 + rn;
			ctx.fillRect(xa, top, xb - xa, bottom - top, sliceColor(i, inRegion ? 0.10f : 0.03f));
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
			bool inRegion = int(si) >= r0 && int(si) < r0 + rn;
			ctx.fillRect(x, mid - clamp(hi, -1.f, 1.f) * amp, 1.f, std::max(1.f, (hi - lo) * amp), sliceColor(si, inRegion ? 0.95f : 0.35f));
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
			ctx.fillRect(frameX(pos) - 1, top, 2, bottom - top, nvgRGBA(0xff, 0xff, 0xff, 0xe0));
			float sa = frameX(double(loop->slices[size_t(s)].start));
			ctx.fillRect(sa, top, frameX(double(loop->sliceEnd(size_t(s)))) - sa, 3, sliceColor(size_t(s), 1.f));
		}
	}
};

}

std::unique_ptr<Page> makeLoopPage() { return std::make_unique<LoopPage>(); }
