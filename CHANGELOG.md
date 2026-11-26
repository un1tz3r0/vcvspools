# Changelog

Versions follow `TODO.md`: each `2.N.p` heading is one task, and the last one in a milestone is the released version.

## 2.9.3

- **PATTERN** page:
  - steps drawn as blocks sized by length and tinted by slice;
  - drag a step to change its slice, Alt-drag for velocity, drag its edge for length, scroll for ratchets;
  - drag tabs to set the play range;
  - double-click to duplicate or add a step, Ctrl-double-click to delete;
  - fields for every step setting;
  - a marker under each step that a voice is playing.
- Right-click → **Pattern**: Init from loop, Shuffle slices, Reverse steps, Add rest, Delete step, Play every step, Direction and Loop count.
- The pattern now belongs to the loop. Pattern edits go through the same publish and undo path as slice edits. The pattern is saved in the patch and, with per-slice settings, in the embedded WAV's `spls` chunk, which is read back on load. Before this, the manual said per-slice settings went into `spls`, but nothing wrote them.

## 2.9.2

- Ratchets: a pattern step can play its slice 1–8 times within its length. The ratchet curve spaces the hits evenly (0), as a roll that speeds up (> 0) or one that slows down (< 0). Each hit is gated for its own share of the step.
- Velocity: each step sets a level for its first hit, and a ramp moves later hits up or down from it. The level follows into the voice's gain without breaking the per-slice level glide.
- A random pass picks a step once per slot, so all its ratchet hits play the same step.

## 2.9.1

- **Pattern mode** (SETTINGS → Mode → Pattern), the third play mode, runs on the loop-mode transport. A pattern (`core/pattern.hpp`) has steps, each with:
  - a slice or a rest;
  - a length in 1/32-note ticks and a ± nudge;
  - a gate length, reverse and a pitch offset.

  Each pattern also has a loop range, a loop count and a direction.
- `PatternSequencer` sets its position from the transport's loop beat, like loop mode. V/OCT shifts every step's slice. Reverse and ping-pong passes play the steps, and their lengths, backwards. Random picks each slot's step. Rests release the voice.
- A step's gate length, pitch and reverse are added to the slice's own settings, and the pitch keeps following knob changes.
- A pattern is made from the loop, one step per slice, whenever a new file loads. The editor and saving come in 2.9.3.

## 2.8.2

- Manual:
  - an introduction;
  - an Inputs table with every CV scaling;
  - a Tail modes section covering all five modes;
  - an Engine section on interpolation quality and CPU use;
  - pictures of the panel and the SLICE page.
- README: a feature summary and images of the panel and the three display pages. The images are 100%-scale previews rendered from the layout code, not Rack screenshots yet. `make bench` is now in the build notes.

## 2.8.1

- `make bench`: 16 voices at 48 kHz in Ring, Auto-loop and Stretch modes, best and eco quality, with and without a filter, plus an idle case. It uses Rack's optimisation flags and reports % of one core and ns per voice-sample (results in docs/development.md).
- About twice as fast as 2.7.5:
  - one sinc kernel lookup per frame instead of one per channel, with the cutoff band cached per rate and fixed-size dot products;
  - envelope, glide and filter coefficients recomputed only when their inputs change;
  - a Hann table for grains;
  - voices that have been silent for 0.1 s skip everything but their envelopes.
- On the 2.1 GHz development VM, Ring uses 3.2 % of one core and Stretch about 4.5–5 %, which is just over the 3 % target. It hasn't been measured on a desktop CPU yet.

## 2.7.5

- **Stretch** tail: granular time-stretch and squeeze that fits each slice to its gate at the original pitch (DESIGN §6.5). The attack keeps its timing, and the rest plays as 50 %-overlapping Hann grains. For pitched slices, grains are about three periods long and start a whole number of periods from the grain they overlap, so they reinforce instead of beating. Unpitched slices use 40 ms grains with jitter. The first and last grains join the plain playback at full level.
- SETTINGS gains **Keep attack** (transient preserve) and **Stretch tail** (tail %), both saved with the patch. The SETTINGS rows are 17 px so all 17 options fit.
- The SLICE page shades the stretched region and shows the grain size.

## 2.7.4

- **Auto-loop** tail: each slice's analysis now searches its post-attack audio for a seamless loop (DESIGN §6.5). Candidate lengths are whole pitch periods (or 20 ms upward when the pitch is unclear). They are scored by the correlation, level match and zero-crossing-rate match of the windows before the loop end and start, with a slight preference for longer loops, and snapped to rising zero crossings.
- The SLICE page shows the found loop and its score. Dragging a loop flag in Auto-loop switches the slice to Loop with manual points.

## 2.7.3

- **Loop** tail: after its attack, a slice repeats a loop region until its gate ends. The region is set by hand or found automatically (transient end to slice end). Loops play forward with a raised-cosine crossfade into the audio before the loop start (SETTINGS → **Loop xfade**, 10 ms by default), or ping-pong. Reversed slices don't loop.
- New **SLICE** page: a close-up of one slice with its trims, transient end and draggable loop flags, plus its per-slice settings as fields (drag numbers, click choices). The ‹ › buttons in the tab row step through slices.
- Loop points and the ping-pong flag are saved with the patch.

## 2.7.2

- Cut and Ring follow DESIGN §6.5 and have tests for both cases. When the slot is longer than the audio, the slice plays once with no click. When it is shorter, the slice fades at the slot end over the overlap time.
- Every tail mode except Ring is gated at DURATION × slot, now clamped to 5–100 %. The tail modes come from one table shared by the TAIL switch, the SETTINGS page and the menu, ready for Loop, Auto-loop and Stretch.

## 2.7.1

- Per-slice features for the tail modes (`core/features.hpp`): the transient end (where the short-term level has fallen below half its peak and flattened) and a YIN fundamental-period estimate of the part after it, with a confidence value. In tests the period is within 0.5 % from 55 Hz to 1 kHz.
- Features are computed on the worker whenever the slice list changes and stored with the loop. Slice edits that move boundaries clear them.
- `Exchange::replace` publishes a derived result only if nothing newer was published in the meantime, so background analysis never overwrites an edit.

## 2.6.6

- Crop view: the **CROP** toggle in the LOOP page's toolbar (the tab row) shows the whole source file with the crop as a bright window. Drag either edge, or drag the window to slide it. The crop applies on release, and plain audio is re-sliced. The source waveform's mips are built once, the first time the view opens for a file.
- Pages can put controls in the tab row (`drawToolbar`/`onToolbarButton`).
- Slow edits (crop, re-slice, slice evenly, loop tempo) now run on the worker and land in Rack's undo history when they finish.

## 2.6.5

- Selection editing: while slices are selected, IN, OUT, PITCH, LEVEL, XFADE, CUTOFF and RES show the selection's mean and edit its slices. Turning a knob moves every selected slice by the same amount, in the slice's own units (seconds, semitones, dB, octaves). Shift-click sets the selection to its mean, as one undo step. An amber ring shows the selection's spread.
- The global knob values are set aside while editing. The audio thread and the saved patch both use them, and they come back when the selection is cleared.
- The LOOP header shows how many slices are selected.

## 2.6.4

- Mouse editing on the LOOP page: select, deselect and multi-select slices; drag boundaries with grid snap (Alt for free); double-click to add a slice and Ctrl-double-click to delete one; drag per-slice trim handles; drag region handles, which set FROM/TO; scroll-wheel zoom around the pointer; middle-drag to scroll, with a position bar.
- Slice-map edits (`core/edit.hpp`: move, add, remove, region, per-slice settings) return a new loop that shares the audio, waveform mips and analysis of the old one. The mips are now shared between loops, so undo history costs only the slice lists.
- Undo and redo through Rack's history for every edit, including region drags.
- View zoom and position are saved with the patch. The selection is not saved.
- Right-clicks on the display open the module menu again.

## 2.6.3

- LOOP page drawing: slice colours are evenly spaced OKLCH hues at equal lightness, with alternate slices on opposite halves of the circle so neighbours never look alike (`core/color.hpp`). Beat and bar grid at the loop's own tempo. The region gets an amber bracket and the slices outside it are dimmed. Each playhead and its slice marker take that slice's colour.
- Header: a 16 px BPM readout, the slice count and length, outlined LOOP MODE and REX badges, and the file name clipped to the space that is left.

## 2.6.2

- Display page framework: a tab row (13 px) picks a `Page`; pages share a `DisplayContext` (module, loop, drawing helpers, page area) and get clicks, drags and scrolls in page coordinates. The chosen page is saved with the patch.
- LOOP page: the existing waveform view, moved into its own file.
- SETTINGS page: all 14 global options in three columns; clicking a row opens its choices.
- One settings table (`src/Settings.cpp`) feeds both the page and the context menu, which now groups the options as Playback, Slicing, Transport and Engine.

## 2.6.1

- Final 36 HP layout: taller display (136 px, room for the page tabs), a shaded tone strip with filter and envelope controls, the main knob row with a CV amount and jack under each, and boxed input and output rows. Checked against a 100 %-scale render in light and dark: all labels are 12 px Nunito Bold, no overlaps.
- Grid snap, onset sensitivity and loop count go on the display's Settings page (2.6.2) instead of panel knobs. DESIGN §7 left the exact layout to this milestone; the panel has no room for them without shrinking the main knobs.

## 2.5.2

- Loop mode (right-click → **Mode**): the region plays continuously from the start slice, positioned straight from the transport's loop beat, so CONT, BAR and tempo changes never drift. Direction and loop count apply per pass, and the slot timing always follows the region's own slices.
- V/oct in loop mode offsets which slice sounds in each slot (wrapping in the region). Each poly V/OCT channel is a separate transport voice. GATE is ignored.
- `Voice::play` takes events from either sequencer. Switching modes silences all voices. The display header shows LOOP in loop mode.

## 2.5.1

- Loop transport (`core/transport.hpp`): RUN gate (unpatched = running, rising edge restarts), CONT (restarts on the next clock edge, or at once when free-running), BAR (realigns the bar phase only), and an automatic CONT every *bars per cycle* bars at exact bar multiples.
- RUN, CONT and BAR inputs. **Transport** menu with bar length (from the loop's meter by default) and bars per cycle (1–64 or ∞), saved in the patch. Loop mode uses them in 2.5.2.
- Panel (interim until 2.6.1): FROM/TO moved under DUR and BPM. Direction, tail and loop count moved to a **Playback** menu, as DESIGN §7 puts them on the display.

## 2.4.3

- Parameter resolution in one place: knob + CV·amount + per-slice offset, clamped per parameter (`core/params.hpp`).
- Pitch, level, cutoff and resonance glide (5 ms one-pole) toward targets re-resolved every 32 samples, on a 4-sample control tick. Level changes now reach a slice that is already playing, and each of the two overlapping players keeps the level of its own slice. A new slice starts at its own pitch without gliding in.
- Knob-only values (envelopes, filter type, tail, quality) are read every 32 samples instead of every sample.

## 2.4.2

- Amp and filter ADSR envelopes with exponential segments (0–10 s), a ±5 octave filter envelope amount, and AENV/FENV outputs (0–10 V, polyphonic).
- Each of a voice's two slice players has its own amp envelope, so a new slice can attack from silence while the previous one keeps its level as it fades out.
- *Envelope retrigger* menu: each slice (default) or each note. The setting is saved in the patch.
- A long amp release lets a slice ring past the end of its note. The player stops once the release reaches −60 dB.
- Bottom row re-spaced to fit the two new outputs (temporary until 2.6.1).

## 2.4.1

- Per-voice stereo filter: zero-delay-feedback state-variable filter with 12 dB low-pass, band-pass (unity at the peak), high-pass and notch, plus a 24 dB low-pass made of two stages tuned as a 4-pole Butterworth, with resonance on the second stage. Coefficients are updated every 4 samples.
- Cutoff in V/oct around C4 (16 Hz to 16.7 kHz on the knob, 10 Hz to 0.49 × sample rate with CV), resonance 0–100 %. Per-slice cutoff and resonance offsets are added.
- FILTER, CUTOFF and RES controls with CV in a strip under the display, which is now shorter. This layout is temporary until the final panel (2.6.1).

## 2.3.5

- **Crop** submenu: start and length text fields that accept `s`, `ms`, `b`, `bars` and `smp` suffixes, a default-unit choice, and *Whole file*. Plain audio is re-sliced and its tempo re-estimated after a crop.
- **Loop tempo** submenu: BPM text field, halve, double and tap tempo (averages up to 8 taps; the menu stays open while tapping).
- A detected tempo is now kept as an estimate, so the display shows `?` until the tempo comes from the file or from you.

## 2.3.4

- Auto-slicing modes: Onsets, Grid (1/4 to 1/32 and triplets) and Hybrid (grid plus strong off-grid hits), with grid-snap strength and sensitivity.
- The grid is aligned to the median offset of detected hits, so flux-envelope latency no longer shifts it by a few ms. Slices shorter than the minimum gap at the end are dropped.
- Plain audio files are sliced and tempo-estimated on load. The onset envelope is cached on the loop, so re-slicing only re-picks.
- Context menu **Auto-slice** submenu; each change re-slices on the worker. Settings are saved in the patch.

## 2.3.3

- Tempo estimation: autocorrelation of the onset envelope scored over four lag multiples (comb), with a log-normal prior centred on 120 BPM between 60 and 200 BPM, plus a confidence value.
- Loops within 1.5 % of a whole number of bars snap to the exact tempo with a zero grid phase. Otherwise the grid phase is found by correlating a beat comb with the envelope.

## 2.3.2

- Onset picking: local maxima of the envelope above a 0.2 s running median plus a sensitivity-controlled delta, with a minimum gap that merges flams. Each onset is refined in the waveform to where the hit starts rising, within about 2 ms in tests, then moved back to the preceding zero crossing.

## 2.3.1

- Onset envelope: Hann STFT (1024/256) of the mono mix, log-compressed magnitudes, half-wave rectified spectral flux, and a ~0.1 s moving-average high-pass.
- Built on a small in-house radix-2 FFT instead of pffft. Analysis runs offline on the worker, so this keeps `core/` dependency-free.

## 2.2.6

- 36 HP panel with light and dark themes. Labels are drawn with NanoVG at 12 px (≈3 mm caps) so they stay legible at 100% zoom.
- Display drawn on the light layer, so it stays lit with the room lights dimmed. It shows the file name, live BPM, slice count, length and an "unofficial REX" tag, a waveform coloured per slice, slice numbers, region shading, and one playhead per voice with its slice highlighted. It also shows the load status and errors.

## 2.2.5

- First playable module. It loads a file from the context menu on a background thread and plays 16 poly voices from V/OCT and GATE, locked to CLOCK (1–96 PPQN, menu) or the manual/loop BPM.
- Controls: start slice, length, start/end trims, pitch, volume, overlap, duration, launch quantize, manual BPM, region start/end, loop count, direction and tail (Ring/Cut). CV inputs with attenuverters for start, length, trims, pitch, volume, overlap and quantize.
- Outputs: L, R, SLICE (1/12 V per slice), TRIG and EOC, all polyphonic.
- The patch saves the slice map with per-slice params. The cropped audio is embedded in patch storage as an Acidized WAV (up to 5 minutes; menu toggle), and is used when the original file is missing.
- Menu: *Slice evenly* (until analysis lands), eco interpolation.
- A clearer error for REX1 `.rex`/`.rcy` files.

## 2.2.4

- `Voice`: a phrase sequencer driving two slice players. The outgoing slice fades out with a raised cosine over the overlap time while the incoming slice keeps its attack.
- Tail modes so far: Ring (the slice plays out) and Cut (gated at `duration` × its slot).
- Global and per-slice trims, gain, pan, pitch and reverse are summed (reverse is XORed). Mono uses equal-power pan and stereo uses balance. Pitch changes take effect mid-slice.
- Reports slice index, trigger and end-of-phrase for the SLICE/TRIG/EOC outputs. Swapping the loop hard-stops players that point at the old audio.

## 2.2.3

- Note-mode `NoteSequencer`: V/oct semitones offset the start slice and wrap both ways inside the region. Each trigger plays `length` slices, scheduled in beats, so clock tempo changes rescale their start times. Directions are forward, reverse, ping-pong (alternating passes) and random within the phrase.
- A gate held past the first slice repeats the phrase up to the loop count (0 = forever). Releasing it finishes the current slice. A shorter gate counts as a trigger and plays the whole phrase.
- Launch quantize to the next beat division, retrigger, end-of-phrase and release events.

## 2.2.2

- `SlicePlayer`: plays a span of a loop at any rate with Blackman-windowed sinc interpolation, 16 taps or 8 in eco mode. Cutoff tables band-limit reads faster than 1:1.
- Forward or reverse playback, start/end trims in seconds clamped to the audio, 1 ms declick ramps that end exactly at the span edge, and `stop()` with any fade time.

## 2.2.1

- `TimeBase`: a beat clock in quarter notes. Tempo comes from CLOCK pulses at 1–96 PPQN, with a median-of-three filter that rejects jitter and follows real tempo jumps at once. The phase locks to pulses and never runs past a late pulse. With no clock patched it falls back to the manual or loop BPM.

## 2.1.5

- `Loop` model: a non-destructive crop of the source file, a sorted slice map with beat positions and per-slice parameters, the loop region, and a min/max waveform pyramid for drawing.
- Crop by samples, seconds or beats at a BPM, clamped to the file.
- `loadLoop` builds a loop from REX2 (slices and native tempo), Acidized WAV (tempo, cue slices, smpl loop as the region) or plain audio. `recrop` keeps slices and their parameters, and `sliceEvenly` makes equal slices.
- `Exchange<T>`: lock-free handoff of immutable objects to the audio thread. Retired objects are freed on the producer side.
- `Worker`: a background job thread. Both are clean under ASan, UBSan and TSan.

## 2.1.4

- Unofficial REX2 (`.rx2`) import: IFF container and DWOP lossless decoder, mono and L/delta stereo, 16- and 24-bit at full precision. Reads slices (transient markers are dropped), tempo, meter, and bars/beats. The native tempo is derived from the loop length. Bounds-checked and safe on corrupt input.
- Bit-exact against fixtures made with schwung-rex's encoder.
- REX1 `.rex`/`.rcy` are not supported yet.

## 2.1.3

- Reads WAV loop metadata: `acid` (tempo, beats, meter, root note, one-shot), `cue ` points with `LIST/adtl/labl` labels, the `smpl` loop and root note, and the Spools `spls` chunk. Parsing is bounds-checked against truncated files.
- Acidized WAV writer (16-bit, 24-bit or 32-bit float) that emits the same chunks. Output is verified with ffmpeg and Python's `wave` module.

## 2.1.2

- Audio decoding into planar float: WAV (PCM, float, RF64), AIFF/AIFC, Wave64, FLAC, MP3 and Ogg Vorbis, via vendored dr_libs and stb_vorbis. Files with more than two channels keep the first two. Files over 30 minutes are refused.
- Format detection from file contents, falling back to the file extension.
- UTF-8 file paths on Windows.
- `tools/make_fixtures.py` generates the test fixtures.

## 2.1.1

- Rack-independent `core/` library with host unit tests (`make test`, doctest, ASan/UBSan).
- CI: `Test` job ahead of the platform builds, version-bump check on PRs, skipping builds for docs-only changes, release notes from this file, Rack SDK caching.
- License: GPL-3.0-or-later.
- `plugin.json`: license, brand, URLs and module tags.
