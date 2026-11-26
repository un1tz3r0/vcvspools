#pragma once
#include <memory>
#include <vector>
#include "Page.hpp"

// The module's screen: a tab row choosing one of several pages. Drawn on the light layer so it stays lit
// in a dimmed room.
struct SpoolsDisplay : widget::OpaqueWidget {
	Spools* module = nullptr;
	std::vector<std::unique_ptr<Page>> pages;
	int fallbackPage = 0;  // used when there is no module (library preview)

	SpoolsDisplay();
	void drawLayer(const DrawArgs& args, int layer) override;
	void onButton(const ButtonEvent& e) override;
	void onDragStart(const DragStartEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override;

private:
	Vec dragPos;
	int& current();
	DisplayContext context(NVGcontext* vg = nullptr);
	float tabRight(int i) const;
};
