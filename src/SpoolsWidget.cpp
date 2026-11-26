#include <osdialog.h>
#include "Spools.hpp"

static void chooseFile(Spools* module) {
	auto l = module->loop();
	std::string dir = l && !l->sourcePath.empty() ? system::getDirectory(l->sourcePath) : "";
	osdialog_filters* filters = osdialog_filters_parse(
		"Audio and loops:wav,aif,aiff,aifc,w64,flac,mp3,ogg,rx2,rex,rcy;All files:*");
	char* path = osdialog_file(OSDIALOG_OPEN, dir.empty() ? nullptr : dir.c_str(), nullptr, filters);
	osdialog_filters_free(filters);
	if (!path) return;
	module->load(path);
	std::free(path);
}

struct SpoolsWidget : ModuleWidget {
	SpoolsWidget(Spools* module) {
		setModule(module);
		box.size = Vec(36 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = getModule<Spools>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load sample…", "", [=] { chooseFile(m); }));
		menu->addChild(createSubmenuItem("Slice evenly", "", [=](Menu* sub) {
			for (int n : {1, 2, 4, 8, 16, 32, 64})
				sub->addChild(createMenuItem(string::f("%d slices", n), "", [=] { m->sliceEvenly(size_t(n)); }));
		}));
		static const std::vector<int> ppqns = {1, 2, 4, 8, 24, 48, 96};
		std::vector<std::string> labels;
		for (int p : ppqns) labels.push_back(string::f("%d PPQN", p));
		menu->addChild(createIndexSubmenuItem("Clock resolution", labels,
			[=] { return size_t(std::find(ppqns.begin(), ppqns.end(), m->ppqn.load()) - ppqns.begin()); },
			[=](size_t i) { m->ppqn = ppqns[i]; }));
		menu->addChild(createBoolMenuItem("Eco interpolation (8 taps)", "", [=] { return m->eco.load(); }, [=](bool b) { m->eco = b; }));
		menu->addChild(createBoolMenuItem("Embed audio in patch", "", [=] { return m->embedAudio.load(); }, [=](bool b) { m->embedAudio = b; }));
	}
};

Model* modelSpools = createModel<Spools, SpoolsWidget>("Spools");
