#include "Spools.hpp"
#include <cmath>
#include "core/file.hpp"
#include "core/riff.hpp"
#include "core/units.hpp"

using namespace spools;

// Envelope times: knob 0..1 maps to 0..10 s on a 1 ms-based exponential curve.
static float envSeconds(float x) { return 0.001f * (std::pow(10001.f, x) - 1.f); }

struct EnvTimeQuantity : ParamQuantity {
	float getDisplayValue() override { return envSeconds(getValue()); }
	void setDisplayValue(float s) override { setValue(std::log(std::max(s, 0.f) * 1000.f + 1.f) / std::log(10001.f)); }
	std::string getDisplayValueString() override {
		float s = getDisplayValue();
		return s < 1.f ? string::f("%.1f", s * 1000.f) : string::f("%.2f", s);
	}
	std::string getUnit() override { return getDisplayValue() < 1.f ? " ms" : " s"; }
};

const std::vector<TailChoice>& tailChoices() {
	static const std::vector<TailChoice> choices = {{TailMode::Ring, "Ring"}, {TailMode::Cut, "Cut"}, {TailMode::Loop, "Loop"}, {TailMode::AutoLoop, "Auto-loop"}, {TailMode::Stretch, "Stretch"}};
	return choices;
}

static std::vector<std::string> tailNames() {
	std::vector<std::string> names;
	for (auto& t : tailChoices()) names.push_back(t.name);
	return names;
}

static const double kQuantizeBeats[] = {0, 0.125, 0.25, 0.5, 1, 2, 4};
static const std::vector<std::string> kQuantizeLabels = {"Off", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar"};

Spools::Spools() {
	config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
	configParam(START_PARAM, 0.f, 1.f, 0.f, "Start slice", "%", 0.f, 100.f);
	configParam(LENGTH_PARAM, 1.f, 64.f, 1.f, "Length", " slices")->snapEnabled = true;
	configParam(START_TRIM_PARAM, -0.25f, 0.25f, 0.f, "Start trim", " ms", 0.f, 1000.f);
	configParam(END_TRIM_PARAM, -0.25f, 0.25f, 0.f, "End trim", " ms", 0.f, 1000.f);
	configParam(PITCH_PARAM, -24.f, 24.f, 0.f, "Pitch", " st");
	configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume", " dB", -10.f, 20.f);
	configParam(OVERLAP_PARAM, 0.f, 0.05f, 0.005f, "Overlap", " ms", 0.f, 1000.f);
	configParam(DURATION_PARAM, 0.05f, 1.f, 1.f, "Duration", "%", 0.f, 100.f);
	configSwitch(QUANTIZE_PARAM, 0.f, 6.f, 0.f, "Launch quantize", kQuantizeLabels);
	configParam(BPM_PARAM, 0.f, 300.f, 0.f, "Manual BPM (0 = from loop)", " BPM");
	configParam(REGION_START_PARAM, 0.f, 1.f, 0.f, "Region start", "%", 0.f, 100.f);
	configParam(REGION_END_PARAM, 0.f, 1.f, 1.f, "Region end", "%", 0.f, 100.f);
	configParam(LOOP_COUNT_PARAM, 0.f, 16.f, 0.f, "Loop count (0 = forever)")->snapEnabled = true;
	configSwitch(DIRECTION_PARAM, 0.f, 3.f, 0.f, "Direction", {"Forward", "Reverse", "Ping-pong", "Random"});
	configSwitch(TAIL_PARAM, 0.f, float(tailChoices().size() - 1), 0.f, "Tail", tailNames());
	const int attended[] = {START_PARAM, LENGTH_PARAM, START_TRIM_PARAM, END_TRIM_PARAM, PITCH_PARAM, VOLUME_PARAM, OVERLAP_PARAM, QUANTIZE_PARAM};
	for (int i = 0; i <= QUANTIZE_ATT_PARAM - START_ATT_PARAM; i++)
		configParam(START_ATT_PARAM + i, -1.f, 1.f, 0.f, paramQuantities[attended[i]]->name + " CV amount", "%", 0.f, 100.f);

	configInput(VOCT_INPUT, "Slice offset (1 V/oct, C4 = start slice)");
	configInput(GATE_INPUT, "Gate / trigger");
	configInput(CLOCK_INPUT, "Clock");
	configInput(START_INPUT, "Start slice CV");
	configInput(LENGTH_INPUT, "Length CV");
	configInput(START_TRIM_INPUT, "Start trim CV");
	configInput(END_TRIM_INPUT, "End trim CV");
	configSwitch(FILTER_TYPE_PARAM, 0.f, 5.f, 0.f, "Filter", {"Off", "Low-pass 12 dB", "Band-pass", "High-pass 12 dB", "Notch", "Low-pass 24 dB"});
	configParam(CUTOFF_PARAM, -4.f, 6.f, 6.f, "Cutoff", " Hz", 2.f, 261.6256f);
	configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
	configParam(CUTOFF_ATT_PARAM, -1.f, 1.f, 0.f, "Cutoff CV amount", "%", 0.f, 100.f);
	configParam(RES_ATT_PARAM, -1.f, 1.f, 0.f, "Resonance CV amount", "%", 0.f, 100.f);
	auto envTime = [&](int id, float seconds, const char* name) {
		configParam<EnvTimeQuantity>(id, 0.f, 1.f, std::log(seconds * 1000.f + 1.f) / std::log(10001.f), name);
	};
	configParam(FILTER_ENV_AMOUNT_PARAM, -5.f, 5.f, 0.f, "Filter envelope amount", " oct");
	envTime(FILTER_A_PARAM, 0.f, "Filter attack");
	envTime(FILTER_D_PARAM, 0.3f, "Filter decay");
	configParam(FILTER_S_PARAM, 0.f, 1.f, 0.f, "Filter sustain", "%", 0.f, 100.f);
	envTime(FILTER_R_PARAM, 0.1f, "Filter release");
	envTime(AMP_A_PARAM, 0.f, "Amp attack");
	envTime(AMP_D_PARAM, 0.5f, "Amp decay");
	configParam(AMP_S_PARAM, 0.f, 1.f, 1.f, "Amp sustain", "%", 0.f, 100.f);
	envTime(AMP_R_PARAM, 0.f, "Amp release");
	configOutput(AMP_ENV_OUTPUT, "Amp envelope (0–10 V)");
	configOutput(FILTER_ENV_OUTPUT, "Filter envelope (0–10 V)");
	configInput(RUN_INPUT, "Run (gate; unpatched = always running)");
	configInput(CONT_INPUT, "Continue (trigger: restart at the start slice on the next clock)");
	configInput(BAR_INPUT, "Bar (trigger: realign the bar phase)");
	configInput(CUTOFF_INPUT, "Cutoff CV (1 V/oct at 100%)");
	configInput(RES_INPUT, "Resonance CV (10 V = 100%)");
	configInput(PITCH_INPUT, "Pitch CV (1 V/oct)");
	configInput(VOLUME_INPUT, "Volume CV");
	configInput(OVERLAP_INPUT, "Overlap CV");
	configInput(QUANTIZE_INPUT, "Launch quantize CV");
	configOutput(LEFT_OUTPUT, "Left");
	configOutput(RIGHT_OUTPUT, "Right");
	configOutput(SLICE_OUTPUT, "Slice index (1/12 V per slice)");
	configOutput(TRIG_OUTPUT, "Slice start trigger");
	configOutput(EOC_OUTPUT, "End of phrase");
	for (int c = 0; c < kMaxVoices; c++) { playingSlice[c] = -1; playheads[c] = -1.f; playingStep[c] = -1; }
	paramDivider.setDivision(32);
	paramDivider.clock = 31;  // read the knobs on the first sample
	worker.reset(new Worker);
}

Spools::~Spools() { worker.reset(); }

int Spools::regionIndex(float frac, size_t n) const {
	return n ? int(std::lround(clamp(frac, 0.f, 1.f) * float(n - 1))) : 0;
}

void Spools::publish(std::shared_ptr<const Loop> l) {
	exchange.publish(std::move(l));
	generation++;
}

void Spools::load(const std::string& path, std::shared_ptr<SavedLoop> saved) {
	loading = true;
	std::string embedded = saved ? system::join(getPatchStorageDirectory(), "loop.wav") : "";
	worker->post([this, path, saved, embedded, slicing = slicing] {
		std::string err;
		std::shared_ptr<Loop> l = path.empty() ? nullptr : loadLoop(path, &err);
		bool fromEmbed = false;
		if ((!l || (saved && saved->crop.end > l->source->frames)) && !embedded.empty() && system::isFile(embedded)) {
			l = loadLoop(embedded, &err);
			fromEmbed = bool(l);
		}
		if (l && saved && !saved->slices.empty()) {
			Range crop = fromEmbed ? Range{0, l->source->frames} : saved->crop;
			std::vector<size_t> starts;
			for (auto& s : saved->slices) starts.push_back(s.start + crop.start);
			auto r = makeLoop(l->source, crop, saved->bpm, saved->bpmKnown, starts, l->origin, path);
			r->meterNum = saved->meterNum;
			r->meterDen = saved->meterDen;
			for (size_t i = 0; i < r->slices.size() && i < saved->slices.size(); i++) r->slices[i].p = saved->slices[i].p;
			r->regionStart = std::min(saved->regionStart, r->slices.size() - 1);
			r->regionEnd = std::min(std::max(saved->regionEnd, r->regionStart), r->slices.size() - 1);
			r->pattern = saved->pattern;
			l = r;
		}
		else if (l && !l->spls.empty()) applySpls(*l);  // a file Spools saved: its slice settings and pattern
		else if (l && l->origin == Origin::Plain) l = autoSlice(*l, slicing);
		if (l) {
			if (fromEmbed) l->sourcePath = path;
			publish(l);
			setStatus("");
		}
		else setStatus(err);
		loading = false;
	});
}

void Spools::postEdit(const char* name, std::function<std::shared_ptr<Loop>(const Loop&)> edit) {
	auto cur = loop();
	if (!cur) return;
	worker->post([this, cur, name, edit] {
		std::shared_ptr<const Loop> next = edit(*cur);
		if (!next) return;
		publish(next);
		std::lock_guard<std::mutex> lock(editsMutex);
		finishedEdits.push_back({name, cur, next});
	});
}

void Spools::flushHistory() {
	std::vector<FinishedEdit> done;
	{
		std::lock_guard<std::mutex> lock(editsMutex);
		done.swap(finishedEdits);
	}
	for (auto& e : done) commitEdit(e.before, e.after, e.name, false);  // already published; this only records it
}

void Spools::ensureFeatures() {
	auto cur = loop();
	if (!cur || cur->features || featuresFor == cur.get()) return;
	featuresFor = cur.get();
	worker->post([this, cur] {
		auto l = std::make_shared<Loop>(*cur);
		l->features = std::make_shared<const std::vector<SliceFeatures>>(analyzeSlices(*cur));
		exchange.replace(cur, l);  // derived data only: no undo step, and dropped if an edit got there first
	});
}

void Spools::sliceEvenly(size_t count) {
	postEdit("slice evenly", [count](const Loop& l) { return spools::sliceEvenly(l, count); });
}

void Spools::reslice() {
	postEdit("re-slice", [s = slicing](const Loop& cur) {
		auto l = autoSlice(cur, s);
		l->regionStart = std::min(cur.regionStart, l->slices.size() - 1);
		l->regionEnd = std::min(std::max(cur.regionEnd, l->regionStart), l->slices.size() - 1);
		return l;
	});
}

void Spools::setCrop(Range crop) {
	postEdit("crop", [crop, s = slicing](const Loop& cur) {
		auto l = recrop(cur, crop);
		return l->origin == Origin::Plain ? autoSlice(*l, s) : l;
	});
}

void Spools::setLoopBpm(double bpm) {
	if (!(bpm > 0)) return;
	postEdit("loop tempo", [bpm](const Loop& l) { return retempo(l, bpm); });
}

std::vector<size_t>& Spools::selected() {
	auto l = loop();
	if (!l || (selectionOf && selectionOf->slices.size() != l->slices.size())) selection.clear();
	selectionOf = l.get();
	return selection;
}

namespace {
// Undo/redo of a slice-map edit: both versions share their audio, so this costs only the slice lists.
struct LoopEditAction : history::ModuleAction {
	std::shared_ptr<const Loop> before, after;
	void apply(std::shared_ptr<const Loop> l) {
		if (auto* m = dynamic_cast<Spools*>(APP->engine->getModule(moduleId))) m->publish(l);
	}
	void undo() override { apply(before); }
	void redo() override { apply(after); }
};
}

void Spools::commitEdit(std::shared_ptr<const Loop> before, std::shared_ptr<const Loop> after, const std::string& name, bool publishIt) {
	if (!after) return;
	if (publishIt) publish(after);
	if (!before || before == after) return;
	if (!APP->history) return;
	auto* h = new LoopEditAction;
	h->moduleId = id;
	h->name = name;
	h->before = before;
	h->after = after;
	APP->history->push(h);
}

void Spools::onReset(const ResetEvent& e) {
	Module::onReset(e);
	ppqn = 1;
	eco = false;
	embedAudio = true;
	retriggerPerNote = false;
	loopXfade = 0.01f;
	transientPreserve = 1;
	stretchTail = 1;
	barBeats = 0;
	barsPerCycle = 0;
	playMode = kNoteMode;
	slicing = {};
	cropUnit = CropSpec::Seconds;
	selection.clear();
	knobsOnSelection = false;
}

void Spools::process(const ProcessArgs& args) {
	const Loop* l = exchange.acquire();
	const bool clockConnected = inputs[CLOCK_INPUT].isConnected();
	const bool edge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
	float manualBpm = params[BPM_PARAM].getValue();
	timebase.setPpqn(ppqn);
	timebase.setFallbackBpm(manualBpm >= 1.f ? manualBpm : l ? l->bpm : 120.0);
	timebase.process(clockConnected, edge, args.sampleTime);
	currentBpm = float(timebase.bpm());
	{
		Transport::In in;
		in.runConnected = inputs[RUN_INPUT].isConnected();
		runTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);
		in.run = runTrigger.isHigh();
		in.cont = contTrigger.process(inputs[CONT_INPUT].getVoltage(), 0.1f, 1.f);
		in.bar = barTrigger.process(inputs[BAR_INPUT].getVoltage(), 0.1f, 1.f);
		in.clocked = clockConnected;
		in.clockEdge = edge;
		float bb = barBeats;
		double bar = bb > 0 ? bb : l ? 4.0 * l->meterNum / std::max(1, l->meterDen) : 4.0;
		transportState = transport.process(timebase.phase(), in, bar, barsPerCycle);
	}

	const int mode = playMode;
	const bool transported = mode != kNoteMode;
	const Pattern* pat = l ? l->pattern.get() : nullptr;
	if (mode != wasMode) {  // switching modes: silence everything and start clean
		for (int c = 0; c < kMaxVoices; c++) { voices[c].reset(); loopSeqs[c].reset(); patternSeqs[c].reset(); }
		wasMode = mode;
	}
	// Loop and pattern modes: one transport voice per V/oct channel (each with its own offset); GATE is ignored.
	const int channels = transported ? std::max(1, inputs[VOCT_INPUT].getChannels())
		: std::max({1, inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()});
	for (int id = 0; id < OUTPUTS_LEN; id++) outputs[id].setChannels(channels);
	activeChannels = channels;
	if (!l) {
		for (int id = 0; id < OUTPUTS_LEN; id++)
			for (int c = 0; c < channels; c++) outputs[id].setVoltage(0.f, c);
		return;
	}

	const size_t n = l->slices.size();
	auto cv = [&](int input, int att, int c) { return inputs[input].getPolyVoltage(c) * params[att].getValue(); };
	PhraseParams phrase;
	phrase.regionStart = regionIndex(params[REGION_START_PARAM].getValue(), n);
	phrase.regionEnd = regionIndex(params[REGION_END_PARAM].getValue(), n);
	if (params[REGION_START_PARAM].getValue() == 0.f && params[REGION_END_PARAM].getValue() == 1.f)
		phrase.regionStart = phrase.regionEnd = -1;  // knobs untouched: use the file's own region
	phrase.direction = Direction(int(params[DIRECTION_PARAM].getValue()));
	phrase.loopCount = int(params[LOOP_COUNT_PARAM].getValue());

	if (paramDivider.process()) {  // knob-only values change slowly; per-voice CV is read every sample below
		VoiceParams& g = globalParams;
		g.tail = tailChoices()[std::min(size_t(params[TAIL_PARAM].getValue()), tailChoices().size() - 1)].mode;
		g.duration = params[DURATION_PARAM].getValue();
		g.quality = eco ? &SincTable::eco() : &SincTable::hq();
		g.filter = FilterType(int(params[FILTER_TYPE_PARAM].getValue()));
		g.filterEnvAmount = params[FILTER_ENV_AMOUNT_PARAM].getValue();
		g.filterEnv = {envSeconds(params[FILTER_A_PARAM].getValue()), envSeconds(params[FILTER_D_PARAM].getValue()),
			params[FILTER_S_PARAM].getValue(), envSeconds(params[FILTER_R_PARAM].getValue())};
		g.ampEnv = {envSeconds(params[AMP_A_PARAM].getValue()), envSeconds(params[AMP_D_PARAM].getValue()),
			params[AMP_S_PARAM].getValue(), envSeconds(params[AMP_R_PARAM].getValue())};
		g.retriggerPerNote = retriggerPerNote;
		g.loopXfade = loopXfade;
		g.transientPreserve = transientPreserve;
		g.stretchTail = stretchTail;
	}
	VoiceParams vp = globalParams;

	const double beat = timebase.phase(), period = timebase.beatPeriod();
	for (int c = 0; c < channels; c++) {
		float startFrac = params[START_PARAM].getValue() + cv(START_INPUT, START_ATT_PARAM, c) * 0.1f;
		int r0, rn;
		phrase.region(*l, r0, rn);
		phrase.startSlice = r0 + int(std::lround(clamp(startFrac, 0.f, 1.f) * float(rn - 1)));
		phrase.length = int(clamp(std::round(params[LENGTH_PARAM].getValue() + cv(LENGTH_INPUT, LENGTH_ATT_PARAM, c) * 6.4f), 1.f, 64.f));
		int q = int(clamp(std::round(params[QUANTIZE_PARAM].getValue() + cv(QUANTIZE_INPUT, QUANTIZE_ATT_PARAM, c) * 0.6f), 0.f, 6.f));
		phrase.launchQuantize = kQuantizeBeats[q];

		vp.startTrim = knob(START_TRIM_PARAM) + cv(START_TRIM_INPUT, START_TRIM_ATT_PARAM, c) * 0.05f;
		vp.endTrim = knob(END_TRIM_PARAM) + cv(END_TRIM_INPUT, END_TRIM_ATT_PARAM, c) * 0.05f;
		vp.pitch = knob(PITCH_PARAM) + cv(PITCH_INPUT, PITCH_ATT_PARAM, c) * 12.f;
		float vol = std::max(0.f, knob(VOLUME_PARAM) + cv(VOLUME_INPUT, VOLUME_ATT_PARAM, c) * 0.2f);
		vp.gain = 20.f * std::log10(std::max(vol, 1e-5f));
		vp.cutoff = knob(CUTOFF_PARAM) + cv(CUTOFF_INPUT, CUTOFF_ATT_PARAM, c);
		vp.res = clamp(knob(RES_PARAM) + cv(RES_INPUT, RES_ATT_PARAM, c) * 0.1f, 0.f, 1.f);
		vp.overlap = clamp(knob(OVERLAP_PARAM) + cv(OVERLAP_INPUT, OVERLAP_ATT_PARAM, c) * 0.005f, 0.f, 0.1f);

		float gateV = inputs[GATE_INPUT].getVoltage(c);
		bool rising = gateTriggers[c].process(gateV, 0.1f, 1.f);
		bool gate = gateTriggers[c].isHigh();
		int semis = voltsToSemitones(inputs[VOCT_INPUT].getVoltage(c));

		const auto& ts = transportState;
		VoiceOut o = mode == kLoopMode ? voices[c].play(loopSeqs[c].process(ts.loopBeat, ts.running, ts.reset, semis, *l, phrase), period, *l, vp, args.sampleRate)
			: mode == kPatternMode ? voices[c].play(pat ? patternSeqs[c].process(ts.loopBeat, ts.running, ts.reset, semis, *l, *pat) : SeqEvent(),
				period, *l, vp, args.sampleRate)
			: voices[c].process(beat, period, gate, rising, semis, *l, phrase, vp, args.sampleRate);
		if (o.trig) trigPulses[c].trigger(1e-3f);
		if (o.eoc) eocPulses[c].trigger(1e-3f);
		outputs[LEFT_OUTPUT].setVoltage(5.f * o.left, c);
		outputs[RIGHT_OUTPUT].setVoltage(5.f * o.right, c);
		outputs[SLICE_OUTPUT].setVoltage(o.slice >= 0 ? semitonesToVolts(o.slice) : 0.f, c);
		outputs[TRIG_OUTPUT].setVoltage(trigPulses[c].process(args.sampleTime) ? 10.f : 0.f, c);
		outputs[EOC_OUTPUT].setVoltage(eocPulses[c].process(args.sampleTime) ? 10.f : 0.f, c);
		outputs[AMP_ENV_OUTPUT].setVoltage(10.f * o.ampEnv, c);
		outputs[FILTER_ENV_OUTPUT].setVoltage(10.f * o.filterEnv, c);
		playingSlice[c] = o.playing ? o.slice : -1;
		playingStep[c] = mode == kPatternMode ? patternSeqs[c].currentStep() : -1;
		playheads[c] = o.playing ? float(voices[c].player().position()) : -1.f;
	}
	for (int c = channels; c < kMaxVoices; c++) {
		if (playheads[c] >= 0.f) { voices[c].reset(); playheads[c] = -1.f; playingSlice[c] = -1; }
		loopSeqs[c].reset();
		patternSeqs[c].reset();
		playingStep[c] = -1;
	}
}

std::shared_ptr<const Loop> Spools::setPattern(std::shared_ptr<const Loop> base, Pattern p, const std::string& name, bool publishOnly) {
	if (!base) return nullptr;
	auto next = std::make_shared<Loop>(*base);
	next->pattern = std::make_shared<const Pattern>(std::move(p));
	if (publishOnly) publish(next);
	else commitEdit(base, next, name);
	return next;
}

void Spools::ensurePattern() {
	auto l = loop();
	if (!l || l->slices.empty() || l->pattern) return;
	auto next = std::make_shared<Loop>(*l);
	next->pattern = std::make_shared<const Pattern>(initFromLoop(*l));
	exchange.replace(l, next);  // derived, like the features: no undo step
}

void Spools::onSave(const SaveEvent& e) {
	auto l = loop();
	if (!embedAudio || !l || generation == savedGeneration) return;
	std::string dir = createPatchStorageDirectory(), path = system::join(dir, "loop.wav");
	if (l->audio->seconds() > kMaxEmbedSeconds) {
		system::remove(path);
		return;
	}
	LoopMeta meta;
	meta.hasAcid = true;
	meta.bpm = l->bpm;
	meta.beats = uint32_t(std::lround(l->beats()));
	meta.meterNum = uint16_t(l->meterNum);
	meta.meterDen = uint16_t(l->meterDen);
	for (auto& s : l->slices) meta.cues.push_back({uint32_t(s.start), ""});
	meta.spls = splsChunk(*l);
	auto bytes = writeWav(*l->audio, meta, SampleFormat::Float32);
	if (spools::writeFile(path, bytes.data(), bytes.size())) savedGeneration = generation;
}

json_t* sliceParamsToJson(const SliceParams& p) {
	json_t* j = json_object();
	auto num = [&](const char* k, float v) { if (v != 0.f) json_object_set_new(j, k, json_real(v)); };
	num("startTrim", p.startTrim);
	num("endTrim", p.endTrim);
	num("gain", p.gain);
	num("pan", p.pan);
	num("pitch", p.pitch);
	num("cutoff", p.cutoff);
	num("res", p.res);
	num("xfade", p.xfade);
	num("loopStart", p.loopStart);
	num("loopEnd", p.loopEnd);
	if (p.pingPong) json_object_set_new(j, "pingPong", json_true());
	if (p.reverse) json_object_set_new(j, "reverse", json_true());
	if (p.tail != TailMode::Inherit) json_object_set_new(j, "tail", json_integer(int(p.tail)));
	return j;
}

void sliceParamsFromJson(json_t* j, SliceParams& p) {
	auto num = [&](const char* k, float& v) { if (json_t* x = json_object_get(j, k)) v = float(json_number_value(x)); };
	num("startTrim", p.startTrim);
	num("endTrim", p.endTrim);
	num("gain", p.gain);
	num("pan", p.pan);
	num("pitch", p.pitch);
	num("cutoff", p.cutoff);
	num("res", p.res);
	num("xfade", p.xfade);
	num("loopStart", p.loopStart);
	num("loopEnd", p.loopEnd);
	p.pingPong = json_is_true(json_object_get(j, "pingPong"));
	p.reverse = json_is_true(json_object_get(j, "reverse"));
	if (json_t* t = json_object_get(j, "tail")) p.tail = TailMode(clamp(int(json_integer_value(t)), 0, int(TailMode::Stretch)));
}

json_t* patternToJson(const Pattern& p) {
	json_t* j = json_object();
	json_object_set_new(j, "loopStart", json_integer(p.loopStart));
	json_object_set_new(j, "loopEnd", json_integer(p.loopEnd));
	json_object_set_new(j, "loopCount", json_integer(p.loopCount));
	json_object_set_new(j, "direction", json_integer(int(p.direction)));
	json_t* steps = json_array();
	const PatternStep def;
	for (const PatternStep& st : p.steps) {
		json_t* js = json_object();
		json_object_set_new(js, "slice", json_integer(st.slice));
		json_object_set_new(js, "ticks", json_integer(st.ticks));
		auto num = [&](const char* k, float v, float d) { if (v != d) json_object_set_new(js, k, json_real(v)); };
		num("nudge", st.nudge, def.nudge);
		num("duration", st.duration, def.duration);
		num("pitch", st.pitch, def.pitch);
		num("ratchetCurve", st.ratchetCurve, def.ratchetCurve);
		num("velocity", st.velocity, def.velocity);
		num("velocityRamp", st.velocityRamp, def.velocityRamp);
		if (st.ratchets != 1) json_object_set_new(js, "ratchets", json_integer(st.ratchets));
		if (st.reverse) json_object_set_new(js, "reverse", json_true());
		json_array_append_new(steps, js);
	}
	json_object_set_new(j, "steps", steps);
	return j;
}

Pattern patternFromJson(json_t* j) {
	Pattern p;
	auto integer = [](json_t* o, const char* k, int d) { json_t* x = json_object_get(o, k); return x ? int(json_integer_value(x)) : d; };
	p.loopStart = std::max(0, integer(j, "loopStart", 0));
	p.loopEnd = std::max(-1, integer(j, "loopEnd", -1));
	p.loopCount = clamp(integer(j, "loopCount", 0), 0, 64);
	p.direction = Direction(clamp(integer(j, "direction", 0), 0, 3));
	size_t i;
	json_t* js;
	json_array_foreach(json_object_get(j, "steps"), i, js) {
		if (i >= 64) break;
		PatternStep st;
		auto num = [&](const char* k, float& v, float lo, float hi) { if (json_t* x = json_object_get(js, k)) v = clamp(float(json_number_value(x)), lo, hi); };
		st.slice = std::max(-1, integer(js, "slice", 0));
		st.ticks = clamp(integer(js, "ticks", 4), 1, 32);
		num("nudge", st.nudge, -1, 1);
		num("duration", st.duration, 0.05f, 1);
		num("pitch", st.pitch, -48, 48);
		num("ratchetCurve", st.ratchetCurve, -1, 1);
		num("velocity", st.velocity, 0, 1);
		num("velocityRamp", st.velocityRamp, -1, 1);
		st.ratchets = clamp(integer(js, "ratchets", 1), 1, 8);
		st.reverse = json_is_true(json_object_get(js, "reverse"));
		p.steps.push_back(st);
	}
	return p;
}

std::string splsChunk(const Loop& l) {
	json_t* root = json_object();
	json_object_set_new(root, "version", json_integer(1));
	json_t* slices = json_array();
	for (auto& s : l.slices) json_array_append_new(slices, sliceParamsToJson(s.p));
	json_object_set_new(root, "slices", slices);
	if (l.pattern) json_object_set_new(root, "pattern", patternToJson(*l.pattern));
	char* text = json_dumps(root, JSON_COMPACT);
	std::string out = text ? text : "";
	free(text);
	json_decref(root);
	return out;
}

void applySpls(Loop& l) {
	if (l.spls.empty()) return;
	json_error_t err;
	json_t* root = json_loads(l.spls.c_str(), 0, &err);
	if (!root) return;
	size_t i;
	json_t* js;
	json_array_foreach(json_object_get(root, "slices"), i, js)
		if (i < l.slices.size()) sliceParamsFromJson(js, l.slices[i].p);
	if (json_t* p = json_object_get(root, "pattern")) l.pattern = std::make_shared<const Pattern>(patternFromJson(p));
	json_decref(root);
}

json_t* Spools::dataToJson() {
	json_t* root = json_object();
	json_object_set_new(root, "ppqn", json_integer(ppqn));
	json_object_set_new(root, "eco", json_boolean(eco));
	json_object_set_new(root, "embedAudio", json_boolean(embedAudio));
	json_object_set_new(root, "retriggerPerNote", json_boolean(retriggerPerNote));
	json_object_set_new(root, "loopXfade", json_real(loopXfade));
	json_object_set_new(root, "transientPreserve", json_real(transientPreserve));
	json_object_set_new(root, "stretchTail", json_real(stretchTail));
	json_object_set_new(root, "barBeats", json_real(barBeats));
	json_object_set_new(root, "barsPerCycle", json_integer(barsPerCycle));
	json_object_set_new(root, "mode", json_integer(playMode));
	json_object_set_new(root, "displayPage", json_integer(displayPage));
	json_object_set_new(root, "viewStart", json_real(viewStart));
	json_object_set_new(root, "viewSpan", json_real(viewSpan));
	json_t* js = json_object();
	json_object_set_new(js, "mode", json_integer(int(slicing.mode)));
	json_object_set_new(js, "gridBeats", json_real(slicing.gridBeats));
	json_object_set_new(js, "sensitivity", json_real(slicing.sensitivity));
	json_object_set_new(js, "snap", json_real(slicing.snap));
	json_object_set_new(root, "slicing", js);
	json_object_set_new(root, "cropUnit", json_integer(int(cropUnit)));
	auto l = loop();
	if (!l) return root;
	json_t* j = json_object();
	json_object_set_new(j, "path", json_string(l->sourcePath.c_str()));
	json_object_set_new(j, "cropStart", json_integer(json_int_t(l->crop.start)));
	json_object_set_new(j, "cropEnd", json_integer(json_int_t(l->crop.end)));
	json_object_set_new(j, "bpm", json_real(l->bpm));
	json_object_set_new(j, "bpmKnown", json_boolean(l->bpmKnown));
	json_object_set_new(j, "meterNum", json_integer(l->meterNum));
	json_object_set_new(j, "meterDen", json_integer(l->meterDen));
	json_object_set_new(j, "regionStart", json_integer(json_int_t(l->regionStart)));
	json_object_set_new(j, "regionEnd", json_integer(json_int_t(l->regionEnd)));
	json_t* slices = json_array();
	for (auto& s : l->slices) {
		json_t* js = sliceParamsToJson(s.p);
		json_object_set_new(js, "start", json_integer(json_int_t(s.start)));
		json_array_append_new(slices, js);
	}
	json_object_set_new(j, "slices", slices);
	if (l->pattern) json_object_set_new(j, "pattern", patternToJson(*l->pattern));
	json_object_set_new(root, "loop", j);
	return root;
}

void Spools::dataFromJson(json_t* root) {
	if (json_t* x = json_object_get(root, "ppqn")) ppqn = clamp(int(json_integer_value(x)), 1, 96);
	if (json_t* x = json_object_get(root, "eco")) eco = json_is_true(x);
	if (json_t* x = json_object_get(root, "embedAudio")) embedAudio = json_is_true(x);
	if (json_t* x = json_object_get(root, "retriggerPerNote")) retriggerPerNote = json_is_true(x);
	if (json_t* x = json_object_get(root, "loopXfade")) loopXfade = float(json_number_value(x));
	if (json_t* x = json_object_get(root, "transientPreserve")) transientPreserve = float(json_number_value(x));
	if (json_t* x = json_object_get(root, "stretchTail")) stretchTail = float(json_number_value(x));
	if (json_t* x = json_object_get(root, "barBeats")) barBeats = clamp(float(json_number_value(x)), 0.f, 64.f);
	if (json_t* x = json_object_get(root, "displayPage")) displayPage = int(json_integer_value(x));
	if (json_t* x = json_object_get(root, "viewSpan")) viewSpan = clamp(float(json_number_value(x)), 0.001f, 1.f);
	if (json_t* x = json_object_get(root, "viewStart")) viewStart = clamp(float(json_number_value(x)), 0.f, 1.f - viewSpan);
	if (json_t* x = json_object_get(root, "mode")) playMode = clamp(int(json_integer_value(x)), 0, int(kPatternMode));
	if (json_t* x = json_object_get(root, "barsPerCycle")) barsPerCycle = clamp(int(json_integer_value(x)), 0, 64);
	if (json_t* js = json_object_get(root, "slicing")) {
		slicing.mode = SliceMode(clamp(int(json_integer_value(json_object_get(js, "mode"))), 0, 2));
		if (json_t* x = json_object_get(js, "gridBeats")) slicing.gridBeats = std::min(std::max(json_number_value(x), 1.0 / 64), 4.0);
		if (json_t* x = json_object_get(js, "sensitivity")) slicing.sensitivity = clamp(float(json_number_value(x)), 0.f, 1.f);
		if (json_t* x = json_object_get(js, "snap")) slicing.snap = clamp(float(json_number_value(x)), 0.f, 1.f);
	}
	if (json_t* x = json_object_get(root, "cropUnit")) cropUnit = CropSpec::Unit(clamp(int(json_integer_value(x)), 0, 2));
	json_t* j = json_object_get(root, "loop");
	if (!j) return;
	auto saved = std::make_shared<SavedLoop>();
	auto integer = [&](const char* k, size_t def) { json_t* x = json_object_get(j, k); return x ? size_t(std::max<json_int_t>(0, json_integer_value(x))) : def; };
	saved->crop = {integer("cropStart", 0), integer("cropEnd", 0)};
	saved->bpm = json_number_value(json_object_get(j, "bpm"));
	saved->bpmKnown = json_is_true(json_object_get(j, "bpmKnown"));
	saved->meterNum = int(integer("meterNum", 4));
	saved->meterDen = int(integer("meterDen", 4));
	saved->regionStart = integer("regionStart", 0);
	saved->regionEnd = integer("regionEnd", 0);
	json_t* slices = json_object_get(j, "slices");
	size_t i;
	json_t* js;
	json_array_foreach(slices, i, js) {
		Slice s;
		s.start = size_t(std::max<json_int_t>(0, json_integer_value(json_object_get(js, "start"))));
		sliceParamsFromJson(js, s.p);
		saved->slices.push_back(s);
	}
	if (json_t* p = json_object_get(j, "pattern")) saved->pattern = std::make_shared<const Pattern>(patternFromJson(p));
	json_t* path = json_object_get(j, "path");
	load(path ? json_string_value(path) : "", saved);
	savedGeneration = generation + 1;  // the embedded file on disk already matches what we are loading
}
