#pragma once
#include "Spools.hpp"

// Waveform, slices, region and playheads. Drawn on the light layer so it stays lit in a dimmed room.
struct SpoolsDisplay : widget::Widget {
	Spools* module = nullptr;
	void drawLayer(const DrawArgs& args, int layer) override;
};
