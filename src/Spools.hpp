#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include "plugin.hpp"
#include "core/exchange.hpp"
#include "core/length.hpp"
#include "core/loop.hpp"
#include "core/slicing.hpp"
#include "core/timebase.hpp"
#include "core/transport.hpp"
#include "core/voice.hpp"
#include "core/worker.hpp"

// Slice map and loop settings as saved in the patch, applied once the audio has loaded.
struct SavedLoop {
	spools::Range crop;
	double bpm = 0;
	bool bpmKnown = false;
	int meterNum = 4, meterDen = 4;
	std::vector<spools::Slice> slices;
	size_t regionStart = 0, regionEnd = 0;
};

json_t* sliceParamsToJson(const spools::SliceParams& p);
void sliceParamsFromJson(json_t* j, spools::SliceParams& p);

// Tail modes offered for the TAIL setting, in menu order (DESIGN §6.5).
struct TailChoice { spools::TailMode mode; const char* name; };
const std::vector<TailChoice>& tailChoices();

struct Spools : Module {
	enum ParamId {
		START_PARAM, LENGTH_PARAM, START_TRIM_PARAM, END_TRIM_PARAM, PITCH_PARAM, VOLUME_PARAM,
		OVERLAP_PARAM, DURATION_PARAM, QUANTIZE_PARAM, BPM_PARAM, REGION_START_PARAM, REGION_END_PARAM,
		LOOP_COUNT_PARAM, DIRECTION_PARAM, TAIL_PARAM,
		START_ATT_PARAM, LENGTH_ATT_PARAM, START_TRIM_ATT_PARAM, END_TRIM_ATT_PARAM, PITCH_ATT_PARAM,
		VOLUME_ATT_PARAM, OVERLAP_ATT_PARAM, QUANTIZE_ATT_PARAM,
		FILTER_TYPE_PARAM, CUTOFF_PARAM, RES_PARAM, CUTOFF_ATT_PARAM, RES_ATT_PARAM,
		FILTER_ENV_AMOUNT_PARAM, FILTER_A_PARAM, FILTER_D_PARAM, FILTER_S_PARAM, FILTER_R_PARAM,
		AMP_A_PARAM, AMP_D_PARAM, AMP_S_PARAM, AMP_R_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, CLOCK_INPUT,
		START_INPUT, LENGTH_INPUT, START_TRIM_INPUT, END_TRIM_INPUT, PITCH_INPUT, VOLUME_INPUT,
		OVERLAP_INPUT, QUANTIZE_INPUT, CUTOFF_INPUT, RES_INPUT, RUN_INPUT, CONT_INPUT, BAR_INPUT,
		INPUTS_LEN
	};
	enum OutputId { LEFT_OUTPUT, RIGHT_OUTPUT, SLICE_OUTPUT, TRIG_OUTPUT, EOC_OUTPUT, AMP_ENV_OUTPUT, FILTER_ENV_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	static constexpr int kMaxVoices = 16;
	static constexpr double kMaxEmbedSeconds = 5 * 60;

	spools::Exchange<spools::Loop> exchange;
	spools::TimeBase timebase;
	spools::Voice voices[kMaxVoices];
	spools::Transport transport;
	spools::Transport::Out transportState;
	dsp::SchmittTrigger runTrigger, contTrigger, barTrigger;
	dsp::SchmittTrigger clockTrigger, gateTriggers[kMaxVoices];
	dsp::PulseGenerator trigPulses[kMaxVoices], eocPulses[kMaxVoices];
	dsp::ClockDivider paramDivider;
	spools::VoiceParams globalParams;

	// Settings (UI thread writes, audio thread reads).
	std::atomic<int> ppqn{1};
	std::atomic<bool> eco{false};
	std::atomic<bool> embedAudio{true};
	std::atomic<bool> retriggerPerNote{false};
	static constexpr int kNoteMode = 0, kLoopMode = 1;
	std::atomic<int> playMode{kNoteMode};
	bool wasLoopMode = false;  // audio thread
	spools::LoopSequencer loopSeqs[kMaxVoices];
	std::atomic<float> barBeats{0};      // 0 = from the loop's meter
	std::atomic<int> barsPerCycle{0};
	// Display state (UI thread).
	int displayPage = 0;
	float viewStart = 0, viewSpan = 1;  // visible part of the loop, as fractions of its length
	bool cropView = false;              // LOOP page shows the whole source file with crop handles
	std::vector<size_t> selection;      // sorted slice indices
	const spools::Loop* selectionOf = nullptr;
	// The selection, emptied when the slice list it refers to has changed shape.
	std::vector<size_t>& selected();
	// Selection editing (DESIGN §8): while slices are selected, the per-slice knobs show and edit the selection,
	// and the audio thread reads the global values set aside in `globalStash`.
	std::atomic<bool> knobsOnSelection{false};
	std::atomic<float> globalStash[PARAMS_LEN];
	float knob(int id) { return knobsOnSelection ? globalStash[id].load(std::memory_order_relaxed) : params[id].getValue(); }
	void syncSelectionKnobs();                  // UI thread, every frame
	void flattenSelection(int paramId);         // sets every selected slice to the selection's mean
	bool selectionSpread(int paramId, float& lo, float& hi);  // knob values of the lowest and highest selected slice
	json_t* paramsToJson() override;
	std::vector<size_t> shownFor;
	const spools::Loop* shownLoop = nullptr;
	float shown[PARAMS_LEN] = {};
	// Publishes an edited loop and records it for undo; `before` is what undo goes back to.
	void commitEdit(std::shared_ptr<const spools::Loop> before, std::shared_ptr<const spools::Loop> after, const std::string& name,
		bool publishIt = true);
	// Runs a slow edit (re-slice, crop, ...) on the worker; it becomes undoable once flushHistory() sees it.
	void postEdit(const char* name, std::function<std::shared_ptr<spools::Loop>(const spools::Loop&)> edit);
	void flushHistory();  // UI thread
	// Analyses slices for the tail modes in the background whenever the slice list changes (UI thread).
	void ensureFeatures();
	const spools::Loop* featuresFor = nullptr;
	struct FinishedEdit { const char* name; std::shared_ptr<const spools::Loop> before, after; };
	std::mutex editsMutex;
	std::vector<FinishedEdit> finishedEdits;    // automatic CONT every N bars; 0 = never
	spools::SliceSettings slicing;  // UI thread only; workers get a copy
	spools::CropSpec::Unit cropUnit = spools::CropSpec::Seconds;  // unit shown in the crop fields
	spools::TapTempo tapTempo;

	// Display feedback from the audio thread.
	std::atomic<int> playingSlice[kMaxVoices];
	std::atomic<float> playheads[kMaxVoices];  // source frame, < 0 when idle
	std::atomic<int> activeChannels{0};
	std::atomic<float> currentBpm{120};

	// Load state (UI thread).
	std::mutex statusMutex;
	std::string status;  // last load error, shown on the display
	void setStatus(std::string s) { std::lock_guard<std::mutex> lock(statusMutex); status = std::move(s); }
	std::string getStatus() { std::lock_guard<std::mutex> lock(statusMutex); return status; }
	std::atomic<bool> loading{false};
	std::atomic<uint64_t> generation{0};
	uint64_t savedGeneration = ~0ull;

	std::unique_ptr<spools::Worker> worker;  // last member: joined first on destruction

	Spools();
	~Spools() override;

	void process(const ProcessArgs& args) override;
	void onSave(const SaveEvent& e) override;
	void onReset(const ResetEvent& e) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;

	// Loads in the background. `saved`, if given, replaces the file's own slice map.
	void load(const std::string& path, std::shared_ptr<SavedLoop> saved = nullptr);
	void publish(std::shared_ptr<const spools::Loop> loop);
	std::shared_ptr<const spools::Loop> loop() { return exchange.latest(); }
	void sliceEvenly(size_t count);
	void reslice();  // re-runs auto-slicing with the current settings
	void setCrop(spools::Range crop);  // plain audio is re-sliced, other files keep their slices inside the crop
	void setLoopBpm(double bpm);

	int regionIndex(float frac, size_t slices) const;
};
