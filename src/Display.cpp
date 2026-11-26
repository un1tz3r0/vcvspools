#include "Display.hpp"

using namespace theme;

namespace {
constexpr float kTabW = 84;
}

SpoolsDisplay::SpoolsDisplay() {
	pages.push_back(makeLoopPage());
	pages.push_back(makeSlicePage());
	pages.push_back(makeSettingsPage());
}

int& SpoolsDisplay::current() {
	int& p = module ? module->displayPage : fallbackPage;
	if (p < 0 || p >= int(pages.size())) p = 0;
	return p;
}

DisplayContext SpoolsDisplay::context(NVGcontext* vg) {
	DisplayContext c;
	c.module = module;
	c.loop = module ? module->loop() : nullptr;
	c.vg = vg;
	c.area = math::Rect(Vec(0, kTabH), Vec(box.size.x, box.size.y - kTabH));
	return c;
}

float SpoolsDisplay::tabRight(int i) const { return kPad + kTabW * float(i + 1); }

math::Rect SpoolsDisplay::toolbar() const {
	float x = tabRight(int(pages.size()) - 1) + 8;
	return math::Rect(Vec(x, 0), Vec(box.size.x - kPad - x, kTabH));
}

void SpoolsDisplay::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 1) return OpaqueWidget::drawLayer(args, layer);
	NVGcontext* vg = args.vg;
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 6);
	nvgFillColor(vg, kBg);
	nvgFill(vg);
	std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
	if (!font) return;
	nvgFontFaceId(vg, font->handle);

	DisplayContext ctx = context(vg);
	const int cur = current();
	for (int i = 0; i < int(pages.size()); i++) {
		float x = tabRight(i) - kTabW;
		if (i == cur) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x, 3, kTabW - 4, kTabH - 4, 3);
			nvgFillColor(vg, kPanel);
			nvgFill(vg);
			ctx.fillRect(x + 6, kTabH - 3, kTabW - 16, 2, kAccent);
		}
		ctx.text(x + (kTabW - 4) / 2, 16, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, kReadout, i == cur ? kText : kDim, pages[i]->title());
	}

	pages[cur]->drawToolbar(ctx, toolbar());
	nvgSave(vg);
	nvgScissor(vg, ctx.area.pos.x, ctx.area.pos.y, ctx.area.size.x, ctx.area.size.y);
	pages[cur]->draw(ctx);
	nvgRestore(vg);
}

void SpoolsDisplay::onButton(const ButtonEvent& e) {
	if (e.button == GLFW_MOUSE_BUTTON_RIGHT) return;  // leave right-clicks to the module's context menu
	if (e.action == GLFW_PRESS) dragPos = e.pos;
	e.consume(this);  // owning the press is what lets this widget receive the drag
	if (e.pos.y < kTabH) {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
			for (int i = 0; i < int(pages.size()); i++)
				if (e.pos.x >= tabRight(i) - kTabW && e.pos.x < tabRight(i)) current() = i;
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) pages[current()]->onToolbarButton(context(), toolbar(), e.pos);
		return;
	}
	DisplayContext ctx = context();
	pages[current()]->onButton(ctx, e.pos.minus(ctx.area.pos), e);
}

void SpoolsDisplay::onDragStart(const DragStartEvent& e) {}  // dragPos was set by the press in onButton

void SpoolsDisplay::onDragMove(const DragMoveEvent& e) {
	dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
	DisplayContext ctx = context();
	pages[current()]->onDragMove(ctx, dragPos.minus(ctx.area.pos), e);
}

void SpoolsDisplay::onHoverScroll(const HoverScrollEvent& e) {
	DisplayContext ctx = context();
	if (pages[current()]->onHoverScroll(ctx, e.pos.minus(ctx.area.pos), e)) e.consume(this);
	else OpaqueWidget::onHoverScroll(e);
}

void SpoolsDisplay::onDragEnd(const DragEndEvent& e) { pages[current()]->onDragEnd(context()); }

void SpoolsDisplay::onDoubleClick(const DoubleClickEvent& e) {
	if (dragPos.y < kTabH) return;
	DisplayContext ctx = context();
	pages[current()]->onDoubleClick(ctx, dragPos.minus(ctx.area.pos));
}
