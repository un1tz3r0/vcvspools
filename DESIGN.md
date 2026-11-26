# Spools — Design

Spools is a VCV Rack 2 loop player and slicer in the spirit of ReCycle/Dr. Rex. It imports a loop, slices it on onsets or a beat grid, and plays the slices back from V/oct + gate or a free-running transport. Slices follow an external clock: tempo changes move slice *start times* and leave pitch alone.

Status: draft for sign-off. Implementation order lives in `TODO.md`.

## 1. Goals and non-goals

**Goals**
- Import REX2 (`.rx2`, unofficial), WAV, AIFF, FLAC, MP3 and Ogg Vorbis. Read slice and tempo metadata from Acidized WAV (`acid`, `smpl`, `cue `, `LIST/adtl`).
- Crop any region of the source file, by offset+duration in beats (at a BPM), seconds or samples.
- Detect tempo, or take a manual BPM. Slice on onsets, on a beat grid, or both, with adjustable onset sensitivity and grid-snap strength.
- Trigger slices from poly V/oct + gate. C4 (0 V) plays the start slice and each semitone is a slice offset. Each trigger plays `length` slices, timed to the incoming clock.
- Free-running loop mode with RUN, CLOCK, BAR and CONT inputs.
- Per-voice multimode filter, amp and filter ADSRs, and crossfades between overlapping slices.
- Global offsets plus per-slice parameters, edited through the display with multi-selection.
- Tail modes for when a slice's time slot and its audio length differ: cut, ring, loop, auto-loop, granular stretch/squeeze.
- Export as an Acidized WAV with slice markers.
- Large, colourful NanoVG display: waveform, slices, beat grid, playheads, loop region.

**Non-goals**
- Writing REX files. Official REX SDK integration. VCV Library distribution: releases are GitHub binaries only.
- Phase-vocoder time-stretch. Granular stretch is the only stretch algorithm.
- Effects beyond the per-voice filter. Sends are a later milestone (§11).

## 2. Architecture

Two layers:

```
core/   Rack-independent C++17 library: decoding, analysis, slicing, DSP, sequencing.
        Unit-tested on the host with doctest. Depends only on vendored single-header libs.
src/    Rack module, widgets, panel, JSON persistence. Thin glue over core/.
```

The plugin builds with `-std=c++17`, set after `plugin.mk` in the Makefile. `core/` never includes `rack.hpp`, so `make test` builds and runs `tests/` with the host compiler and no SDK.

### Threading

- **Audio thread** (`Module::process`) only reads an immutable `std::shared_ptr<const Loop>`. It never allocates, locks or frees.
- **Worker thread** (one per module, started lazily) handles decoding, resampling of analysis buffers, onset/tempo analysis, slicing, export and patch-storage writes. It builds a new `Loop` and publishes it with `std::atomic_store`. The old `Loop` is handed back over a lock-free SPSC queue so the worker frees it, not the audio thread.
- **UI thread** sends edit commands (slice moves, per-slice params, re-slice requests) over an SPSC command queue. Cheap edits, such as a per-slice param change, mutate a small `SliceParams` array that is double-buffered with an atomic index. Structural edits (adding, removing or moving slices, re-cropping) go through the worker and produce a new `Loop`.
- **Display** reads the current `Loop`, voice playheads and selection through atomics. Waveform min/max mip levels are part of `Loop` and computed on the worker.

## 3. Data model (`core/`)

```cpp
struct Audio {            // planar float, 1-2 channels, native sample rate
  float sampleRate; int channels; std::vector<float> data[2]; size_t frames;
};
struct SliceParams {      // per-slice values, summed with global offsets (§6.7)
  float startTrim = 0, endTrim = 0;   // seconds, ±0.25
  float gain = 0;                     // dB
  float pan = 0;                      // -1..1
  float pitch = 0;                    // semitones
  float cutoff = 0, res = 0;          // octaves, 0..1 offset
  float xfade = 0;                    // seconds offset
  bool  reverse = false;
  TailMode tail = TailMode::Inherit;
};
struct Slice { size_t start; double beat; SliceParams p; };   // end = next slice's start
struct Loop {
  std::shared_ptr<const Audio> source;     // full decoded file
  Range crop;                              // frames into source
  std::shared_ptr<const Audio> audio;      // cropped view, playback buffer
  double bpm; int beatsPerBar; double beats;
  std::vector<Slice> slices;
  Analysis analysis;                       // onset envelope, per-slice transient end, pitch period, loop candidates
  WaveformMips mips;
  std::string sourcePath; Origin origin;   // Rex, Acid, Plain
};
```

Slice beat positions come from the grid (or from REX/Acid metadata). Every timing decision in the sequencer is made in beats.

## 4. Import, crop, export

| Format | Library |
|---|---|
| WAV, AIFF, W64 | `dr_wav` (vendored, public domain/MIT-0) |
| FLAC | `dr_flac` |
| MP3 | `dr_mp3` |
| Ogg Vorbis | `stb_vorbis` |
| REX2 (`.rx2`) | `core/rex.cpp`: our own C++ port of the reverse-engineered IFF + DWOP description in [schwung-rex](https://github.com/charlesvestal/schwung-rex) (MIT), with full 24-bit precision. Labelled *unofficial* in the UI and docs. REX1 `.rex`/`.rcy` use a different, undocumented layout and are not supported yet. |

- **REX import** keeps the decoded audio, the slice positions, the tempo, the bar/beat signature and the loop length. Slice beat positions come from the REX PPQ positions. Ignored: REX envelope settings, transient-stretch settings, and anything else the decoder can't recover.
- **Acidized WAV import** reads the `acid` chunk (tempo, beats, root note, one-shot flag), `cue ` points with `LIST/adtl/labl` as slices, and `smpl` loops as the loop region. Without `acid`, beats are derived from analysis.
- **Crop** is non-destructive: `Loop.crop` is a range into `source`. Units are beats@BPM, seconds or samples. Offsets and durations are typed into context-menu text fields, or set by dragging the crop handles in *Crop view* (§8). Re-cropping keeps slices inside the new range and clips the rest.
- **Export** writes the cropped audio (32-bit float or 24-bit PCM) with `acid` (tempo/beats), `cue `+`LIST/adtl/labl` (one per slice) and `smpl` (loop region). Per-slice params go in an `spls` RIFF chunk (JSON) that other tools ignore. Spools re-imports its own exports losslessly.

## 5. Analysis (`core/analysis/`)

Runs on the worker after import or crop, and again when the sensitivity, grid or BPM settings change. Analysis is cached per crop, so re-slicing reuses the onset envelope.

1. **Onset envelope**: mono mix, STFT (1024/256, Hann, in-house radix-2 FFT), log-magnitude spectral flux, half-wave rectified, then high-pass filtered.
2. **Onset picking**: adaptive threshold = median over ±N frames + `δ`. `δ` is driven by the *Onset sensitivity* control. A minimum inter-onset gap applies. Each onset is refined to a local energy rise, then to the nearest preceding zero crossing.
3. **Tempo**: autocorrelation of the onset envelope, weighted by a log-normal prior centred on 120 BPM over 60–200, then a comb-filter bank to pick the octave. If crop duration × BPM is within 1.5 % of a whole number of bars, BPM is nudged to exactly fit (so loops loop). Manual BPM overrides this. *Halve*, *double* and *tap* are menu actions.
4. **Beat grid**: phase found by cross-correlating an impulse train with the onset envelope, or set to crop start when the crop is bar-aligned (the default).
5. **Slicing modes**: *Onsets*, *Grid* (1/4, 1/8, 1/16, 1/32, triplets), *Hybrid* (grid points plus strong onsets). *Grid snap strength* (0–100%) moves each onset toward its nearest grid line: `pos = lerp(onset, grid, s)`. Each slice's `beat` is its (snapped) position in beats.
6. **Per-slice features** for tail modes:
   - *Transient end*: the first frame after the attack peak where flux falls below a fraction of its peak and the envelope slope flattens.
   - *Fundamental period*: YIN over the post-transient region, with a confidence value.
   - *Auto-loop candidates* (§6.5).

## 6. Playback engine

### 6.1 Time base

- `beatPeriod` (seconds per quarter note) comes from CLOCK edges ÷ PPQN (1, 2, 4, 8, 24, 48, 96). It is smoothed with a short median, and jumps immediately on a large change. With no clock patched, it comes from the manual BPM or the loop BPM.
- A beat-phase accumulator advances by `dt / beatPeriod`. All sequencing runs on it, so tempo changes apply mid-slice.
- *Bar length* (beats per bar, from the loop by default) and *bars per cycle* (1–64 or ∞) define when an automatic CONT fires (§6.3).

### 6.2 Note mode (default)

- **Region**: `[loopStart, loopEnd]` in slice indices. Start-slice, offsets and phrases all wrap inside it.
- **Trigger**: on a gate rising edge per poly channel, `offset = round(12 · V/oct)` and `first = wrap(startSlice + offset)`. Negative offsets play earlier slices. Rising edges are delayed to the next *launch quantize* division (off, 1/32 … 1 bar) when that is enabled.
- **Phrase**: slices `first … first+length-1` (wrapping), in *direction* order. Slice *k* starts `Σ beatLen` beats after the trigger, where `beatLen = slice[k+1].beat - slice[k].beat`. A gate that falls before the first slice ends counts as a trigger and plays the phrase once. A gate still high at the first slice boundary is a held gate: it repeats the phrase at its end, up to *loop count* times (∞ allowed). When the gate falls, the current slice finishes and the voice releases.
- **Retrigger** on the same channel cuts the old phrase with the crossfade.
- **Poly**: up to 16 channels, following the V/oct/GATE channel count. Each channel is an independent voice.

### 6.3 Loop mode

- One transport voice plays the region continuously, with direction and count. RUN gates the transport, and its rising edge resets to the start slice.
- **CONT** (trigger): resets to `startSlice` and aligns the beat phase to the next clock edge. It is also injected automatically every *bars per cycle* bars, unless that is set to ∞.
- **BAR** (trigger): realigns the bar phase without resetting the slice position. Use it when an external sequencer loops at a bar boundary.
- V/oct in loop mode adds an offset to the slice position (beat-repeat/chop effects). The GATE input is ignored.

### 6.4 Direction and reverse

Phrase/sequence direction: *forward*, *reverse*, *ping-pong* (alternate passes forward and backward), *random* (a random slice of the phrase each step). Per-slice `reverse` plays the slice's audio backwards from its end trim, independent of the sequence direction.

### 6.5 Slice voice and tail modes

Each voice owns two `SlicePlayer`s, so an outgoing and an incoming slice can overlap during a crossfade. A `SlicePlayer` reads `audio` with 16-tap windowed-sinc interpolation (8-tap in eco mode) at rate `2^(pitch/12) · srcRate / engineRate`.

The slot is `beatLen · beatPeriod · duration`, where *duration* is a fraction 0.05–1 of the slice's slot (gate length). The audio is `(end - start) / rate`, trims included. The *tail mode* decides what happens when the two differ:

| Mode | slot > audio | slot < audio |
|---|---|---|
| **Cut** | silence after the audio | fade out at slot end (xfade time) |
| **Ring** | audio plays to its end; the amp env handles release | as Cut |
| **Loop** | post-transient region loops (fwd or ping-pong) with crossfade, from loop points set on the display | as Cut |
| **Auto-loop** | as Loop, with loop points chosen automatically (below) | as Cut |
| **Stretch** | post-transient region is granularly stretched to fill the slot | post-transient region is granularly squeezed; the transient keeps its original timing |

- **Auto-loop point search** (offline, per slice): candidate lengths are integer multiples of the fundamental period (or, if YIN confidence is low, lengths from an envelope-flatness search). Each candidate is scored by normalized cross-correlation of the windows at loop start and end, plus RMS and spectral-centroid continuity. The best one wins, then both ends snap to matching-slope zero crossings.
- **Granular stretch/squeeze**: the transient segment (onset → transient end, with a *transient preserve* control 0–100% that scales it) plays at native timing. The remainder plays as overlapping Hann grains. Grain size is about 2–4 periods (or 40 ms when no pitch is found), and grain hop is set so the result fits the slot. A small random position jitter reduces phasing. The *tail percentage* control limits how much of the slice may be stretched.

### 6.6 Per-voice DSP chain

`SlicePlayer ×2 (overlapping) → gain/pan → filter → amp VCA → L/R`

- **Crossfade**: the *overlap* control (0–50 ms, plus the per-slice xfade offset) sets how long the outgoing slice fades out, with a raised-cosine curve, under the incoming one. The incoming slice keeps its attack: it only gets the 1 ms declick, so transients stay sharp.
- **Filter**: TPT/ZDF state-variable filter, 12 dB LP/BP/HP/Notch, plus a 24 dB LP (two cascaded SVFs with resonance compensation). Cutoff is in V/oct around 261.6 Hz (C4), with 10 octaves of range. Coefficients are updated every 4 samples.
- **Envelopes**: amp ADSR and filter ADSR with a bipolar amount (±5 octaves), exponential segments. *Retrigger* is per slice (default) or per note.

### 6.7 Parameter resolution

`effective = clamp(global_knob + cv·attenuverter + slice.p.x)` for every per-slice parameter. Global-only parameters (envelopes, filter type, clock settings, region, length) use only knob + CV. This is evaluated at slice start, except pitch, cutoff, resonance and volume, which are also smoothed and updated every 32 samples.

## 7. Module I/O (40 HP, resizable)

**Inputs** (P = polyphonic)

| Jack | |
|---|---|
| V/OCT (P), GATE (P) | slice offset; trigger/gate |
| CLOCK, RUN, CONT, BAR | transport |
| CV: START SLICE, LENGTH, START TRIM, END TRIM, PITCH (P), CUTOFF (P), RES, VOL (P), XFADE, QUANT | each has a small attenuverter |

**Outputs**: L, R (P); SLICE (P, 1/12 V per slice index, so it can be patched straight back into V/OCT); TRIG (P, 1 ms on each slice start); EOC (P, end of phrase/cycle); AMP ENV (P), FILT ENV (P), both 0–10 V.

**Knobs**: start slice, length (1–64), start trim, end trim, pitch (±24 st), cutoff, resonance, volume, xfade overlap, duration, launch quantize, grid snap, onset sensitivity, BPM (manual; fully counter-clockwise = auto), loop start, loop end, loop count, amp ADSR, filter ADSR + amount.

**On the display** (Settings page and per-page toolbars, to save panel space): mode (note/loop/pattern), direction, tail mode, filter type, slicing mode/grid, PPQN, bar length, bars per cycle, retrigger mode, interpolation quality, transient preserve, tail %.

The panel was 36 HP until 2.10.1, when the per-slice outputs took a column on the right and it became 40 HP. From 2.11.1 it is drawn in NanoVG and can be dragged from 40 to 120 HP; extra width goes to the display.

## 8. Display and interaction

Drawn with NanoVG in `drawLayer(1)`, so it stays lit when the room lights are dimmed. The background is dark neutral and the content colour is light.

- **Legibility at 100% zoom** is a hard requirement. Rack draws 1 panel px = 1 screen px at 100% (a 36 HP panel is 540 px wide). Minimum sizes, in NanoVG px: 11 for display body text, 13 for tabs and readouts, 16+ for the main BPM/slice readout. Display text has ≥ 7:1 contrast on its background. Panel labels are ≥ 2.6 mm cap height in the SVG (about 10 px). Fine detail such as grid ticks can be thin, but never text. Every UI change is checked with a 100%-zoom screenshot before it is committed.
- **Pages**: a tab row across the top of the display picks the page.
  - **Loop**: the default page. It shows the cropped loop with slices, the region and playheads. A *Crop* toggle switches to the whole source file with crop handles.
  - **Slice**: a zoomed editor for the selected slice. It shows start/end trims, the transient-end marker, loop points (manual or auto), and the tail mode, with per-slice parameters as numeric fields.
  - **Pattern**: the remap sequencer (§11). It appears only once that milestone lands.
  - **Settings**: global options that don't fit on the panel (the list in §7).
- Pages are `Page` subclasses sharing a `DisplayContext` (the current `Loop`, the selection, the voice state and a param-edit API). Adding a page doesn't touch the others.
- **Drawing**: waveform from min/max mips (a level is chosen per pixel). Slices are filled with hues spread evenly around OKLCH. Beat and bar grid lines, the loop region shaded, one playhead per active voice tinted with its slice's hue, selected slices outlined, BPM/beats/slice count/selection readout, and an *unofficial REX* badge when relevant.
- **Mouse**:
  - Click a slice to select it, and click it again to deselect.
  - Shift/Ctrl-click adds to or removes from the selection. Click empty space to clear it.
  - Drag a slice boundary to move it (with grid snap unless Alt is held). Drag a trim handle for per-slice start/end trim. Drag the loop handles to set the region.
  - Double-click to add a slice. Ctrl-double-click a boundary to delete it.
  - Scroll to zoom, and drag with the middle button to scroll the view.
  - Right-click opens the context menu: load, export, crop fields, BPM tools, re-slice, reset slice params.
- **Selection editing**:
  - With no selection, the knobs are global offsets.
  - With a selection, the per-slice knobs (start/end trim, gain, pan, pitch, cutoff, res, xfade) edit the selected slices. On selection change each knob jumps to the mean of the selection (global value kept aside), and a ring on the knob shows the selection's spread.
  - Turning a knob applies the delta to every selected slice, so relative differences are kept. Shift-click on a knob first sets every selected slice to the mean, then adjusts absolutely.
  - Clearing the selection restores the global knob values.
  - CV always modulates the global offsets.
  - Every edit is undoable through Rack's history (`history::ModuleChange` with JSON snapshots of slice params).

## 9. Persistence

`dataToJson` stores the source path and origin, the crop, BPM and grid settings, slicing and analysis settings, the slices (position, beat, params), the mode and transport settings, and the view zoom. Selection is not saved.

By default the cropped audio is also written to the module's patch storage directory as `loop.wav`, using the exporter (§4). Patches therefore load without the original file. The audio cap is 5 minutes at the source rate. Menu option *Reference file only* skips embedding. Load order: patch storage, then source path, then "missing file" shown on the display with the slice map kept.

## 10. Testing and tooling

- `tests/`: doctest, one file per core area (decoders, rex, acid chunks, analysis, slicing, sequencer timing, tail modes, filter, envelopes, export round-trip). `make test` builds and runs them with the host compiler and no Rack SDK.
- Fixtures: `tools/make_fixtures.py` (numpy + stdlib `wave`) generates small synthetic loops: click trains at known BPM, sine+noise drums, chords for pitch/loop tests, and Acidized WAVs. Its output is committed so CI doesn't need Python. REX tests use small `.rx2` files from a permissively licensed source in `tests/fixtures/rex/`, and are skipped when none is present.
- DSP tests check behaviour: timing to ±1 sample, filter response at test frequencies, loop-point continuity, no allocation in `process` (via an allocation-counting hook in test builds).
- CI: a `test` job (Linux host) runs before the four platform builds.
- Benchmarks (`make bench`, not in CI): 16 voices, sinc16, stretch mode. Target under 3% of one core at 48 kHz on a modern x86.

## 11. Later milestones (designed for, not in the proof of concept)

- **Slice remap sequencer (Pattern mode)**: a second sequencer that plays slices in any order, for jungle/DnB-style re-chopping. It becomes a third mode next to note and loop, and is clocked by the same time base and transport.
  - A pattern has up to 64 steps. Each step holds: slice index (or rest), length in grid ticks (1/32 … 1 bar) plus a ± nudge, ratchet count (1–8) with a ratchet-rate curve, velocity with a ramp across the ratchets, duration (gate fraction), reverse and pitch offset.
  - Pattern-level loop start/end and count, plus a direction setting.
  - V/oct offsets the slice index of every step, as in loop mode.
  - Editing happens on the Pattern page. Steps are drawn as blocks tinted with their slice's hue. Drag vertically to change the slice, drag an edge to change the length, scroll for ratchets, and Alt-drag for velocity. *Init from loop* (identity order), *shuffle* and *reverse* are menu actions.
  - Patterns are saved in the patch JSON and in the exported `spls` chunk.
- Per-slice FX send A/B (poly outputs) and per-slice CV A/B outputs, edited like other per-slice params.
- Resizable panel: the panel is drawn in NanoVG, the display grows with width, and controls stay in a fixed-width column.
- Expanders, if the panel runs out of room.

## 12. Licensing and dependencies

- Spools: GPL-3.0-or-later (`LICENSE` added in the first code milestone).
- Vendored: dr_wav/dr_flac/dr_mp3 (public domain/MIT-0), stb_vorbis (public domain/MIT), pffft (BSD-like, from Rack's SDK), doctest (MIT, tests only), schwung-rex format description and reference decoder (MIT; attribution in `vendor/README.md`).
- REX support is unofficial and unaffiliated with Reason Studios. Docs say so, and bugs go to this repo.

## 13. Risks

- **REX decoder coverage**: REX1 `.rex`/`.rcy` don't decode, and the decoder has only been checked against an independent encoder, not against ReCycle-made files. Mitigation: the `RexReader` interface, a clear error on the display, and the Acid WAV path as a fallback.
- **CPU**: 16 voices × 2 players × sinc16 + stretch. Mitigation: an eco interpolation option, voices that skip silence, and benchmarks per milestone.
- **Knob retargeting** fights Rack's param model (the knob shows the selection value while CV modulates the global value). Mitigation: global values live in module state, and the knobs hold only the "current target" value.
- **Panel space** at 36 HP (§7).
