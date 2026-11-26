#include "Settings.hpp"

namespace {

// A choice over a list of values: the option nearest the current value is shown as selected.
template <class T>
Setting choice(const char* group, std::string name, std::vector<std::string> labels, std::vector<T> values,
	std::function<T()> get, std::function<void(T)> set) {
	return {group, std::move(name), std::move(labels),
		[=] {
			T v = get();
			size_t best = 0;
			for (size_t i = 1; i < values.size(); i++)
				if (std::fabs(double(values[i] - v)) < std::fabs(double(values[best] - v))) best = i;
			return best;
		},
		[=](size_t i) { set(values[i]); }};
}

Setting paramChoice(const char* group, std::string name, Spools* m, int id, std::vector<std::string> labels) {
	return {group, std::move(name), std::move(labels),
		[=] { return size_t(m->params[id].getValue()); }, [=](size_t i) { m->params[id].setValue(float(i)); }};
}

}

std::vector<Setting> moduleSettings(Spools* m) {
	std::vector<Setting> s;
	s.push_back({"Playback", "Mode", {"Note", "Loop"}, [=] { return size_t(m->playMode.load()); }, [=](size_t i) { m->playMode = int(i); }});
	s.push_back(paramChoice("Playback", "Direction", m, Spools::DIRECTION_PARAM, {"Forward", "Reverse", "Ping-pong", "Random"}));
	std::vector<std::string> tails;
	for (auto& t : tailChoices()) tails.push_back(t.name);
	s.push_back(paramChoice("Playback", "Tail", m, Spools::TAIL_PARAM, tails));
	std::vector<std::string> loops = {"∞"};
	for (int i = 1; i <= 16; i++) loops.push_back(std::to_string(i));
	s.push_back(paramChoice("Playback", "Loop count", m, Spools::LOOP_COUNT_PARAM, loops));
	s.push_back({"Playback", "Retrigger", {"Each slice", "Each note"},
		[=] { return size_t(m->retriggerPerNote.load()); }, [=](size_t i) { m->retriggerPerNote = i == 1; }});
	s.push_back(choice<float>("Playback", "Loop xfade", {"Off", "2 ms", "5 ms", "10 ms", "20 ms", "50 ms", "100 ms"},
		{0, 0.002f, 0.005f, 0.01f, 0.02f, 0.05f, 0.1f}, [=] { return m->loopXfade.load(); }, [=](float v) { m->loopXfade = v; }));

	auto reslice = [=] { m->reslice(); };
	s.push_back(choice<int>("Slicing", "Slice by", {"Onsets", "Grid", "Hybrid"}, {0, 1, 2},
		[=] { return int(m->slicing.mode); }, [=](int v) { m->slicing.mode = spools::SliceMode(v); reslice(); }));
	s.push_back(choice<double>("Slicing", "Grid", {"1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T"}, {1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6},
		[=] { return m->slicing.gridBeats; }, [=](double v) { m->slicing.gridBeats = v; reslice(); }));
	std::vector<std::string> pct = {"0%", "25%", "50%", "75%", "100%"};
	std::vector<float> frac = {0, 0.25f, 0.5f, 0.75f, 1};
	s.push_back(choice<float>("Slicing", "Sensitivity", pct, frac,
		[=] { return m->slicing.sensitivity; }, [=](float v) { m->slicing.sensitivity = v; reslice(); }));
	s.push_back(choice<float>("Slicing", "Grid snap", pct, frac,
		[=] { return m->slicing.snap; }, [=](float v) { m->slicing.snap = v; reslice(); }));

	s.push_back(choice<int>("Transport", "Clock", {"1 PPQN", "2 PPQN", "4 PPQN", "8 PPQN", "24 PPQN", "48 PPQN", "96 PPQN"},
		{1, 2, 4, 8, 24, 48, 96}, [=] { return m->ppqn.load(); }, [=](int v) { m->ppqn = v; }));
	std::vector<std::string> bars = {"From loop"};
	std::vector<float> beats = {0};
	for (int b : {2, 3, 4, 5, 6, 7, 8, 12, 16}) { bars.push_back(string::f("%d beats", b)); beats.push_back(float(b)); }
	s.push_back(choice<float>("Transport", "Bar length", bars, beats, [=] { return m->barBeats.load(); }, [=](float v) { m->barBeats = v; }));
	std::vector<std::string> cycles = {"∞"};
	std::vector<int> counts = {0};
	for (int n : {1, 2, 4, 8, 16, 32, 64}) { cycles.push_back(string::f("%d bar%s", n, n == 1 ? "" : "s")); counts.push_back(n); }
	s.push_back(choice<int>("Transport", "Bars per cycle", cycles, counts, [=] { return m->barsPerCycle.load(); }, [=](int v) { m->barsPerCycle = v; }));

	s.push_back({"Engine", "Quality", {"Best", "Eco"}, [=] { return size_t(m->eco.load()); }, [=](size_t i) { m->eco = i == 1; }});
	s.push_back({"Engine", "Embed audio", {"Off", "On"}, [=] { return size_t(m->embedAudio.load()); }, [=](size_t i) { m->embedAudio = i == 1; }});
	return s;
}

void appendSettingsMenus(ui::Menu* menu, Spools* m) {
	for (const char* group : {"Playback", "Slicing", "Transport", "Engine"}) {
		menu->addChild(createSubmenuItem(group, "", [=](ui::Menu* sub) {
			for (auto& s : moduleSettings(m))
				if (std::string(s.group) == group) sub->addChild(createIndexSubmenuItem(s.name, s.options, s.get, s.set));
			if (std::string(group) == "Slicing") sub->addChild(createMenuItem("Re-slice now", "", [=] { m->reslice(); }));
		}));
	}
}

void openSettingMenu(const Setting& s) {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(s.name));
	size_t cur = s.get();
	for (size_t i = 0; i < s.options.size(); i++) {
		auto set = s.set;
		menu->addChild(createCheckMenuItem(s.options[i], "", [=] { return i == cur; }, [=] { set(i); }));
	}
}
