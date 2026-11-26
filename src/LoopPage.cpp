#include <cmath>
#include "Page.hpp"
#include "core/color.hpp"
#include "core/edit.hpp"

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

// Where things are on the page, in page-area coordinates, for drawing and hit-testing alike.
struct Geometry {
	float top, bottom, x0, x1;
	double frames, viewStart, viewSpan;  // view in fractions of the loop
	Geometry(const DisplayContext& ctx, const Loop& l) {
		top = kHeader;
		bottom = ctx.area.size.y - kFooter;
		x0 = kPad;
		x1 = ctx.area.size.x - kPad;
		frames = double(l.audio->frames);
		viewSpan = ctx.module ? ctx.module->viewSpan : 1;
		viewStart = ctx.module ? ctx.module->viewStart : 0;
	}
	float x(double frame) const { return x0 + float((frame / frames - viewStart) / viewSpan) * (x1 - x0); }
	double frame(float px) const { return std::clamp((viewStart + double(px - x0) / (x1 - x0) * viewSpan) * frames, 0.0, frames); }
};

constexpr float kGrab = 4;       // px either side of a boundary that picks it up
constexpr float kHandleBand = 9; // px at the top (region) and bottom (trims) of the waveform that hold handles

struct LoopPage : Page {
	enum class Drag { None, Boundary, RegionStart, RegionEnd, TrimStart, TrimEnd, Scroll, CropStart, CropEnd, CropMove };
	Drag drag = Drag::None;
	size_t dragIndex = 0;
	std::shared_ptr<const Loop> dragBase, dragResult;
	float scrollAnchor = 0;
	float regionBefore[2] = {};

	// Crop view: the whole source file, the crop as a bright window with handles. Edits preview here and
	// apply (re-slicing plain audio) when the mouse is released.
	std::shared_ptr<const Audio> mipsSource;
	WaveformMips sourceMips;
	Range cropPreview{0, 0};
	bool previewing = false;
	double grabOffset = 0;

	const WaveformMips& mipsOf(const Loop& l) {
		if (mipsSource != l.source) {
			sourceMips = WaveformMips();
			sourceMips.build(*l.source);
			mipsSource = l.source;
		}
		return sourceMips;
	}

	static math::Rect cropButton(math::Rect bar) { return math::Rect(Vec(bar.pos.x + bar.size.x - 64, 3), Vec(64, kTabH - 6)); }

	void drawToolbar(const DisplayContext& ctx, math::Rect bar) override {
		if (!ctx.module || !ctx.loop) return;
		math::Rect r = cropButton(bar);
		const bool on = ctx.module->cropView;
		nvgBeginPath(ctx.vg);
		nvgRoundedRect(ctx.vg, r.pos.x + 0.5f, r.pos.y + 0.5f, r.size.x - 1, r.size.y - 1, 3);
		if (on) { nvgFillColor(ctx.vg, kAccent); nvgFill(ctx.vg); }
		nvgStrokeColor(ctx.vg, on ? kAccent : kDim);
		nvgStrokeWidth(ctx.vg, 1);
		nvgStroke(ctx.vg);
		ctx.text(r.pos.x + r.size.x / 2, r.pos.y + 12, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kBody, on ? kBg : kText, "CROP");
	}

	bool onToolbarButton(const DisplayContext& ctx, math::Rect bar, Vec pos) override {
		if (!ctx.module || !ctx.loop || !cropButton(bar).contains(pos)) return false;
		ctx.module->cropView = !ctx.module->cropView;
		return true;
	}

	Range shownCrop(const Loop& l) const { return previewing ? cropPreview : l.crop; }

	void drawCrop(const DisplayContext& ctx) {
		const Loop& l = *ctx.loop;
		const Audio& src = *l.source;
		const WaveformMips& mips = mipsOf(l);
		const float ax = ctx.area.pos.x, ay = ctx.area.pos.y, h = ctx.area.size.y;
		const float top = ay + kHeader, bottom = ay + h - kFooter, mid = (top + bottom) / 2, amp = (bottom - top) / 2 - 2;
		const float x0 = ax + kPad, x1 = ax + ctx.area.size.x - kPad;
		auto fx = [&](double f) { return x0 + float(f / double(src.frames)) * (x1 - x0); };
		const Range c = shownCrop(l);
		const double sr = src.sampleRate;

		ctx.text(x0, ay + 15, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kReadout, kText,
			string::f("Crop %.3f – %.3f s  (%.3f s of %.2f s)", c.start / sr, c.end / sr, c.length() / sr, src.seconds()));

		const float ca = fx(double(c.start)), cb = fx(double(c.end));
		ctx.fillRect(ca, top, cb - ca, bottom - top, nvgRGBA(0xff, 0xc8, 0x57, 0x18));
		for (float x = x0; x < x1; x += 1.f) {
			size_t f0 = size_t(double(x - x0) / (x1 - x0) * src.frames), f1 = std::max(f0 + 1, size_t(double(x + 1 - x0) / (x1 - x0) * src.frames));
			float lo = 0, hi = 0;
			for (int ch = 0; ch < src.channels; ch++) {
				auto pk = mips.peak(ch, f0, f1, src);
				lo = std::min(lo, pk.lo);
				hi = std::max(hi, pk.hi);
			}
			const bool inside = x >= ca && x < cb;
			ctx.fillRect(x, mid - clamp(hi, -1.f, 1.f) * amp, 1.f, std::max(1.f, (hi - lo) * amp), inside ? kText : nvgRGBA(0xa8, 0xa8, 0xae, 0x60));
		}
		if (!previewing)  // the slices belong to the current crop; hide them while it is being changed
			for (auto& s : l.slices) ctx.fillRect(fx(double(s.start + l.crop.start)), top, 1, bottom - top, nvgRGBA(0xff, 0xff, 0xff, 0x40));
		for (float x : {ca, cb}) {
			ctx.fillRect(x - 1, top, 2, bottom - top, kAccent);
			ctx.fillRect(x - 4, top, 8, 8, kAccent);
		}
		ctx.text(x0, bottom + 12, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 11, kDim, "0 s");
		ctx.text(x1, bottom + 12, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, 11, kDim, string::f("%.2f s", src.seconds()));
	}

	double sourceFrame(const DisplayContext& ctx, const Loop& l, float px) const {
		const float x0 = kPad, x1 = ctx.area.size.x - kPad;
		return std::clamp(double(px - x0) / (x1 - x0), 0.0, 1.0) * double(l.source->frames);
	}

	bool cropButtonPress(const DisplayContext& ctx, Vec pos) {
		const Loop& l = *ctx.loop;
		const float x0 = kPad, x1 = ctx.area.size.x - kPad;
		auto fx = [&](double f) { return x0 + float(f / double(l.source->frames)) * (x1 - x0); };
		cropPreview = l.crop;
		const double f = sourceFrame(ctx, l, pos.x);
		if (std::fabs(pos.x - fx(double(l.crop.start))) <= kGrab * 2) drag = Drag::CropStart;
		else if (std::fabs(pos.x - fx(double(l.crop.end))) <= kGrab * 2) drag = Drag::CropEnd;
		else if (f > double(l.crop.start) && f < double(l.crop.end)) { drag = Drag::CropMove; grabOffset = f - double(l.crop.start); }
		return true;
	}

	void cropDrag(const DisplayContext& ctx, Vec pos) {
		const Loop& l = *dragBase;
		const double f = sourceFrame(ctx, l, pos.x), total = double(l.source->frames);
		const double minLen = 0.01 * l.source->sampleRate;
		Range c = cropPreview;
		if (drag == Drag::CropStart) c.start = size_t(std::clamp(f, 0.0, double(c.end) - minLen));
		else if (drag == Drag::CropEnd) c.end = size_t(std::clamp(f, double(c.start) + minLen, total));
		else {
			const double len = double(l.crop.length());
			c.start = size_t(std::clamp(f - grabOffset, 0.0, total - len));
			c.end = c.start + l.crop.length();
		}
		cropPreview = c;
		previewing = true;
	}

	const char* title() const override { return "LOOP"; }

	static void region(Spools* m, const Loop& l, int& r0, int& rn) {
		PhraseParams p;
		const size_t n = l.slices.size();
		float fs = m->params[Spools::REGION_START_PARAM].getValue(), fe = m->params[Spools::REGION_END_PARAM].getValue();
		if (fs != 0.f || fe != 1.f) { p.regionStart = m->regionIndex(fs, n); p.regionEnd = m->regionIndex(fe, n); }
		p.region(l, r0, rn);
	}

	// Grid snap for edits: the slicing grid at the loop's tempo, off while Alt is held.
	static size_t snap(Spools* m, const Loop& l, double f) {
		if ((APP->window->getMods() & GLFW_MOD_ALT) || !m) return size_t(f);
		double step = m->slicing.gridBeats * 60.0 / l.bpm * l.audio->sampleRate;
		return size_t(std::max(0.0, std::round(f / step) * step));
	}

	// Nearest boundary (index >= 1) within kGrab px, or 0.
	static size_t boundaryAt(const Geometry& g, const Loop& l, float px) {
		size_t best = 0;
		float bestD = kGrab;
		for (size_t i = 1; i < l.slices.size(); i++) {
			float d = std::fabs(g.x(double(l.slices[i].start)) - px);
			if (d <= bestD) { bestD = d; best = i; }
		}
		return best;
	}

	static float trimX(const Geometry& g, const Loop& l, size_t i, bool end) {
		const double sr = l.audio->sampleRate;
		return end ? g.x(double(l.sliceEnd(i)) - l.slices[i].p.endTrim * sr) : g.x(double(l.slices[i].start) + l.slices[i].p.startTrim * sr);
	}

	bool onButton(const DisplayContext& ctx, Vec pos, const widget::Widget::ButtonEvent& e) override {
		Spools* m = ctx.module;
		if (!m || !ctx.loop || e.action != GLFW_PRESS) return false;
		const Loop& l = *ctx.loop;
		Geometry g(ctx, l);
		dragBase = dragResult = ctx.loop;
		drag = Drag::None;
		if (m->cropView) return e.button == GLFW_MOUSE_BUTTON_LEFT && cropButtonPress(ctx, pos);
		if (e.button == GLFW_MOUSE_BUTTON_MIDDLE) { drag = Drag::Scroll; scrollAnchor = pos.x; return true; }
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return false;

		auto& sel = m->selected();
		if (pos.y < g.top || pos.y > g.bottom) { sel.clear(); return true; }  // header or footer: clear

		// Region handles along the top edge.
		int r0, rn;
		region(m, l, r0, rn);
		regionBefore[0] = m->params[Spools::REGION_START_PARAM].getValue();
		regionBefore[1] = m->params[Spools::REGION_END_PARAM].getValue();
		if (pos.y < g.top + kHandleBand) {
			float ra = g.x(double(l.slices[size_t(r0)].start)), rb = g.x(double(l.sliceEnd(size_t(r0 + rn - 1))));
			if (std::fabs(pos.x - ra) <= kGrab * 2) { drag = Drag::RegionStart; return true; }
			if (std::fabs(pos.x - rb) <= kGrab * 2) { drag = Drag::RegionEnd; return true; }
		}
		// Trim handles of selected slices along the bottom edge.
		if (pos.y > g.bottom - kHandleBand)
			for (size_t i : sel)
				for (bool end : {false, true})
					if (std::fabs(pos.x - trimX(g, l, i, end)) <= kGrab) {
						drag = end ? Drag::TrimEnd : Drag::TrimStart;
						dragIndex = i;
						return true;
					}
		if (size_t b = boundaryAt(g, l, pos.x)) { drag = Drag::Boundary; dragIndex = b; return true; }

		// Selection: click toggles; Shift/Ctrl adds to or removes from the selection.
		size_t i = sliceAt(l, size_t(g.frame(pos.x)));
		bool multi = APP->window->getMods() & (GLFW_MOD_SHIFT | RACK_MOD_CTRL);
		auto it = std::find(sel.begin(), sel.end(), i);
		bool was = it != sel.end();
		if (!multi) sel.clear();
		if (was && multi) sel.erase(it);
		else if (!was) { sel.push_back(i); std::sort(sel.begin(), sel.end()); }
		return true;
	}

	bool onDragMove(const DisplayContext& ctx, Vec pos, const widget::Widget::DragMoveEvent& e) override {
		Spools* m = ctx.module;
		if (!m || !dragBase || drag == Drag::None) return false;
		const Loop& l = *dragBase;
		Geometry g(ctx, l);
		const double f = g.frame(pos.x), sr = l.audio->sampleRate;
		switch (drag) {
			case Drag::CropStart:
			case Drag::CropEnd:
			case Drag::CropMove:
				cropDrag(ctx, pos);
				return true;
			case Drag::Scroll: {
				float dx = pos.x - scrollAnchor;
				scrollAnchor = pos.x;
				m->viewStart = std::clamp(m->viewStart - dx / (g.x1 - g.x0) * m->viewSpan, 0.f, 1.f - m->viewSpan);
				return true;
			}
			case Drag::Boundary:
				dragResult = moveBoundary(l, dragIndex, snap(m, l, f));
				break;
			case Drag::RegionStart:
			case Drag::RegionEnd: {
				const size_t n = l.slices.size();
				size_t i = sliceAt(l, size_t(f + (drag == Drag::RegionEnd ? 0 : sr * 0.001)));
				int r0, rn;
				region(m, l, r0, rn);
				size_t a = drag == Drag::RegionStart ? i : size_t(r0), b = drag == Drag::RegionEnd ? i : size_t(r0 + rn - 1);
				if (a > b) std::swap(a, b);
				float d = n > 1 ? float(n - 1) : 1.f;
				m->params[Spools::REGION_START_PARAM].setValue(float(a) / d);
				m->params[Spools::REGION_END_PARAM].setValue(float(b) / d);
				return true;
			}
			case Drag::TrimStart:
			case Drag::TrimEnd: {
				SliceParams p = l.slices[dragIndex].p;
				const double len = double(l.sliceEnd(dragIndex) - l.slices[dragIndex].start) / sr;
				if (drag == Drag::TrimStart) p.startTrim = float(std::clamp((f - double(l.slices[dragIndex].start)) / sr, -0.25, len - 0.002));
				else p.endTrim = float(std::clamp((double(l.sliceEnd(dragIndex)) - f) / sr, -0.25, len - 0.002));
				dragResult = setSliceParams(l, {dragIndex}, {p});
				break;
			}
			default: return false;
		}
		m->publish(dragResult);
		return true;
	}

	void onDragEnd(const DisplayContext& ctx) override {
		if (ctx.module && previewing && dragBase && (cropPreview.start != dragBase->crop.start || cropPreview.end != dragBase->crop.end))
			ctx.module->setCrop(cropPreview);
		previewing = false;
		if (ctx.module && dragResult != dragBase && (drag == Drag::Boundary || drag == Drag::TrimStart || drag == Drag::TrimEnd))
			ctx.module->commitEdit(dragBase, dragResult, drag == Drag::Boundary ? "move slice" : "trim slice");
		if (ctx.module && (drag == Drag::RegionStart || drag == Drag::RegionEnd)) {
			auto* all = new history::ComplexAction;
			all->name = "move region";
			for (int k = 0; k < 2; k++) {
				auto* h = new history::ParamChange;
				h->moduleId = ctx.module->id;
				h->paramId = k ? Spools::REGION_END_PARAM : Spools::REGION_START_PARAM;
				h->oldValue = regionBefore[k];
				h->newValue = ctx.module->params[h->paramId].getValue();
				all->push(h);
			}
			APP->history->push(all);
		}
		drag = Drag::None;
		dragBase = dragResult = nullptr;
	}

	// Double-click adds a slice; Ctrl-double-click on a boundary removes it.
	void onDoubleClick(const DisplayContext& ctx, Vec pos) override {
		Spools* m = ctx.module;
		if (!m || !ctx.loop || pos.y < kHeader || m->cropView) return;
		const Loop& l = *ctx.loop;
		Geometry g(ctx, l);
		if (APP->window->getMods() & RACK_MOD_CTRL) {
			if (size_t b = boundaryAt(g, l, pos.x)) m->commitEdit(ctx.loop, removeSlice(l, b), "delete slice");
			return;
		}
		auto next = addSlice(l, snap(m, l, g.frame(pos.x)));
		if (next->slices.size() != l.slices.size()) m->commitEdit(ctx.loop, next, "add slice");
	}

	// Scroll zooms around the pointer.
	bool onHoverScroll(const DisplayContext& ctx, Vec pos, const widget::Widget::HoverScrollEvent& e) override {
		Spools* m = ctx.module;
		if (!m || !ctx.loop || e.scrollDelta.y == 0 || m->cropView) return false;
		Geometry g(ctx, *ctx.loop);
		float at = m->viewStart + (pos.x - g.x0) / (g.x1 - g.x0) * m->viewSpan;
		float span = std::clamp(m->viewSpan * std::pow(0.85f, e.scrollDelta.y / 50.f), 0.002f, 1.f);
		m->viewStart = std::clamp(at - (pos.x - g.x0) / (g.x1 - g.x0) * span, 0.f, 1.f - span);
		m->viewSpan = span;
		return true;
	}

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
		if (module->cropView) return drawCrop(ctx);
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
		const size_t nsel = module->selected().size();
		std::string info = nsel ? string::f("%zu of %zu selected   BPM", nsel, n) : string::f("%zu slice%s  %.2f s   BPM", n, n == 1 ? "" : "s", a.seconds());
		nvgFontSize(vg, kReadout);
		ctx.text(right, hy, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kReadout, kDim, info);
		right -= nvgTextBounds(vg, 0, 0, info.c_str(), nullptr, nullptr) + 8;
		if (loop->origin == Origin::Rex) right = badge(ctx, right, hy, "REX unofficial", kAccent) - 6;
		if (module->playMode != Spools::kNoteMode)
			right = badge(ctx, right, hy, module->playMode == Spools::kLoopMode ? "LOOP MODE" : "PATTERN MODE", kText) - 6;
		nvgSave(vg);
		nvgScissor(vg, ax, ay, right - ax - 4, kHeader);
		ctx.text(ax + kPad, hy, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kReadout, kText, name);
		nvgRestore(vg);
		std::string err = module->getStatus();
		if (!err.empty()) ctx.text(ax + w / 2, hy, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kBody, kText, "⚠ " + err);

		const Geometry g(ctx, *loop);
		const float top = ay + g.top, bottom = ay + g.bottom, mid = (top + bottom) / 2, amp = (bottom - top) / 2 - 2;
		const float x0 = ax + g.x0, x1 = ax + g.x1;
		auto frameX = [&](double f) { return ax + g.x(f); };
		nvgSave(vg);
		nvgIntersectScissor(vg, x0, ay, x1 - x0, h);

		int r0 = 0, rn = int(n);
		region(module, *loop, r0, rn);
		auto inRegion = [&](size_t i) { return int(i) >= r0 && int(i) < r0 + rn; };
		for (size_t i = 0; i < n; i++) {
			float xa = frameX(double(loop->slices[i].start)), xb = frameX(double(loop->sliceEnd(i)));
			ctx.fillRect(xa, top, xb - xa, bottom - top, withAlpha(hue[i], inRegion(i) ? 0.10f : 0.03f));
		}

		// Beat and bar grid at the loop's own tempo.
		const double framesPerBeat = a.sampleRate * 60.0 / loop->bpm;
		const int beatsPerBar = std::max(1, loop->meterNum * 4 / std::max(1, loop->meterDen));
		if (frameX(framesPerBeat) - frameX(0) >= 4)
			for (int b = 1; b * framesPerBeat < a.frames; b++) {
				bool bar = b % beatsPerBar == 0;
				ctx.fillRect(std::round(frameX(b * framesPerBeat)), top, 1, bottom - top, nvgRGBA(0xff, 0xff, 0xff, bar ? 0x38 : 0x14));
			}

		// Waveform: one min/max bar per pixel, coloured by slice.
		size_t si = 0;
		for (float x = x0; x < x1; x += 1.f) {
			size_t f0 = size_t(g.frame(x - ax)), f1 = std::max(f0 + 1, size_t(g.frame(x + 1 - ax)));
			while (si + 1 < n && loop->slices[si + 1].start <= f0) si++;
			float lo = 0, hi = 0;
			for (int c = 0; c < a.channels; c++) {
				auto pk = loop->mips->peak(c, f0, f1, a);
				lo = std::min(lo, pk.lo);
				hi = std::max(hi, pk.hi);
			}
			ctx.fillRect(x, mid - clamp(hi, -1.f, 1.f) * amp, 1.f, std::max(1.f, (hi - lo) * amp), withAlpha(hue[si], inRegion(si) ? 0.95f : 0.30f));
		}

		// Region bracket, with grab tabs at its top corners.
		{
			float ra = frameX(double(loop->slices[size_t(r0)].start)), rb = frameX(double(loop->sliceEnd(size_t(r0 + rn - 1))));
			NVGcolor c = withAlpha(kAccent, rn < int(n) ? 0.9f : 0.45f);
			for (float x : {ra, rb - 2}) ctx.fillRect(x, top, 2, bottom - top, c);
			ctx.fillRect(ra, top, rb - ra, 2, c);
			ctx.fillRect(ra, top, 6, 7, c);
			ctx.fillRect(rb - 6, top, 6, 7, c);
		}

		// Selected slices: outline plus trim handles along the bottom.
		for (size_t i : module->selected()) {
			if (i >= n) continue;
			float xa = frameX(double(loop->slices[i].start)), xb = frameX(double(loop->sliceEnd(i)));
			nvgBeginPath(vg);
			nvgRect(vg, xa + 1, top + 1, xb - xa - 2, bottom - top - 2);
			nvgStrokeColor(vg, kText);
			nvgStrokeWidth(vg, 2);
			nvgStroke(vg);
			for (bool end : {false, true}) {
				float tx = ax + trimX(g, *loop, i, end);
				nvgBeginPath(vg);
				nvgMoveTo(vg, tx, bottom - kHandleBand);
				nvgLineTo(vg, tx, bottom);
				nvgLineTo(vg, end ? tx - 7 : tx + 7, bottom);
				nvgClosePath(vg);
				nvgFillColor(vg, kText);
				nvgFill(vg);
			}
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
		nvgRestore(vg);

		// Zoomed: a scroll bar under the slice numbers shows which part is visible.
		if (g.viewSpan < 1) {
			ctx.fillRect(x0, ay + h - 2, x1 - x0, 2, nvgRGBA(0xff, 0xff, 0xff, 0x20));
			ctx.fillRect(x0 + float(g.viewStart) * (x1 - x0), ay + h - 2, float(g.viewSpan) * (x1 - x0), 2, kDim);
		}
	}
};

}

std::unique_ptr<Page> makeLoopPage() { return std::make_unique<LoopPage>(); }
