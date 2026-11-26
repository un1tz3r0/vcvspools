#include "Display.hpp"
#include <cmath>

using namespace spools;

namespace {

constexpr float kHeader = 24, kFooter = 16, kPad = 8;
const NVGcolor kBg = nvgRGB(0x0e, 0x0e, 0x10), kText = nvgRGB(0xe8, 0xe8, 0xea), kDim = nvgRGB(0xa8, 0xa8, 0xae);

NVGcolor sliceColor(size_t i, float alpha) {
	float h = std::fmod(float(i) * 0.618034f, 1.f);  // golden-ratio hue walk keeps neighbours distinct
	return nvgHSLA(h, 0.62f, 0.60f, uint8_t(alpha * 255));
}

void text(NVGcontext* vg, float x, float y, int align, float size, NVGcolor c, const std::string& s) {
	nvgFontSize(vg, size);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, c);
	nvgText(vg, x, y, s.c_str(), nullptr);
}

}

void SpoolsDisplay::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 1) return Widget::drawLayer(args, layer);
	NVGcontext* vg = args.vg;
	const float w = box.size.x, h = box.size.y;
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0, 0, w, h, 6);
	nvgFillColor(vg, kBg);
	nvgFill(vg);

	std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
	if (!font) return;
	nvgFontFaceId(vg, font->handle);

	std::shared_ptr<const Loop> loop = module ? module->loop() : nullptr;
	if (!loop) {
		std::string msg = !module ? "SPOOLS" : module->loading ? "Loading…" : "Right-click › Load sample…";
		text(vg, w / 2, h / 2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 16, kText, msg);
		if (module) {
			std::string err = module->getStatus();
			if (!err.empty()) text(vg, w / 2, h / 2 + 22, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 12, kDim, err);
		}
		return;
	}

	const Audio& a = *loop->audio;
	const size_t n = loop->slices.size();

	// Header: file name left, tempo and slice count right.
	std::string name = loop->sourcePath.empty() ? "untitled" : system::getFilename(loop->sourcePath);
	text(vg, kPad, 16, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 13, kText, name);
	std::string info = string::f("%.1f BPM%s   %zu slice%s   %.2f s", module->currentBpm.load(), loop->bpmKnown ? "" : "?",
		n, n == 1 ? "" : "s", a.seconds());
	if (loop->origin == Origin::Rex) info = "REX (unofficial)   " + info;
	text(vg, w - kPad, 16, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, 13, kDim, info);
	std::string err = module->getStatus();
	if (!err.empty()) text(vg, w / 2, 16, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 12, kText, "⚠ " + err);

	const float top = kHeader, bottom = h - kFooter, mid = (top + bottom) / 2, amp = (bottom - top) / 2 - 2;
	const float x0 = kPad, x1 = w - kPad, span = x1 - x0;
	auto frameX = [&](double f) { return x0 + float(f / double(a.frames)) * span; };

	// Region shading and slice backgrounds.
	int r0 = 0, rn = int(n);
	if (module) {
		PhraseParams p;
		float fs = module->params[Spools::REGION_START_PARAM].getValue(), fe = module->params[Spools::REGION_END_PARAM].getValue();
		if (fs != 0.f || fe != 1.f) { p.regionStart = module->regionIndex(fs, n); p.regionEnd = module->regionIndex(fe, n); }
		p.region(*loop, r0, rn);
	}
	for (size_t i = 0; i < n; i++) {
		float xa = frameX(double(loop->slices[i].start)), xb = frameX(double(loop->sliceEnd(i)));
		bool inRegion = int(i) >= r0 && int(i) < r0 + rn;
		nvgBeginPath(vg);
		nvgRect(vg, xa, top, xb - xa, bottom - top);
		nvgFillColor(vg, sliceColor(i, inRegion ? 0.10f : 0.03f));
		nvgFill(vg);
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
		nvgBeginPath(vg);
		nvgRect(vg, x, mid - clamp(hi, -1.f, 1.f) * amp, 1.f, std::max(1.f, (hi - lo) * amp));
		nvgFillColor(vg, sliceColor(si, inRegion ? 0.95f : 0.35f));
		nvgFill(vg);
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
		if (next - x >= 18) text(vg, x + 3, bottom + 12, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 11, kDim, string::f("%zu", i + 1));
	}

	// Playheads.
	for (int c = 0; c < module->activeChannels; c++) {
		float pos = module->playheads[c];
		int s = module->playingSlice[c];
		if (pos < 0 || s < 0) continue;
		float x = frameX(pos);
		nvgBeginPath(vg);
		nvgRect(vg, x - 1, top, 2, bottom - top);
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xe0));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRect(vg, frameX(double(loop->slices[size_t(s)].start)), top, frameX(double(loop->sliceEnd(size_t(s)))) - frameX(double(loop->slices[size_t(s)].start)), 3);
		nvgFillColor(vg, sliceColor(size_t(s), 1.f));
		nvgFill(vg);
	}
}
