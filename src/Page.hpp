#pragma once
#include <memory>
#include "Spools.hpp"

// Shared state for drawing and editing a display page (DESIGN §8). `area` is the page's rect in display px.
struct DisplayContext {
	Spools* module = nullptr;
	std::shared_ptr<const spools::Loop> loop;
	NVGcontext* vg = nullptr;  // null outside drawing
	math::Rect area;

	void text(float x, float y, int align, float size, NVGcolor c, const std::string& s) const {
		nvgFontSize(vg, size);
		nvgTextAlign(vg, align);
		nvgFillColor(vg, c);
		nvgText(vg, x, y, s.c_str(), nullptr);
	}
	void fillRect(float x, float y, float w, float h, NVGcolor c) const {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
};

namespace theme {
// Light content on a dark neutral background; kText and kDim both exceed 7:1 contrast on kBg.
const NVGcolor kBg = nvgRGB(0x0e, 0x0e, 0x10), kPanel = nvgRGB(0x1c, 0x1c, 0x20), kText = nvgRGB(0xe8, 0xe8, 0xea),
	kDim = nvgRGB(0xa8, 0xa8, 0xae), kAccent = nvgRGB(0xff, 0xc8, 0x57);
constexpr float kPad = 8, kTabH = 22;
constexpr float kBody = 12, kReadout = 13;  // px; DESIGN §8 minimums are 11 and 13
}

struct Page {
	virtual ~Page() = default;
	virtual const char* title() const = 0;
	virtual void draw(const DisplayContext& ctx) = 0;
	// Positions are relative to the page area. Return true when the event was used.
	virtual bool onButton(const DisplayContext&, Vec, const widget::Widget::ButtonEvent&) { return false; }
	virtual bool onDragMove(const DisplayContext&, Vec, const widget::Widget::DragMoveEvent&) { return false; }
	virtual bool onHoverScroll(const DisplayContext&, Vec, const widget::Widget::HoverScrollEvent&) { return false; }
};

std::unique_ptr<Page> makeLoopPage();
std::unique_ptr<Page> makeSettingsPage();
