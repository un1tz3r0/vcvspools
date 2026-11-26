#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include "plugin.hpp"
#include "exchange.hpp"
#include "loop.hpp"
#include "timebase.hpp"
#include "voice.hpp"
#include "worker.hpp"

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

struct Spools : Module {
	enum ParamId {
		START_PARAM, LENGTH_PARAM, START_TRIM_PARAM, END_TRIM_PARAM, PITCH_PARAM, VOLUME_PARAM,
		OVERLAP_PARAM, DURATION_PARAM, QUANTIZE_PARAM, BPM_PARAM, REGION_START_PARAM, REGION_END_PARAM,
		LOOP_COUNT_PARAM, DIRECTION_PARAM, TAIL_PARAM,
		START_ATT_PARAM, LENGTH_ATT_PARAM, START_TRIM_ATT_PARAM, END_TRIM_ATT_PARAM, PITCH_ATT_PARAM,
		VOLUME_ATT_PARAM, OVERLAP_ATT_PARAM, QUANTIZE_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GATE_INPUT, CLOCK_INPUT,
		START_INPUT, LENGTH_INPUT, START_TRIM_INPUT, END_TRIM_INPUT, PITCH_INPUT, VOLUME_INPUT,
		OVERLAP_INPUT, QUANTIZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { LEFT_OUTPUT, RIGHT_OUTPUT, SLICE_OUTPUT, TRIG_OUTPUT, EOC_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	static constexpr int kMaxVoices = 16;
	static constexpr double kMaxEmbedSeconds = 5 * 60;

	spools::Exchange<spools::Loop> exchange;
	spools::TimeBase timebase;
	spools::Voice voices[kMaxVoices];
	dsp::SchmittTrigger clockTrigger, gateTriggers[kMaxVoices];
	dsp::PulseGenerator trigPulses[kMaxVoices], eocPulses[kMaxVoices];

	// Settings (UI thread writes, audio thread reads).
	std::atomic<int> ppqn{1};
	std::atomic<bool> eco{false};
	std::atomic<bool> embedAudio{true};

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

	int regionIndex(float frac, size_t slices) const;
};
