#include <osdialog.h>
#include "Spools.hpp"
#include "Display.hpp"
#include "Settings.hpp"

namespace {

// Layout in panel px (40 HP = 600 x 380), top to bottom: display; tone strip (filter, envelopes);
// main knob row with a CV amount and jack under each; inputs and outputs. The per-slice outputs sit in a
// column measured from the right edge. A wider panel only widens the display and the gap before that column.
constexpr float kDisplayY = 14, kDisplayH = 136, kMargin = 12;
constexpr float kSliceColInset = 38, kSliceColY0 = 190, kSliceColStep = 54;  // SEND A, SEND B, CV A, CV B
constexpr float kStripY = 186, kStripLabelY = 168, kStripStep = 30, kStripGap = 6;
constexpr float kCol0 = 39, kColStep = 51, kRow1 = 252, kAttY = 287, kCvY = 312, kBottomY = 360;
float col(int i) { return kCol0 + kColStep * i; }

struct Label { float x, y; const char* text; bool fromRight = false; };  // fromRight: x is from the right edge

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
		for (auto& l : labels) nvgText(args.vg, l.fromRight ? box.size.x - l.x : l.x, l.y, l.text, nullptr);
	}
};

// The panel, drawn in NanoVG so that it can take any width: background, display well and control boxes.
struct PanelBackground : widget::Widget {
	void draw(const DrawArgs& args) override {
		const bool dark = settings::preferDarkPanels;
		const float w = box.size.x, h = box.size.y;
		auto rect = [&](float x, float y, float rw, float rh, float r, NVGcolor c) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x, y, rw, rh, r);
			nvgFillColor(args.vg, c);
			nvgFill(args.vg);
		};
		const NVGcolor boxColor = dark ? nvgRGB(0x2f, 0x2f, 0x30) : nvgRGB(0xd6, 0xd6, 0xd4);
		rect(0, 0, w, h, 0, dark ? nvgRGB(0x26, 0x26, 0x27) : nvgRGB(0xe4, 0xe4, 0xe2));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.5f, 0.5f, w - 1, h - 1);
		nvgStrokeColor(args.vg, dark ? nvgRGB(0x3a, 0x3a, 0x3b) : nvgRGB(0xc8, 0xc8, 0xc6));
		nvgStrokeWidth(args.vg, 1);
		nvgStroke(args.vg);
		rect(kMargin, kDisplayY, w - 2 * kMargin, kDisplayH, 6, nvgRGB(0x0e, 0x0e, 0x10));
		rect(12, 156, 516, 62, 5, dark ? nvgRGB(0x2b, 0x2b, 0x2c) : nvgRGB(0xdc, 0xdc, 0xda));
		rect(12, 326, 240, 48, 5, boxColor);
		rect(274, 326, 254, 48, 5, boxColor);
		rect(w - kSliceColInset - 26, 156, 52, 218, 5, boxColor);
	}
};

// Drag the right edge to resize, in whole HP; the rack refuses widths that would overlap a neighbour.
struct ResizeHandle : widget::OpaqueWidget {
	Spools* module = nullptr;
	Vec dragStart;
	float startWidth = 0;
	ResizeHandle() { box.size = Vec(8, RACK_GRID_HEIGHT); }
	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		dragStart = APP->scene->rack->getMousePos();
		startWidth = getAncestorOfType<app::ModuleWidget>()->box.size.x;
	}
	void onDragMove(const DragMoveEvent& e) override {
		auto* mw = getAncestorOfType<app::ModuleWidget>();
		const int hp = spools::snapPanelHp(startWidth + APP->scene->rack->getMousePos().x - dragStart.x, RACK_GRID_WIDTH);
		const math::Rect old = mw->box;
		mw->box.size.x = hp * RACK_GRID_WIDTH;
		if (!APP->scene->rack->requestModulePos(mw, mw->box.pos)) mw->box = old;
		else if (module) module->panelHp = hp;
	}
	void draw(const DrawArgs& args) override {
		for (float x : {2.f, 5.f}) {
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x + 0.5f, box.size.y / 2 - 14);
			nvgLineTo(args.vg, x + 0.5f, box.size.y / 2 + 14);
			nvgStrokeColor(args.vg, settings::preferDarkPanels ? nvgRGB(0x6a, 0x6a, 0x6c) : nvgRGB(0x9a, 0x9a, 0x98));
			nvgStrokeWidth(args.vg, 1);
			nvgStroke(args.vg);
		}
	}
};

// A knob that can edit the selected slices: Shift-click first sets them all to their mean, and a ring
// shows the spread of the selection's values.
template <class TBase>
struct SliceKnob : TBase {
	void onButton(const widget::Widget::ButtonEvent& e) override {
		auto* m = dynamic_cast<Spools*>(this->module);
		if (m && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT)
			m->flattenSelection(this->paramId);
		TBase::onButton(e);
	}
	void drawLayer(const widget::Widget::DrawArgs& args, int layer) override {
		TBase::drawLayer(args, layer);
		auto* m = dynamic_cast<Spools*>(this->module);
		float lo, hi;
		if (layer != 1 || !m || !m->selectionSpread(this->paramId, lo, hi)) return;
		ParamQuantity* q = this->getParamQuantity();
		auto angle = [&](float v) {
			float t = (v - q->getMinValue()) / (q->getMaxValue() - q->getMinValue());
			return float(this->minAngle + t * (this->maxAngle - this->minAngle) - M_PI / 2);
		};
		const Vec c = this->box.size.div(2);
		nvgBeginPath(args.vg);
		nvgArc(args.vg, c.x, c.y, c.x + 2.5f, angle(lo), std::max(angle(hi), angle(lo) + 0.08f), NVG_CW);
		nvgStrokeColor(args.vg, nvgRGB(0xff, 0xc8, 0x57));
		nvgStrokeWidth(args.vg, 2.5f);
		nvgStroke(args.vg);
	}
};

bool editsSlices(int id) {
	for (int p : {Spools::START_TRIM_PARAM, Spools::END_TRIM_PARAM, Spools::PITCH_PARAM, Spools::VOLUME_PARAM,
		Spools::OVERLAP_PARAM, Spools::CUTOFF_PARAM, Spools::RES_PARAM})
		if (p == id) return true;
	return false;
}

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




}

struct SpoolsWidget : ModuleWidget {
	SpoolsWidget(Spools* module) {
		setModule(module);
		auto* panel = new PanelBackground;
		panel->box.size = Vec(spools::kPanelMinHp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
		setPanel(panel);

		display = new SpoolsDisplay;
		display->module = module;
		display->box.pos = Vec(kMargin, kDisplayY);
		display->box.size = Vec(box.size.x - 2 * kMargin, kDisplayH);
		addChild(display);

		labels = new PanelLabels;
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
			if (editsSlices(cols[i].param)) addParam(createParamCentered<SliceKnob<RoundBlackKnob>>(Vec(col(i), kRow1), module, cols[i].param));
			else addParam(createParamCentered<RoundBlackKnob>(Vec(col(i), kRow1), module, cols[i].param));
			labels->labels.push_back({col(i), kRow1 - 24, cols[i].label});
			if (cols[i].att >= 0) {
				addParam(createParamCentered<Trimpot>(Vec(col(i), kAttY), module, cols[i].att));
				addInput(createInputCentered<ThemedPJ301MPort>(Vec(col(i), kCvY), module, cols[i].input));
			}
		}
		// Region knobs sit under the two columns without CV; direction, tail and loop count are in the menu.
		addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col(7), kCvY - 6), module, S::REGION_START_PARAM));
		labels->labels.push_back({col(7), kAttY, "FROM"});
		addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col(9), kCvY - 6), module, S::REGION_END_PARAM));
		labels->labels.push_back({col(9), kAttY, "TO"});

		// Tone strip: groups of controls with one label centred over each group.
		float x = 32;
		auto group = [&](const char* label, std::vector<std::function<void(Vec)>> items) {
			float x0 = x;
			for (auto& add : items) { add(Vec(x, kStripY)); x += kStripStep; }
			labels->labels.push_back({(x0 + x - kStripStep) / 2, kStripLabelY, label});
			x += kStripGap;
		};
		auto knob = [&](int id) {
			return [=](Vec p) {
				if (editsSlices(id)) addParam(createParamCentered<SliceKnob<RoundSmallBlackKnob>>(p, module, id));
				else addParam(createParamCentered<RoundSmallBlackKnob>(p, module, id));
			};
		};
		auto trim = [&](int id) { return [=](Vec p) { addParam(createParamCentered<Trimpot>(p, module, id)); }; };
		auto jack = [&](int id) { return [=](Vec p) { addInput(createInputCentered<ThemedPJ301MPort>(p, module, id)); }; };
		group("FILTER", {knob(S::FILTER_TYPE_PARAM)});
		group("CUTOFF", {knob(S::CUTOFF_PARAM), trim(S::CUTOFF_ATT_PARAM), jack(S::CUTOFF_INPUT)});
		group("RES", {knob(S::RES_PARAM), trim(S::RES_ATT_PARAM), jack(S::RES_INPUT)});
		group("F.ENV", {knob(S::FILTER_ENV_AMOUNT_PARAM)});
		auto adsr = [&](const char* label, int first) {
			static const char* letters[] = {"A", "D", "S", "R"};
			for (int i = 0; i < 4; i++) labels->labels.push_back({x + kStripStep * i, kStripY + 24, letters[i]});
			group(label, {knob(first), knob(first + 1), knob(first + 2), knob(first + 3)});
		};
		adsr("FILTER ENV", S::FILTER_A_PARAM);
		adsr("AMP ENV", S::AMP_A_PARAM);

		const int ins[] = {S::VOCT_INPUT, S::GATE_INPUT, S::CLOCK_INPUT, S::RUN_INPUT, S::CONT_INPUT, S::BAR_INPUT};
		const char* inLabels[] = {"V/OCT", "GATE", "CLOCK", "RUN", "CONT", "BAR"};
		for (int i = 0; i < 6; i++) {
			float x = 32 + 40 * i;
			addInput(createInputCentered<ThemedPJ301MPort>(Vec(x, kBottomY), module, ins[i]));
			labels->labels.push_back({x, kBottomY - 18, inLabels[i]});
		}
		const int outs[] = {S::LEFT_OUTPUT, S::RIGHT_OUTPUT, S::SLICE_OUTPUT, S::TRIG_OUTPUT, S::EOC_OUTPUT,
			S::AMP_ENV_OUTPUT, S::FILTER_ENV_OUTPUT};
		const char* outLabels[] = {"L", "R", "SLICE", "TRIG", "EOC", "AENV", "FENV"};
		for (int i = 0; i < 7; i++) {
			float x = 292 + 36 * i;
			addOutput(createOutputCentered<ThemedPJ301MPort>(Vec(x, kBottomY), module, outs[i]));
			labels->labels.push_back({x, kBottomY - 18, outLabels[i]});
		}
		// Per-slice outputs: each slice sets its own level into them on the SLICE page.
		const int sliceOuts[] = {S::SEND_A_OUTPUT, S::SEND_B_OUTPUT, S::CV_A_OUTPUT, S::CV_B_OUTPUT};
		const char* sliceLabels[] = {"SEND A", "SEND B", "CV A", "CV B"};
		for (int i = 0; i < 4; i++) {
			const float y = kSliceColY0 + kSliceColStep * float(i);
			auto* port = createOutputCentered<ThemedPJ301MPort>(Vec(box.size.x - kSliceColInset, y), module, sliceOuts[i]);
			addOutput(port);
			rightColumn.push_back(port);
			labels->labels.push_back({kSliceColInset, y - 18, sliceLabels[i], true});
		}
		addChild(labels);

		handle = new ResizeHandle;
		handle->module = module;
		addChild(handle);
		layoutWidth();
	}

	SpoolsDisplay* display;
	PanelLabels* labels;
	ResizeHandle* handle;
	std::vector<widget::Widget*> rightColumn;
	float laidOutWidth = 0;

	// Fit everything that depends on the width to box.size.x.
	void layoutWidth() {
		const float w = laidOutWidth = box.size.x;
		getPanel()->box.size.x = w;
		labels->box.size = box.size;
		display->box.size.x = w - 2 * kMargin;
		handle->box.pos.x = w - handle->box.size.x;
		for (auto* p : rightColumn) p->box.pos.x = w - kSliceColInset - p->box.size.x / 2;
	}

	void step() override {
		if (auto* m = getModule<Spools>()) {
			if (const float w = m->panelHp * RACK_GRID_WIDTH; w != laidOutWidth) {
				box.size.x = w;
				layoutWidth();
			}
			m->flushHistory();
			m->ensureFeatures();
			m->ensurePattern();
			m->syncSelectionKnobs();
		}
		ModuleWidget::step();
	}

	// Pattern mode's menu actions (DESIGN §11); each is one undo step.
	static void patternMenu(Menu* menu, Spools* m) {
		auto l = m->loop();
		if (!l || !l->pattern) { menu->addChild(createMenuLabel("Load a sample first")); return; }
		auto act = [=](const char* name, std::function<spools::Pattern(const spools::Pattern&)> f) {
			menu->addChild(createMenuItem(name, "", [=] {
				if (auto cur = m->loop(); cur && cur->pattern) m->setPattern(cur, f(*cur->pattern), name);
			}));
		};
		act("Init from loop", [=](const spools::Pattern&) { return spools::initFromLoop(*m->loop()); });
		act("Shuffle slices", [](const spools::Pattern& p) { return spools::shuffled(p, uint32_t(random::u32())); });
		act("Reverse steps", [](const spools::Pattern& p) { return spools::reversed(p); });
		act("Add rest at end", [](const spools::Pattern& p) { return spools::withStepInserted(p, p.steps.size(), {-1, 4}); });
		act("Delete selected step", [=](const spools::Pattern& p) { return spools::withStepRemoved(p, size_t(std::max(0, m->patternStep))); });
		act("Play every step", [](const spools::Pattern& p) { auto q = p; q.loopStart = 0; q.loopEnd = -1; return q; });
		menu->addChild(createIndexSubmenuItem("Direction", {"Forward", "Reverse", "Ping-pong", "Random"},
			[=] { auto c = m->loop(); return c && c->pattern ? size_t(c->pattern->direction) : 0; },
			[=](size_t i) {
				auto c = m->loop();
				if (!c || !c->pattern) return;
				auto q = *c->pattern;
				q.direction = spools::Direction(i);
				m->setPattern(c, q, "pattern direction");
			}));
		std::vector<std::string> counts = {"∞"};
		for (int i = 1; i <= 16; i++) counts.push_back(std::to_string(i));
		menu->addChild(createIndexSubmenuItem("Loop count", counts,
			[=] { auto c = m->loop(); return c && c->pattern ? size_t(std::min(c->pattern->loopCount, 16)) : 0; },
			[=](size_t i) {
				auto c = m->loop();
				if (!c || !c->pattern) return;
				auto q = *c->pattern;
				q.loopCount = int(i);
				m->setPattern(c, q, "pattern loop count");
			}));
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
		menu->addChild(createSubmenuItem("Pattern", "", [=](Menu* sub) { patternMenu(sub, m); }));
		menu->addChild(new MenuSeparator);
		appendSettingsMenus(menu, m);
	}
};

Model* modelSpools = createModel<Spools, SpoolsWidget>("Spools");
