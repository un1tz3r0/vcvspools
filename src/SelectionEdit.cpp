#include "Spools.hpp"
#include "core/edit.hpp"

using namespace spools;

namespace {

// A knob that edits a per-slice value while slices are selected. Values are converted to the units the
// slice stores (seconds, semitones, dB, octaves), where the global knob and the slice offset add up.
struct Mapping {
	int param;
	float SliceParams::*field;
	float (*units)(float knob);
	float (*knobOf)(float units);
};

float same(float v) { return v; }
float volumeDb(float v) { return 20.f * std::log10(std::max(v, 1e-5f)); }
float dbVolume(float db) { return std::pow(10.f, db / 20.f); }

const Mapping kMappings[] = {
	{Spools::START_TRIM_PARAM, &SliceParams::startTrim, same, same},
	{Spools::END_TRIM_PARAM, &SliceParams::endTrim, same, same},
	{Spools::PITCH_PARAM, &SliceParams::pitch, same, same},
	{Spools::VOLUME_PARAM, &SliceParams::gain, volumeDb, dbVolume},
	{Spools::OVERLAP_PARAM, &SliceParams::xfade, same, same},
	{Spools::CUTOFF_PARAM, &SliceParams::cutoff, same, same},
	{Spools::RES_PARAM, &SliceParams::res, same, same},
};

const Mapping* mappingFor(int id) {
	for (auto& m : kMappings) if (m.param == id) return &m;
	return nullptr;
}

}

json_t* Spools::paramsToJson() {
	json_t* j = Module::paramsToJson();
	if (!knobsOnSelection) return j;
	size_t i;
	json_t* p;
	json_array_foreach(j, i, p) {  // save the global values, not the selection's
		int id = int(json_integer_value(json_object_get(p, "id")));
		if (mappingFor(id)) json_object_set_new(p, "value", json_real(globalStash[id].load()));
	}
	return j;
}

void Spools::syncSelectionKnobs() {
	auto l = loop();
	const std::vector<size_t> sel = l ? selected() : std::vector<size_t>{};
	auto effective = [&](const Mapping& mp, size_t s) { return mp.units(globalStash[mp.param]) + l->slices[s].p.*mp.field; };
	auto showMean = [&] {
		for (auto& mp : kMappings) {
			float sum = 0;
			for (size_t s : sel) sum += effective(mp, s);
			ParamQuantity* q = paramQuantities[mp.param];
			params[mp.param].setValue(clamp(mp.knobOf(sum / float(sel.size())), q->getMinValue(), q->getMaxValue()));
			shown[mp.param] = params[mp.param].getValue();
		}
		shownFor = sel;
		shownLoop = l.get();
	};

	if (sel.empty()) {
		if (knobsOnSelection) {
			for (auto& mp : kMappings) params[mp.param].setValue(globalStash[mp.param]);
			knobsOnSelection = false;
		}
		shownFor.clear();
		return;
	}
	if (!knobsOnSelection) {
		for (auto& mp : kMappings) globalStash[mp.param] = params[mp.param].getValue();
		knobsOnSelection = true;
		return showMean();
	}
	if (sel != shownFor || l.get() != shownLoop) return showMean();  // new selection, or the loop changed elsewhere (undo)

	// A knob moved: move every selected slice by the same amount, keeping their differences.
	std::vector<SliceParams> next;
	for (size_t s : sel) next.push_back(l->slices[s].p);
	bool changed = false;
	for (auto& mp : kMappings) {
		float v = params[mp.param].getValue();
		if (v == shown[mp.param]) continue;
		float delta = mp.units(v) - mp.units(shown[mp.param]);
		for (auto& p : next) p.*mp.field += delta;
		shown[mp.param] = v;
		changed = true;
	}
	if (!changed) return;
	auto edited = setSliceParams(*l, sel, next);
	shownLoop = edited.get();
	publish(edited);
}

void Spools::flattenSelection(int id) {
	const Mapping* mp = mappingFor(id);
	auto l = loop();
	if (!mp || !l || !knobsOnSelection) return;
	const auto sel = selected();
	float sum = 0;
	for (size_t s : sel) sum += l->slices[s].p.*mp->field;
	std::vector<SliceParams> next;
	for (size_t s : sel) {
		next.push_back(l->slices[s].p);
		next.back().*mp->field = sum / float(sel.size());
	}
	auto edited = setSliceParams(*l, sel, next);
	commitEdit(l, edited, "set slices to mean");
	shownLoop = nullptr;  // re-read the knobs from the new values
}

bool Spools::selectionSpread(int id, float& lo, float& hi) {
	const Mapping* mp = mappingFor(id);
	auto l = loop();
	if (!mp || !l || !knobsOnSelection || shownFor.size() < 2) return false;
	lo = INFINITY;
	hi = -INFINITY;
	for (size_t s : shownFor) {
		if (s >= l->slices.size()) return false;
		float v = mp->knobOf(mp->units(globalStash[id]) + l->slices[s].p.*mp->field);
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
	return true;
}
