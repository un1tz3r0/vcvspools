# Changelog

Versions follow `TODO.md`: each `2.N.p` heading is one task, and the last one in a milestone is the released version.

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
