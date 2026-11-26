#pragma once
#include <functional>
#include "Spools.hpp"

// One global option, shared by the context menu and the display's Settings page so both stay in step.
struct Setting {
	const char* group;
	std::string name;
	std::vector<std::string> options;
	std::function<size_t()> get;
	std::function<void(size_t)> set;
	std::string value() const { size_t i = get(); return i < options.size() ? options[i] : "?"; }
};

std::vector<Setting> moduleSettings(Spools* m);
// Adds one submenu per setting group ("Playback", "Slicing", ...) to a context menu.
void appendSettingsMenus(ui::Menu* menu, Spools* m);
// A popup menu of a setting's options at the mouse.
void openSettingMenu(const Setting& s);
