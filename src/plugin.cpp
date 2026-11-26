#include <rack.hpp>

using namespace rack;

struct Spools : Module {
	Spools() {
		config(0, 0, 0, 0);
	}
};

struct SpoolsWidget : ModuleWidget {
	SpoolsWidget(Spools* module) {
		setModule(module);
		box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
	}
};

void init(Plugin* plugin) {
	plugin->addModel(createModel<Spools, SpoolsWidget>("Spools"));
}
