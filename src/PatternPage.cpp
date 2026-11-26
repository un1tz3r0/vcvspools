#include <cmath>
#include <functional>
#include "Page.hpp"
#include "core/color.hpp"
#include "core/pattern.hpp"
#include "core/units.hpp"

using namespace spools;
using namespace theme;

namespace {

constexpr float kHeader = 18, kFieldH = 19, kFieldCols = 5, kGrab = 4, kTab = 7;

NVGcolor withAlpha(NVGcolor c, float a) { c.a = a; return c; }

std::string lengthName(int ticks) {
	switch (ticks) {
		case 1: return "1/32";
		case 2: return "1/16";
		case 4: return "1/8";
		case 8: return "1/4";
		case 16: return "1/2";
		case 32: return "1 bar";
		default: return string::f("%d/32", ticks);
	}
}

// A step setting shown as a name/value cell: numbers drag vertically (Shift for fine) and reset on
// double-click; Reverse opens a menu.
struct StepField {
	const char* name;
	float lo, hi, perPx, def;
	std::function<float(const PatternStep&)> get;
	std::function<void(PatternStep&, float)> set;
	std::function<std::string(float)> show;
	bool choice = false;
};

const std::vector<StepField>& stepFields() {
	static const std::vector<StepField> f = {
		{"Slice", -1, 63, 0.1f, 0, [](const PatternStep& s) { return float(s.slice); },
			[](PatternStep& s, float v) { s.slice = int(std::lround(v)); },
			[](float v) { return v < 0 ? std::string("Rest") : string::f("%d", int(std::lround(v)) + 1); }},
		{"Length", 1, 32, 0.15f, 4, [](const PatternStep& s) { return float(s.ticks); },
			[](PatternStep& s, float v) { s.ticks = int(std::lround(v)); }, [](float v) { return lengthName(int(std::lround(v))); }},
		{"Nudge", -1, 1, 0.01f, 0, [](const PatternStep& s) { return s.nudge; }, [](PatternStep& s, float v) { s.nudge = v; },
			[](float v) { return string::f("%+.2f", v); }},
		{"Gate", 0.05f, 1, 0.005f, 1, [](const PatternStep& s) { return s.duration; }, [](PatternStep& s, float v) { s.duration = v; },
			[](float v) { return string::f("%.0f%%", v * 100); }},
		{"Pitch", -48, 48, 0.1f, 0, [](const PatternStep& s) { return s.pitch; }, [](PatternStep& s, float v) { s.pitch = v; },
			[](float v) { return string::f("%+.1f st", v); }},
		{"Ratchet", 1, 8, 0.05f, 1, [](const PatternStep& s) { return float(s.ratchets); },
			[](PatternStep& s, float v) { s.ratchets = int(std::lround(v)); }, [](float v) { return string::f("×%d", int(std::lround(v))); }},
		{"Curve", -1, 1, 0.01f, 0, [](const PatternStep& s) { return s.ratchetCurve; }, [](PatternStep& s, float v) { s.ratchetCurve = v; },
			[](float v) { return string::f("%+.2f", v); }},
		{"Velocity", 0, 1, 0.005f, 1, [](const PatternStep& s) { return s.velocity; }, [](PatternStep& s, float v) { s.velocity = v; },
			[](float v) { return string::f("%.0f%%", v * 100); }},
		{"Ramp", -1, 1, 0.01f, 0, [](const PatternStep& s) { return s.velocityRamp; }, [](PatternStep& s, float v) { s.velocityRamp = v; },
			[](float v) { return string::f("%+.0f%%", v * 100); }},
		{"Reverse", 0, 1, 0, 0, [](const PatternStep& s) { return s.reverse ? 1.f : 0.f; },
			[](PatternStep& s, float v) { s.reverse = v > 0.5f; }, [](float v) { return std::string(v > 0.5f ? "On" : "Off"); }, true},
	};
	return f;
}

// Step blocks laid out across the page by length, in page-area coordinates.
struct Lane {
	float top, bottom, x0, x1;
	double perTick;
	std::vector<int> startTick;  // per step, plus the total at the end
	Lane(const DisplayContext& ctx, const Pattern& p) {
		top = kHeader;
		bottom = ctx.area.size.y - 2 * kFieldH - 4;
		x0 = kPad;
		x1 = ctx.area.size.x - kPad;
		int t = 0;
		for (auto& s : p.steps) { startTick.push_back(t); t += s.ticks; }
		startTick.push_back(t);
		perTick = (x1 - x0) / std::max(1, t);
	}
	float x(int tick) const { return x0 + float(tick * perTick); }
	int stepAt(float px) const {  // -1 left of the first, size() right of the last
		if (px < x0) return -1;
		for (size_t i = 0; i + 1 < startTick.size(); i++)
			if (px < x(startTick[i + 1])) return int(i);
		return int(startTick.size()) - 1;
	}
};

struct PatternPage : Page {
	enum class Drag { None, Slice, Velocity, Length, RangeStart, RangeEnd, Field };
	Drag drag = Drag::None;
	size_t dragStep = 0, dragField = 0;
	float pressY = 0, startValue = 0;
	std::shared_ptr<const Loop> dragBase, dragResult;

	const char* title() const override { return "PATTERN"; }

	static const Pattern* patternOf(const DisplayContext& ctx) { return ctx.loop ? ctx.loop->pattern.get() : nullptr; }

	math::Rect cell(const DisplayContext& ctx, size_t i) const {
		const float cw = (ctx.area.size.x - 2 * kPad) / kFieldCols, y = ctx.area.size.y - 2 * kFieldH;
		return math::Rect(Vec(kPad + cw * float(i % size_t(kFieldCols)), y + kFieldH * float(i / size_t(kFieldCols))), Vec(cw - 6, kFieldH));
	}

	static size_t selectedStep(Spools* m, const Pattern& p) {
		return size_t(std::clamp(m->patternStep, 0, int(p.steps.size()) - 1));
	}

	void edit(Spools* m, const std::function<void(Pattern&)>& f) {
		Pattern p = *dragBase->pattern;
		f(p);
		dragResult = m->setPattern(dragBase, std::move(p), "", true);
	}

	static void commit(Spools* m, const Pattern& p, const std::string& name) {
		if (auto l = m->loop()) m->setPattern(l, p, name);
	}

	bool onButton(const DisplayContext& ctx, Vec pos, const widget::Widget::ButtonEvent& e) override {
		Spools* m = ctx.module;
		const Pattern* p = patternOf(ctx);
		if (!m || !p || p->steps.empty() || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) return false;
		dragBase = dragResult = ctx.loop;
		drag = Drag::None;
		pressY = pos.y;
		const size_t sel = selectedStep(m, *p);
		for (size_t fi = 0; fi < stepFields().size(); fi++) {
			if (!cell(ctx, fi).contains(pos)) continue;
			const StepField& f = stepFields()[fi];
			if (f.choice) {
				PatternStep s = p->steps[sel];
				f.set(s, f.get(s) > 0.5f ? 0.f : 1.f);
				Pattern q = *p;
				q.steps[sel] = s;
				commit(m, q, string::f("step %s", f.name));
				return true;
			}
			drag = Drag::Field;
			dragField = fi;
			dragStep = sel;
			startValue = f.get(p->steps[sel]);
			return true;
		}
		const Lane lane(ctx, *p);
		if (pos.y < lane.top || pos.y > lane.bottom) return true;
		int first, n;
		p->range(first, n);
		if (pos.y < lane.top + kTab + 2) {  // range tabs along the top
			if (std::fabs(pos.x - lane.x(lane.startTick[size_t(first)])) <= kGrab * 2) { drag = Drag::RangeStart; return true; }
			if (std::fabs(pos.x - lane.x(lane.startTick[size_t(first + n)])) <= kGrab * 2) { drag = Drag::RangeEnd; return true; }
		}
		const int i = lane.stepAt(pos.x);
		if (i < 0 || i >= int(p->steps.size())) return true;
		dragStep = size_t(i);
		m->patternStep = i;
		if (std::fabs(pos.x - lane.x(lane.startTick[size_t(i) + 1])) <= kGrab) drag = Drag::Length;
		else if (APP->window->getMods() & GLFW_MOD_ALT) { drag = Drag::Velocity; startValue = p->steps[size_t(i)].velocity; }
		else { drag = Drag::Slice; startValue = float(p->steps[size_t(i)].slice); }
		return true;
	}

	bool onDragMove(const DisplayContext& ctx, Vec pos, const widget::Widget::DragMoveEvent&) override {
		Spools* m = ctx.module;
		if (!m || !dragBase || !dragBase->pattern || drag == Drag::None) return false;
		const Pattern& base = *dragBase->pattern;
		if (dragStep >= base.steps.size()) return false;
		const Lane lane(ctx, base);
		const float dy = pressY - pos.y, fine = (APP->window->getMods() & GLFW_MOD_SHIFT) ? 0.1f : 1.f;
		const int slices = int(dragBase->slices.size());
		switch (drag) {
			case Drag::Field: {
				const StepField& f = stepFields()[dragField];
				const float hi = dragField == 0 ? float(slices - 1) : f.hi;
				edit(m, [&](Pattern& p) { f.set(p.steps[dragStep], std::clamp(startValue + dy * f.perPx * fine, f.lo, hi)); });
				break;
			}
			case Drag::Slice:  // one slice per 6 px; below the first is a rest
				edit(m, [&](Pattern& p) { p.steps[dragStep].slice = std::clamp(int(std::lround(startValue + dy / 6)), -1, slices - 1); });
				break;
			case Drag::Velocity:
				edit(m, [&](Pattern& p) { p.steps[dragStep].velocity = std::clamp(startValue + dy * 0.005f * fine, 0.f, 1.f); });
				break;
			case Drag::Length: {
				const int ticks = int(std::lround((pos.x - lane.x(lane.startTick[dragStep])) / lane.perTick));
				edit(m, [&](Pattern& p) { p.steps[dragStep].ticks = std::clamp(ticks, 1, 32); });
				break;
			}
			case Drag::RangeStart:
			case Drag::RangeEnd: {
				const int i = std::clamp(lane.stepAt(pos.x + float(lane.perTick) / 2 * (drag == Drag::RangeStart ? 1 : -1)), 0, int(base.steps.size()) - 1);
				edit(m, [&](Pattern& p) {
					int first, n;
					p.range(first, n);
					int a = drag == Drag::RangeStart ? i : first, b = drag == Drag::RangeEnd ? i : first + n - 1;
					if (a > b) std::swap(a, b);
					p.loopStart = a;
					p.loopEnd = b == int(p.steps.size()) - 1 ? -1 : b;
				});
				break;
			}
			default: return false;
		}
		return true;
	}

	void onDragEnd(const DisplayContext& ctx) override {
		static const char* names[] = {"", "step slice", "step velocity", "step length", "pattern range", "pattern range", "edit step"};
		if (ctx.module && dragBase && dragResult != dragBase) ctx.module->commitEdit(dragBase, dragResult, names[int(drag)], false);
		drag = Drag::None;
		dragBase = dragResult = nullptr;
	}

	// Scroll over a step changes its ratchets.
	bool onHoverScroll(const DisplayContext& ctx, Vec pos, const widget::Widget::HoverScrollEvent& e) override {
		Spools* m = ctx.module;
		const Pattern* p = patternOf(ctx);
		if (!m || !p || e.scrollDelta.y == 0) return false;
		const Lane lane(ctx, *p);
		const int i = lane.stepAt(pos.x);
		if (pos.y < lane.top || pos.y > lane.bottom || i < 0 || i >= int(p->steps.size())) return false;
		Pattern q = *p;
		q.steps[size_t(i)].ratchets = std::clamp(q.steps[size_t(i)].ratchets + (e.scrollDelta.y > 0 ? 1 : -1), 1, 8);
		m->patternStep = i;
		if (q.steps[size_t(i)].ratchets != p->steps[size_t(i)].ratchets) commit(m, q, "step ratchets");
		return true;
	}

	// Double-click: a numeric field resets; a step is duplicated after itself (Ctrl: deleted); past the end adds a rest.
	void onDoubleClick(const DisplayContext& ctx, Vec pos) override {
		Spools* m = ctx.module;
		const Pattern* p = patternOf(ctx);
		if (!m || !p || p->steps.empty()) return;
		const size_t sel = selectedStep(m, *p);
		for (size_t fi = 0; fi < stepFields().size(); fi++)
			if (cell(ctx, fi).contains(pos) && !stepFields()[fi].choice) {
				Pattern q = *p;
				stepFields()[fi].set(q.steps[sel], stepFields()[fi].def);
				commit(m, q, string::f("reset step %s", stepFields()[fi].name));
				return;
			}
		const Lane lane(ctx, *p);
		if (pos.y < lane.top || pos.y > lane.bottom) return;
		const int i = lane.stepAt(pos.x);
		if (i < 0) return;
		if (i >= int(p->steps.size())) {
			commit(m, withStepInserted(*p, p->steps.size(), {-1, 4}), "add step");
			m->patternStep = int(p->steps.size());
		}
		else if (APP->window->getMods() & RACK_MOD_CTRL) commit(m, withStepRemoved(*p, size_t(i)), "delete step");
		else {
			commit(m, withStepInserted(*p, size_t(i) + 1, p->steps[size_t(i)]), "duplicate step");
			m->patternStep = i + 1;
		}
	}

	void draw(const DisplayContext& ctx) override {
		NVGcontext* vg = ctx.vg;
		const float ax = ctx.area.pos.x, ay = ctx.area.pos.y, w = ctx.area.size.x, h = ctx.area.size.y;
		Spools* m = ctx.module;
		const Pattern* p = patternOf(ctx);
		if (!m || !p || p->steps.empty()) {
			ctx.text(ax + w / 2, ay + h / 2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 16, kText, m ? "No pattern yet" : "SPOOLS");
			return;
		}
		const Loop& l = *ctx.loop;
		const size_t ns = l.slices.size(), sel = selectedStep(m, *p);
		int first, n;
		p->range(first, n);
		const Lane lane(ctx, *p);
		static const char* dirs[] = {"forward", "reverse", "ping-pong", "random"};

		const float hy = ay + 15;
		ctx.text(ax + kPad, hy, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kReadout, kText,
			string::f("%zu steps   %.2f beats   play %d–%d %s, %s", p->steps.size(), lane.startTick.back() * PatternStep::kTickBeats,
				first + 1, first + n, p->loopCount ? string::f("×%d", p->loopCount).c_str() : "×∞", dirs[int(p->direction)]));
		ctx.text(ax + w - kPad, hy, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kBody, kDim,
			m->playMode == Spools::kPatternMode ? string::f("step %zu", sel + 1) : "Mode is not Pattern");

		const float top = ay + lane.top, bottom = ay + lane.bottom;
		for (size_t i = 0; i < p->steps.size(); i++) {
			const PatternStep& s = p->steps[i];
			const float xa = ax + lane.x(lane.startTick[i]) + 1, xb = ax + lane.x(lane.startTick[i + 1]) - 1;
			const bool in = int(i) >= first && int(i) < first + n;
			NVGcolor c = kDim;
			if (s.slice >= 0) {
				const Rgb rgb = oklch(0.78f, 0.13f, sliceHue(size_t(wrap(s.slice, int(ns))), ns));
				c = nvgRGBf(rgb.r, rgb.g, rgb.b);
			}
			ctx.fillRect(xa, top + kTab + 2, xb - xa, bottom - top - kTab - 2, withAlpha(c, (in ? 0.25f : 0.08f) + (in ? 0.5f : 0.15f) * s.velocity));
			const int r = std::clamp(s.ratchets, 1, 8);
			for (int j = 1; j < r; j++) ctx.fillRect(xa + float(s.hitAt(j)) * (xb - xa), bottom - 7, 1, 7, kBg);
			if (xb - xa >= 14)
				ctx.text((xa + xb) / 2, top + kTab + 15, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kBody, kBg,
					s.slice < 0 ? "–" : string::f("%d%s", s.slice + 1, s.reverse ? "◂" : ""));
			if (i == sel) {
				nvgBeginPath(vg);
				nvgRect(vg, xa, top + kTab + 2, xb - xa, bottom - top - kTab - 2);
				nvgStrokeColor(vg, kText);
				nvgStrokeWidth(vg, 2);
				nvgStroke(vg);
			}
		}
		// Play range: an amber bracket with grab tabs, as on the LOOP page.
		const float ra = ax + lane.x(lane.startTick[size_t(first)]), rb = ax + lane.x(lane.startTick[size_t(first + n)]);
		const NVGcolor rc = withAlpha(kAccent, n < int(p->steps.size()) ? 0.9f : 0.45f);
		ctx.fillRect(ra, top, rb - ra, 2, rc);
		ctx.fillRect(ra, top, 6, kTab, rc);
		ctx.fillRect(rb - 6, top, 6, kTab, rc);
		for (int c = 0; c < m->activeChannels; c++) {
			const int s = m->playingStep[c];
			if (s < 0 || size_t(s) >= p->steps.size()) continue;
			const float xa = ax + lane.x(lane.startTick[size_t(s)]), xb = ax + lane.x(lane.startTick[size_t(s) + 1]);
			ctx.fillRect(xa, bottom + 1, xb - xa, 3, kAccent);
		}

		const PatternStep& st = p->steps[sel];
		for (size_t fi = 0; fi < stepFields().size(); fi++) {
			const StepField& f = stepFields()[fi];
			math::Rect r = cell(ctx, fi);
			r.pos = r.pos.plus(ctx.area.pos);
			if ((fi + fi / size_t(kFieldCols)) % 2 == 0) ctx.fillRect(r.pos.x - 4, r.pos.y, r.size.x + 8, r.size.y, nvgRGBA(0xff, 0xff, 0xff, 0x08));
			const bool active = drag == Drag::Field && dragField == fi;
			ctx.text(r.pos.x, r.pos.y + 14, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kBody, kDim, f.name);
			ctx.text(r.pos.x + r.size.x, r.pos.y + 14, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kReadout, active ? kAccent : kText, f.show(f.get(st)));
		}
	}
};

}

std::unique_ptr<Page> makePatternPage() { return std::make_unique<PatternPage>(); }
