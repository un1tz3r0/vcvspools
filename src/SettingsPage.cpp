#include "Page.hpp"
#include "Settings.hpp"

using namespace theme;

namespace {

// Global options in three columns of name/value rows; clicking a row opens its choices.
struct SettingsPage : Page {
	static constexpr int kCols = 3;
	static constexpr float kRowH = 19, kTop = 6;

	const char* title() const override { return "SETTINGS"; }

	math::Rect cell(const DisplayContext& ctx, int i, int rows) const {
		float cw = (ctx.area.size.x - 2 * kPad) / kCols;
		return math::Rect(Vec(kPad + cw * float(i / rows), kTop + kRowH * float(i % rows)), Vec(cw - 10, kRowH));
	}

	void draw(const DisplayContext& ctx) override {
		if (!ctx.module) return;
		auto settings = moduleSettings(ctx.module);
		const int rows = int((settings.size() + kCols - 1) / kCols);
		for (int i = 0; i < int(settings.size()); i++) {
			math::Rect r = cell(ctx, i, rows);
			r.pos = r.pos.plus(ctx.area.pos);
			if (i % 2 == 0) ctx.fillRect(r.pos.x - 4, r.pos.y, r.size.x + 8, r.size.y, nvgRGBA(0xff, 0xff, 0xff, 0x08));
			float y = r.pos.y + 14;
			ctx.text(r.pos.x, y, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, kBody, kDim, settings[size_t(i)].name);
			ctx.text(r.pos.x + r.size.x, y, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, kReadout, kText, settings[size_t(i)].value());
		}
	}

	bool onButton(const DisplayContext& ctx, Vec pos, const widget::Widget::ButtonEvent& e) override {
		if (!ctx.module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) return false;
		auto settings = moduleSettings(ctx.module);
		const int rows = int((settings.size() + kCols - 1) / kCols);
		for (int i = 0; i < int(settings.size()); i++)
			if (cell(ctx, i, rows).contains(pos)) {
				openSettingMenu(settings[size_t(i)]);
				return true;
			}
		return false;
	}
};

}

std::unique_ptr<Page> makeSettingsPage() { return std::make_unique<SettingsPage>(); }
