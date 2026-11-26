#include <cmath>
#include "Page.hpp"
#include "core/color.hpp"
#include "core/edit.hpp"
#include "core/voice.hpp"

using namespace spools;
using namespace theme;

namespace {

constexpr float kHeader = 18, kFieldH = 17, kFieldRows = 3, kFieldCols = 5, kGrab = 5, kFlag = 8;

// A per-slice parameter shown as a name/value cell. Numeric fields drag vertically (Shift for fine) and
// reset on double-click; choice fields open a menu.
struct Field {
	const char* name;
	float SliceParams::*value;  // numeric when set
	float lo, hi, perPx;
	const char* format;
	std::vector<std::string> options;
	std::function<size_t(const SliceParams&)> get;
	std::function<void(SliceParams&, size_t, const Loop&, size_t)> set;
};

const std::vector<Field>& fields() {
	static const std::vector<Field> f = [] {
		std::vector<std::string> tails = {"Global"};
		for (auto& t : tailChoices()) tails.push_back(t.name);
		auto num = [](const char* n, float SliceParams::*v, float lo, float hi, float perPx, const char* fmt) {
			return Field{n, v, lo, hi, perPx, fmt, {}, nullptr, nullptr};
		};
		auto pick = [](const char* n, std::vector<std::string> o, std::function<size_t(const SliceParams&)> g,
			std::function<void(SliceParams&, size_t, const Loop&, size_t)> s) { return Field{n, nullptr, 0, 0, 0, "", o, g, s}; };
		return std::vector<Field>{
			num("Pitch", &SliceParams::pitch, -48, 48, 0.1f, "%+.1f st"),
			num("Gain", &SliceParams::gain, -48, 12, 0.2f, "%+.1f dB"),
			num("Pan", &SliceParams::pan, -1, 1, 0.01f, "%+.2f"),
			num("Cutoff", &SliceParams::cutoff, -8, 8, 0.05f, "%+.1f V"),
			num("Res", &SliceParams::res, -1, 1, 0.01f, "%+.2f"),
			num("Xfade", &SliceParams::xfade, 0, 0.25f, 0.0005f, "%.3f s"),
			pick("Reverse", {"Off", "On"}, [](const SliceParams& p) { return size_t(p.reverse); },
				[](SliceParams& p, size_t i, const Loop&, size_t) { p.reverse = i == 1; }),
			pick("Tail", tails,
				[](const SliceParams& p) {
					for (size_t i = 0; i < tailChoices().size(); i++)
						if (tailChoices()[i].mode == p.tail) return i + 1;
					return size_t(0);
				},
				[](SliceParams& p, size_t i, const Loop&, size_t) { p.tail = i ? tailChoices()[i - 1].mode : TailMode::Inherit; }),
			pick("Loop", {"Forward", "Pingpong"}, [](const SliceParams& p) { return size_t(p.pingPong); },
				[](SliceParams& p, size_t i, const Loop&, size_t) { p.pingPong = i == 1; }),
			pick("Points", {"Auto", "Manual"}, [](const SliceParams& p) { return size_t(p.hasLoopPoints()); },
				[](SliceParams& p, size_t i, const Loop& l, size_t s) {
					if (i == 0) { p.loopStart = p.loopEnd = 0; return; }
					if (p.hasLoopPoints()) return;
					const double sr = l.audio->sampleRate, from = double(l.slices[s].start);
					auto lp = loopPoints(l, s, SlicePlayer::span(*l.audio, l.slices[s].start, l.sliceEnd(s), p.startTrim, p.endTrim, false), {});
					p.loopStart = float((lp.start - from) / sr);
					p.loopEnd = float((lp.end - from) / sr);
				}),
			num("Send A", &SliceParams::sendA, 0, 1, 0.005f, "%.2f"),
			num("Send B", &SliceParams::sendB, 0, 1, 0.005f, "%.2f"),
			num("CV A", &SliceParams::cvA, -10, 10, 0.02f, "%+.2f V"),
			num("CV B", &SliceParams::cvB, -10, 10, 0.02f, "%+.2f V"),
		};
	}();
	return f;
}

std::string noteName(double hz) {
	static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	int midi = int(std::lround(69 + 12 * std::log2(hz / 440)));
	return string::f("%s%d", names[(midi % 12 + 12) % 12], midi / 12 - 1);
}

// The selected slice (the first, when several are) with its view: the slice plus any trim reaching outside it.
struct View {
	size_t index;
	double a, b;  // frames shown
	float top, bottom, x0, x1;
	View(const DisplayContext& ctx, const Loop& l, size_t i) : index(i) {
		const Slice& s = l.slices[i];
		const double sr = l.audio->sampleRate;
		a = std::max(0.0, double(s.start) + std::min(0.f, s.p.startTrim) * sr);
		b = std::min(double(l.audio->frames), double(l.sliceEnd(i)) - std::min(0.f, s.p.endTrim) * sr);
		if (b - a < 2) b = a + 2;
		top = kHeader;
		bottom = ctx.area.size.y - kFieldRows * kFieldH - 4;
		x0 = kPad;
		x1 = ctx.area.size.x - kPad;
	}
	float x(double f) const { return x0 + float((f - a) / (b - a)) * (x1 - x0); }
	double frame(float px) const { return std::clamp(a + double(px - x0) / (x1 - x0) * (b - a), a, b); }
};

struct SlicePage : Page {
	enum class Drag { None, LoopStart, LoopEnd, TrimStart, TrimEnd, Field };
	Drag drag = Drag::None;
	size_t dragField = 0, dragSlice = 0;  // the slice is fixed at the press; the playing one may change
	float dragY = 0, dragValue = 0;
	std::shared_ptr<const Loop> dragBase, dragResult;

	const char* title() const override { return "SLICE"; }

	static size_t shown(Spools* m, const Loop& l) {
		auto& sel = m->selected();
		size_t i = sel.empty() ? size_t(std::max(0, m->playingSlice[0].load())) : sel.front();
		return std::min(i, l.slices.size() - 1);
	}

	math::Rect cell(const DisplayContext& ctx, size_t i) const {
		const float cw = (ctx.area.size.x - 2 * kPad) / kFieldCols;
		const float y = ctx.area.size.y - kFieldRows * kFieldH;
		return math::Rect(Vec(kPad + cw * float(i % size_t(kFieldCols)), y + kFieldH * float(i / size_t(kFieldCols))), Vec(cw - 6, kFieldH));
	}

	// The slice's own tail, or the global one.
	static TailMode tailOf(Spools* m, const Loop& l, size_t i) {
		if (l.slices[i].p.tail != TailMode::Inherit) return l.slices[i].p.tail;
		const auto& c = tailChoices();
		return c[std::min(size_t(m->params[Spools::TAIL_PARAM].getValue()), c.size() - 1)].mode;
	}

	// Loop points as frames: manual, searched (Auto-loop) or the default region.
	static SlicePlayer::LoopPoints points(Spools* m, const Loop& l, size_t i) {
		const SliceParams& p = l.slices[i].p;
		auto lp = loopPoints(l, i, SlicePlayer::span(*l.audio, l.slices[i].start, l.sliceEnd(i), p.startTrim, p.endTrim, false), {},
			tailOf(m, l, i));
		lp.start = std::clamp(lp.start, double(l.slices[i].start), double(l.sliceEnd(i)));
		lp.end = std::clamp(lp.end, lp.start, double(l.sliceEnd(i)));
		return lp;
	}

	static math::Rect stepButton(math::Rect bar, int dir) {
		return math::Rect(Vec(bar.pos.x + bar.size.x - (dir > 0 ? 26 : 54), 3), Vec(24, kTabH - 6));
	}

	void drawToolbar(const DisplayContext& ctx, math::Rect bar) override {
		if (!ctx.module || !ctx.loop) return;
		for (int dir : {-1, 1}) {
			math::Rect r = stepButton(bar, dir);
			nvgBeginPath(ctx.vg);
			nvgRoundedRect(ctx.vg, r.pos.x + 0.5f, r.pos.y + 0.5f, r.size.x - 1, r.size.y - 1, 3);
			nvgStrokeColor(ctx.vg, kDim);
			nvgStrokeWidth(ctx.vg, 1);
			nvgStroke(ctx.vg);
			ctx.text(r.pos.x + r.size.x / 2, r.pos.y + 12, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kReadout, kText, dir < 0 ? "‹" : "›");
		}
	}

	bool onToolbarButton(const DisplayContext& ctx, math::Rect bar, Vec pos) override {
		if (!ctx.module || !ctx.loop) return false;
		for (int dir : {-1, 1})
			if (stepButton(bar, dir).contains(pos)) {
				const long n = long(ctx.loop->slices.size());
				auto& sel = ctx.module->selected();
				long i = (long(shown(ctx.module, *ctx.loop)) + dir + n) % n;
				sel.assign(1, size_t(i));
				return true;
			}
		return false;
	}


	void openFieldMenu(Spools* m, size_t fi, size_t si) {
		const Field& f = fields()[fi];
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Slice %zu %s", si + 1, f.name)));
		for (size_t k = 0; k < f.options.size(); k++) {
			menu->addChild(createCheckMenuItem(f.options[k], "",
				[=] { auto l = m->loop(); return l && si < l->slices.size() && f.get(l->slices[si].p) == k; },
				[=] {
					auto l = m->loop();
					if (!l || si >= l->slices.size()) return;
					SliceParams p = l->slices[si].p;
					f.set(p, k, *l, si);
					m->commitEdit(l, setSliceParams(*l, {si}, {p}), string::f("slice %s", f.name));
				}));
		}
	}

	bool onButton(const DisplayContext& ctx, Vec pos, const widget::Widget::ButtonEvent& e) override {
		Spools* m = ctx.module;
		if (!m || !ctx.loop || ctx.loop->slices.empty() || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) return false;
		const Loop& l = *ctx.loop;
		const View v(ctx, l, shown(m, l));
		const SliceParams& p = l.slices[v.index].p;
		dragBase = dragResult = ctx.loop;
		dragSlice = v.index;
		drag = Drag::None;
		for (size_t fi = 0; fi < fields().size(); fi++) {
			if (!cell(ctx, fi).contains(pos)) continue;
			const Field& f = fields()[fi];
			if (!f.value) { openFieldMenu(m, fi, v.index); return true; }
			drag = Drag::Field;
			dragField = fi;
			dragY = pos.y;
			dragValue = p.*f.value;
			return true;
		}
		if (pos.y < v.top || pos.y > v.bottom) return true;
		const double sr = l.audio->sampleRate;
		if (pos.y < v.top + kFlag + 2 && tailOf(m, l, v.index) != TailMode::Stretch) {  // loop flags along the top
			auto lp = points(m, l, v.index);
			if (std::fabs(pos.x - v.x(lp.start)) <= kGrab) { drag = Drag::LoopStart; return true; }
			if (std::fabs(pos.x - v.x(lp.end)) <= kGrab) { drag = Drag::LoopEnd; return true; }
		}
		if (pos.y > v.bottom - kFlag - 2) {  // trim handles along the bottom
			if (std::fabs(pos.x - v.x(double(l.slices[v.index].start) + p.startTrim * sr)) <= kGrab) { drag = Drag::TrimStart; return true; }
			if (std::fabs(pos.x - v.x(double(l.sliceEnd(v.index)) - p.endTrim * sr)) <= kGrab) { drag = Drag::TrimEnd; return true; }
		}
		return true;
	}

	bool onDragMove(const DisplayContext& ctx, Vec pos, const widget::Widget::DragMoveEvent&) override {
		Spools* m = ctx.module;
		if (!m || !dragBase || drag == Drag::None || dragSlice >= dragBase->slices.size()) return false;
		const Loop& l = *dragBase;
		const View v(ctx, l, dragSlice);
		const size_t i = v.index;
		SliceParams p = l.slices[i].p;
		const double sr = l.audio->sampleRate, from = double(l.slices[i].start), len = double(l.sliceEnd(i)) - from;
		const double f = v.frame(pos.x), minLoop = 4 + 0.001 * sr;
		switch (drag) {
			case Drag::Field: {
				const Field& fd = fields()[dragField];
				const float fine = (APP->window->getMods() & GLFW_MOD_SHIFT) ? 0.1f : 1.f;
				p.*fd.value = std::clamp(dragValue + (dragY - pos.y) * fd.perPx * fine, fd.lo, fd.hi);
				break;
			}
			case Drag::LoopStart:
			case Drag::LoopEnd: {
				auto lp = points(m, l, i);  // the first move turns automatic points into manual ones
				if (tailOf(m, l, i) == TailMode::AutoLoop) p.tail = TailMode::Loop;  // so the manual points are used
				if (drag == Drag::LoopStart) lp.start = std::clamp(f, from, lp.end - minLoop);
				else lp.end = std::clamp(f, lp.start + minLoop, from + len);
				p.loopStart = float((lp.start - from) / sr);
				p.loopEnd = float((lp.end - from) / sr);
				break;
			}
			case Drag::TrimStart: p.startTrim = float(std::clamp((f - from) / sr, -0.25, len / sr - 0.002)); break;
			case Drag::TrimEnd: p.endTrim = float(std::clamp((from + len - f) / sr, -0.25, len / sr - 0.002)); break;
			default: return false;
		}
		dragResult = setSliceParams(l, {i}, {p});
		m->publish(dragResult);
		return true;
	}

	void onDragEnd(const DisplayContext& ctx) override {
		static const char* names[] = {"", "move loop start", "move loop end", "trim slice", "trim slice", "edit slice"};
		if (ctx.module && dragBase && dragResult != dragBase) ctx.module->commitEdit(dragBase, dragResult, names[int(drag)], false);
		drag = Drag::None;
		dragBase = dragResult = nullptr;
	}

	// Double-click resets a numeric field, or the loop points to automatic when on the waveform.
	void onDoubleClick(const DisplayContext& ctx, Vec pos) override {
		Spools* m = ctx.module;
		if (!m || !ctx.loop || ctx.loop->slices.empty()) return;
		const Loop& l = *ctx.loop;
		const size_t i = shown(m, l);
		SliceParams p = l.slices[i].p;
		std::string name;
		for (size_t fi = 0; fi < fields().size(); fi++)
			if (cell(ctx, fi).contains(pos) && fields()[fi].value) { p.*fields()[fi].value = 0; name = string::f("reset slice %s", fields()[fi].name); }
		const View v(ctx, l, i);
		if (name.empty() && pos.y >= v.top && pos.y <= v.bottom && p.hasLoopPoints()) { p.loopStart = p.loopEnd = 0; name = "auto loop points"; }
		if (!name.empty()) m->commitEdit(ctx.loop, setSliceParams(l, {i}, {p}), name);
	}

	void draw(const DisplayContext& ctx) override {
		NVGcontext* vg = ctx.vg;
		const float ax = ctx.area.pos.x, ay = ctx.area.pos.y, w = ctx.area.size.x, h = ctx.area.size.y;
		Spools* m = ctx.module;
		if (!m || !ctx.loop || ctx.loop->slices.empty()) {
			ctx.text(ax + w / 2, ay + h / 2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 16, kText, m ? "No slices" : "SPOOLS");
			return;
		}
		const Loop& l = *ctx.loop;
		const Audio& a = *l.audio;
		const View v(ctx, l, shown(m, l));
		const size_t i = v.index, n = l.slices.size();
		const SliceParams& p = l.slices[i].p;
		const double sr = a.sampleRate, from = double(l.slices[i].start), to = double(l.sliceEnd(i));
		const Rgb hc = oklch(0.78f, 0.13f, sliceHue(i, n));
		const NVGcolor hue = nvgRGBf(hc.r, hc.g, hc.b);
		const bool hasFeatures = l.features && i < l.features->size();

		// Header: which slice, its length and pitch; the hint on how it was chosen.
		const float hy = ay + 15;
		std::string head = string::f("Slice %zu of %zu   %.3f s", i + 1, n, (to - from) / sr);
		if (hasFeatures) {
			const auto& pe = (*l.features)[i].pitch;
			if (pe.period > 0 && pe.confidence > 0.5f) head += string::f("   %s  %.0f Hz", noteName(sr / pe.period).c_str(), sr / pe.period);
		}
		ctx.text(ax + kPad, hy, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kReadout, kText, head);
		ctx.text(ax + w - kPad, hy, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kBody, kDim,
			m->selected().empty() ? "select on LOOP, or ‹ ›" : m->selected().size() > 1 ? "first of selection" : "");

		const float top = ay + v.top, bottom = ay + v.bottom, mid = (top + bottom) / 2, amp = (bottom - top) / 2 - 2;
		auto fx = [&](double f) { return ax + v.x(f); };
		ctx.fillRect(ax + v.x0, top, v.x1 - v.x0, bottom - top, withAlpha(hue, 0.08f));

		// Loop region: bright when set by hand, faint when automatic.
		const auto lp = points(m, l, i);
		const TailMode tail = tailOf(m, l, i);
		const bool searched = tail == TailMode::AutoLoop && hasFeatures && (*l.features)[i].loop.score > 0;
		const bool manual = !searched && tail != TailMode::AutoLoop && p.hasLoopPoints();
		const bool stretch = tail == TailMode::Stretch;
		const float la = fx(lp.start), lb = fx(lp.end);
		if (!stretch) ctx.fillRect(la, top, lb - la, bottom - top, withAlpha(kAccent, manual || searched ? 0.16f : 0.07f));
		else {  // the part that is played as grains
			VoiceParams vp;
			vp.transientPreserve = m->transientPreserve;
			vp.stretchTail = m->stretchTail;
			auto sp = stretchPlan(l, i, SlicePlayer::span(a, l.slices[i].start, l.sliceEnd(i), p.startTrim, p.endTrim, false), vp, 0, 1);
			const float sa = fx(sp.from), sb = fx(sp.to);
			ctx.fillRect(sa, top, sb - sa, bottom - top, withAlpha(kAccent, 0.12f));
			ctx.text((sa + sb) / 2, top + 11, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 11, kText,
				sp.period > 0 ? string::f("STRETCH  grain %.0f ms", sp.grain / sr * 1000) : "STRETCH  grain 40 ms");
		}

		for (float x = ax + v.x0; x < ax + v.x1; x += 1.f) {
			size_t f0 = size_t(v.frame(x - ax)), f1 = std::max(f0 + 1, size_t(v.frame(x + 1 - ax)));
			float lo = 0, hi = 0;
			for (int c = 0; c < a.channels; c++) {
				auto pk = l.mips->peak(c, f0, f1, a);
				lo = std::min(lo, pk.lo);
				hi = std::max(hi, pk.hi);
			}
			const bool in = f0 >= size_t(from + p.startTrim * sr) && f0 < size_t(to - p.endTrim * sr);
			ctx.fillRect(x, mid - clamp(hi, -1.f, 1.f) * amp, 1.f, std::max(1.f, (hi - lo) * amp), withAlpha(hue, in ? 0.95f : 0.3f));
		}

		// Slice edges, transient end, loop flags and trim triangles.
		for (double f : {from, to}) ctx.fillRect(fx(f), top, 1, bottom - top, nvgRGBA(0xff, 0xff, 0xff, 0x50));
		if (hasFeatures) {
			const float tx = fx(from + double((*l.features)[i].transientEnd));
			for (float y = top; y < bottom; y += 6) ctx.fillRect(tx, y, 1, 3, kText);
			ctx.text(tx + 3, bottom - 3, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 11, kDim, "T");
		}
		const NVGcolor flag = manual || searched ? kAccent : withAlpha(kAccent, 0.6f);
		for (int k = 0; k < 2 && !stretch; k++) {
			const float x = k ? lb : la;
			ctx.fillRect(x - (k ? 1 : 0), top, 1.5f, bottom - top, flag);
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, top);
			nvgLineTo(vg, k ? x - kFlag : x + kFlag, top);
			nvgLineTo(vg, x, top + kFlag);
			nvgClosePath(vg);
			nvgFillColor(vg, flag);
			nvgFill(vg);
		}
		if (!stretch) ctx.text((la + lb) / 2, top + 11, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 11, kText,
			searched ? string::f("FOUND %.0f%%%s", (*l.features)[i].loop.score * 100, p.pingPong ? " ⇄" : "")
				: string::f("%s%s", manual ? "LOOP" : "AUTO LOOP", p.pingPong ? " ⇄" : ""));
		for (bool end : {false, true}) {
			const float tx = fx(end ? to - p.endTrim * sr : from + p.startTrim * sr);
			nvgBeginPath(vg);
			nvgMoveTo(vg, tx, bottom - kFlag);
			nvgLineTo(vg, tx, bottom);
			nvgLineTo(vg, end ? tx - kFlag : tx + kFlag, bottom);
			nvgClosePath(vg);
			nvgFillColor(vg, kText);
			nvgFill(vg);
		}
		for (int c = 0; c < m->activeChannels; c++) {
			const float pos = m->playheads[c];
			if (pos >= v.a && pos <= v.b && m->playingSlice[c] == int(i)) ctx.fillRect(fx(pos) - 1, top, 2, bottom - top, kText);
		}

		for (size_t fi = 0; fi < fields().size(); fi++) {
			const Field& f = fields()[fi];
			math::Rect r = cell(ctx, fi);
			r.pos = r.pos.plus(ctx.area.pos);
			if ((fi + fi / size_t(kFieldCols)) % 2 == 0) ctx.fillRect(r.pos.x - 4, r.pos.y, r.size.x + 8, r.size.y, nvgRGBA(0xff, 0xff, 0xff, 0x08));
			const float y = r.pos.y + 13;
			const bool active = drag == Drag::Field && dragField == fi;
			ctx.text(r.pos.x, y, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kBody, kDim, f.name);
			std::string value = f.value ? string::f(f.format, p.*f.value) : f.options[f.get(p)];
			ctx.text(r.pos.x + r.size.x, y, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kReadout, active ? kAccent : kText, value);
		}
	}

	static NVGcolor withAlpha(NVGcolor c, float al) { c.a = al; return c; }
};

}

std::unique_ptr<Page> makeSlicePage() { return std::make_unique<SlicePage>(); }
