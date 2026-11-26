#pragma once
#include <algorithm>
#include <cmath>

namespace spools {

// The panel is resizable: 40 HP fits every control, and a wider panel gives the display the extra room.
constexpr int kPanelMinHp = 40, kPanelMaxHp = 120;

inline int clampPanelHp(int hp) { return std::clamp(hp, kPanelMinHp, kPanelMaxHp); }

// Width in HP for a dragged width in px, rounded to the nearest HP.
inline int snapPanelHp(double widthPx, double hpPx) { return clampPanelHp(int(std::lround(widthPx / hpPx))); }

}
