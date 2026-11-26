#include <osdialog.h>
#include "Spools.hpp"
#include "Display.hpp"

namespace {

// Layout in panel px (36 HP = 540 x 380). Row 1 is the main knob row; CV and its amount sit below each knob.
constexpr float kCol0 = 39, kColStep = 51, kRow1 = 222, kAttY = 258, kCvY = 288, kBottomY = 352;
float col(int i) { return kCol0 + kColStep * i; }

struct Label { float x, y; const char* text; };

// Panel text drawn with NanoVG: nanosvg cannot render SVG text, and this keeps labels crisp at any zoom.
struct PanelLabels : widget::TransparentWidget {
	std::vector<Label> labels;
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 12.f);
		nvgTextLetterSpacing(args.vg, 0.4f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, settings::preferDarkPanels ? nvgRGB(0xe6, 0xe6, 0xe6) : nvgRGB(0x22, 0x22, 0x22));
		for (auto& l : labels) nvgText(args.vg, l.x, l.y, l.text, nullptr);
	}
};

void chooseFile(Spools* module) {
	auto l = module->loop();
	std::string dir = l && !l->sourcePath.empty() ? system::getDirectory(l->sourcePath) : "";
	osdialog_filters* filters = osdialog_filters_parse("Audio and loops:wav,aif,aiff,aifc,w64,flac,mp3,ogg,rx2,rex,rcy;All files:*");
	char* path = osdialog_file(OSDIALOG_OPEN, dir.empty() ? nullptr : dir.c_str(), nullptr, filters);
	osdialog_filters_free(filters);
	if (!path) return;
	module->load(path);
	std::free(path);
}

// A text entry inside a menu: Enter applies it and closes the menu.
struct MenuTextField : ui::TextField {
	std::function<void(const std::string&)> apply;
	MenuTextField(std::string initial, std::function<void(const std::string&)> f) : apply(std::move(f)) {
		box.size.x = 160;
		text = std::move(initial);
		selectAll();
	}
	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			apply(text);
			if (auto* overlay = getAncestorOfType<ui::MenuOverlay>()) overlay->requestDelete();
			e.consume(this);
		}
		if (!e.getTarget()) TextField::onSelectKey(e);
	}
};

// Stays open, so you can click along with the loop.
struct TapItem : ui::MenuItem {
	Spools* m;
	double bpm = 0;
	void onAction(const ActionEvent& e) override {
		bpm = m->tapTempo.tap(system::getTime());
		if (bpm > 0) m->setLoopBpm(bpm);
		e.context->consumed = false;
	}
	void step() override {
		rightText = bpm > 0 ? string::f("%.1f", bpm) : "click on each beat";
		MenuItem::step();
	}
};

void cropMenu(Menu* menu, Spools* m) {
	auto l = m->loop();
	if (!l) return;
	const double sr = l->source->sampleRate, bpm = l->bpm;
	menu->addChild(createIndexSubmenuItem("Units", {"Samples", "Seconds", "Beats"},
		[=] { return size_t(m->cropUnit); }, [=](size_t i) { m->cropUnit = spools::CropSpec::Unit(i); }));
	menu->addChild(createMenuLabel("Start (e.g. 1.5 s, 250 ms, 4 b, 2 bars)"));
	menu->addChild(new MenuTextField(spools::formatLength(l->crop.start, m->cropUnit, sr, bpm), [=](const std::string& t) {
		if (auto v = spools::parseLength(t, m->cropUnit, l->meterNum)) {
			size_t s = size_t(std::clamp(std::llround(v->frames(sr, bpm)), 0LL, (long long)l->source->frames - 1));
			m->setCrop({s, std::min(l->source->frames, s + l->crop.length())});
		}
	}));
	menu->addChild(createMenuLabel("Length (empty = to the end)"));
	menu->addChild(new MenuTextField(spools::formatLength(l->crop.length(), m->cropUnit, sr, bpm), [=](const std::string& t) {
		auto v = spools::parseLength(t, m->cropUnit, l->meterNum);
		size_t e = !v ? l->source->frames : l->crop.start + size_t(std::max(1LL, std::llround(v->frames(sr, bpm))));
		if (v || t.find_first_not_of(" ") == std::string::npos) m->setCrop({l->crop.start, std::min(e, l->source->frames)});
	}));
	menu->addChild(createMenuItem("Whole file", "", [=] { m->setCrop({0, l->source->frames}); }));
}

void tempoMenu(Menu* menu, Spools* m) {
	auto l = m->loop();
	if (!l) return;
	menu->addChild(createMenuLabel(l->bpmKnown ? "Loop BPM" : "Loop BPM (estimated)"));
	menu->addChild(new MenuTextField(string::f("%.2f", l->bpm), [=](const std::string& t) {
		char* end = nullptr;
		double v = std::strtod(t.c_str(), &end);
		if (end != t.c_str()) m->setLoopBpm(v);
	}));
	menu->addChild(createMenuItem("Halve", string::f("%.2f", l->bpm / 2), [=] { m->setLoopBpm(l->bpm / 2); }));
	menu->addChild(createMenuItem("Double", string::f("%.2f", l->bpm * 2), [=] { m->setLoopBpm(l->bpm * 2); }));
	TapItem* tap = createMenuItem<TapItem>("Tap tempo");
	tap->m = m;
	menu->addChild(tap);
}

// Each change re-slices right away, so the menu doubles as a preview.
void slicingMenu(Menu* menu, Spools* m) {
	auto choice = [=](const char* name, std::vector<std::string> labels, std::vector<double> values, auto get, auto set) {
		menu->addChild(createIndexSubmenuItem(name, labels,
			[=] {
				double v = get();
				size_t best = 0;
				for (size_t i = 1; i < values.size(); i++)
					if (std::fabs(values[i] - v) < std::fabs(values[best] - v)) best = i;
				return best;
			},
			[=](size_t i) { set(values[i]); m->reslice(); }));
	};
	choice("Mode", {"Onsets", "Grid", "Hybrid (grid + strong hits)"}, {0, 1, 2},
		[=] { return double(m->slicing.mode); }, [=](double v) { m->slicing.mode = spools::SliceMode(int(v)); });
	choice("Grid", {"1/4", "1/8", "1/16", "1/32", "1/8 triplet", "1/16 triplet"}, {1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6},
		[=] { return m->slicing.gridBeats; }, [=](double v) { m->slicing.gridBeats = v; });
	std::vector<std::string> pct = {"0%", "25%", "50%", "75%", "100%"};
	std::vector<double> frac = {0, 0.25, 0.5, 0.75, 1};
	choice("Sensitivity", pct, frac, [=] { return double(m->slicing.sensitivity); }, [=](double v) { m->slicing.sensitivity = float(v); });
	choice("Grid snap", pct, frac, [=] { return double(m->slicing.snap); }, [=](double v) { m->slicing.snap = float(v); });
	menu->addChild(createMenuItem("Re-slice now", "", [=] { m->reslice(); }));
}

}

struct SpoolsWidget : ModuleWidget {
	SpoolsWidget(Spools* module) {
		setModule(module);
		setPanel(createPanel<ThemedSvgPanel>(asset::plugin(pluginInstance, "res/Spools.svg"), asset::plugin(pluginInstance, "res/Spools-dark.svg")));

		auto* display = new SpoolsDisplay;
		display->module = module;
		display->box.pos = Vec(12, 16);
		display->box.size = Vec(516, 164);
		addChild(display);

		auto* labels = new PanelLabels;
		labels->box.size = box.size;

		using S = Spools;
		struct Column { int param, att, input; const char* label; };
		const Column cols[] = {
			{S::START_PARAM, S::START_ATT_PARAM, S::START_INPUT, "SLICE"},
			{S::LENGTH_PARAM, S::LENGTH_ATT_PARAM, S::LENGTH_INPUT, "LENGTH"},
			{S::START_TRIM_PARAM, S::START_TRIM_ATT_PARAM, S::START_TRIM_INPUT, "IN"},
			{S::END_TRIM_PARAM, S::END_TRIM_ATT_PARAM, S::END_TRIM_INPUT, "OUT"},
			{S::PITCH_PARAM, S::PITCH_ATT_PARAM, S::PITCH_INPUT, "PITCH"},
			{S::VOLUME_PARAM, S::VOLUME_ATT_PARAM, S::VOLUME_INPUT, "LEVEL"},
			{S::OVERLAP_PARAM, S::OVERLAP_ATT_PARAM, S::OVERLAP_INPUT, "XFADE"},
			{S::DURATION_PARAM, -1, -1, "DUR"},
			{S::QUANTIZE_PARAM, S::QUANTIZE_ATT_PARAM, S::QUANTIZE_INPUT, "QUANT"},
			{S::BPM_PARAM, -1, -1, "BPM"},
		};
		for (int i = 0; i < 10; i++) {
			addParam(createParamCentered<RoundBlackKnob>(Vec(col(i), kRow1), module, cols[i].param));
			labels->labels.push_back({col(i), kRow1 - 24, cols[i].label});
			if (cols[i].att >= 0) {
				addParam(createParamCentered<Trimpot>(Vec(col(i), kAttY), module, cols[i].att));
				addInput(createInputCentered<ThemedPJ301MPort>(Vec(col(i), kCvY), module, cols[i].input));
			}
		}
		addParam(createParamCentered<CKSS>(Vec(col(7), kCvY - 6), module, S::TAIL_PARAM));
		labels->labels.push_back({col(7), kAttY - 2, "CUT"});
		labels->labels.push_back({col(7), kCvY + 22, "RING"});
		addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col(9), kCvY - 6), module, S::DIRECTION_PARAM));
		labels->labels.push_back({col(9), kAttY - 2, "DIR"});

		const float inX[] = {40, 92, 144};
		const int ins[] = {S::VOCT_INPUT, S::GATE_INPUT, S::CLOCK_INPUT};
		const char* inLabels[] = {"V/OCT", "GATE", "CLOCK"};
		for (int i = 0; i < 3; i++) {
			addInput(createInputCentered<ThemedPJ301MPort>(Vec(inX[i], kBottomY), module, ins[i]));
			labels->labels.push_back({inX[i], kBottomY - 18, inLabels[i]});
		}
		const float regX[] = {203, 250, 297};
		const int regs[] = {S::REGION_START_PARAM, S::REGION_END_PARAM, S::LOOP_COUNT_PARAM};
		const char* regLabels[] = {"FROM", "TO", "LOOPS"};
		for (int i = 0; i < 3; i++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(Vec(regX[i], kBottomY), module, regs[i]));
			labels->labels.push_back({regX[i], kBottomY - 18, regLabels[i]});
		}
		const int outs[] = {S::LEFT_OUTPUT, S::RIGHT_OUTPUT, S::SLICE_OUTPUT, S::TRIG_OUTPUT, S::EOC_OUTPUT};
		const char* outLabels[] = {"L", "R", "SLICE", "TRIG", "EOC"};
		for (int i = 0; i < 5; i++) {
			float x = 352 + 38 * i;
			addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(x, kBottomY), module, outs[i]));
			labels->labels.push_back({x, kBottomY - 18, outLabels[i]});
		}
		addChild(labels);
	}

	void appendContextMenu(Menu* menu) override {
		auto* m = getModule<Spools>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load sample…", "", [=] { chooseFile(m); }));
		menu->addChild(createSubmenuItem("Slice evenly", "", [=](Menu* sub) {
			for (int n : {1, 2, 4, 8, 16, 32, 64})
				sub->addChild(createMenuItem(string::f("%d slices", n), "", [=] { m->sliceEvenly(size_t(n)); }));
		}));
		menu->addChild(createSubmenuItem("Crop", "", [=](Menu* sub) { cropMenu(sub, m); }));
		menu->addChild(createSubmenuItem("Loop tempo", "", [=](Menu* sub) { tempoMenu(sub, m); }));
		menu->addChild(createSubmenuItem("Auto-slice", "", [=](Menu* sub) { slicingMenu(sub, m); }));
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
