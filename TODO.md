# Spools — TODO

Each top-level item is a minor version (`2.N.0`) and one PR to `main`. Each second-level item is a patch bump (`2.N.p`) and one commit. Per task: tests → code → run tests → fix → update `docs/` and `CHANGELOG.md` → check off here. See `DESIGN.md` for the details behind each item.

- [x] **2.1 Foundation and import**
  - [x] 2.1.1 Scaffolding: `core/` library, C++17, vendored doctest, `make test`, CI test job, `LICENSE` (GPL-3.0-or-later), `docs/`, `CHANGELOG.md`
  - [x] 2.1.2 Audio decoding: dr_wav (WAV/AIFF/W64), dr_flac, dr_mp3, stb_vorbis → `Audio`; fixture generator `tools/make_fixtures.py`
  - [x] 2.1.3 RIFF metadata: read/write `acid`, `cue `, `LIST/adtl/labl`, `smpl`, `spls`; Acidized WAV exporter; round-trip tests
  - [x] 2.1.4 REX2 import: C++ IFF + DWOP decoder from the schwung-rex spec; slices, PPQ→beats, tempo, signature; licence/attribution; "unofficial" docs
  - [x] 2.1.5 `Loop` model: crop (beats/seconds/samples), slice map, waveform mips, worker thread, atomic publish, deferred free

- [x] **2.2 Playback proof of concept**
  - [x] 2.2.1 Time base: CLOCK + PPQN, smoothing, manual/loop BPM fallback, beat-phase accumulator
  - [x] 2.2.2 `SlicePlayer`: windowed-sinc interpolation (16/8 tap), pitch, start/end trims, reverse, declick
  - [x] 2.2.3 Note-mode sequencer: V/oct offset with wrap, region, length, phrase timing in beats, gate repeat with loop count, direction (fwd/rev/ping-pong/random), launch quantize, retrigger
  - [x] 2.2.4 Voice: two players, equal-power overlap crossfade, duration, gain/pan, 16-voice poly; SLICE/TRIG/EOC outputs
  - [x] 2.2.5 Module glue: params, inputs, outputs, file-load menu, JSON persistence, patch-storage embedding (≤5 min) with *Reference file only* option
  - [x] 2.2.6 Minimal display (waveform, slices, playheads) on a placeholder 36 HP panel

- [x] **2.3 Analysis and slicing**
  - [x] 2.3.1 STFT + spectral-flux onset envelope (in-house FFT)
  - [x] 2.3.2 Onset picking: adaptive threshold, sensitivity, minimum gap, zero-crossing refinement
  - [x] 2.3.3 Tempo estimation (autocorrelation + prior + comb), bar-fit nudge, grid phase (halve/double/tap move to 2.3.5)
  - [x] 2.3.4 Slicing modes (onsets/grid/hybrid) with grid-snap strength; re-slice on the worker when settings change
  - [x] 2.3.5 Context-menu crop and BPM text fields (plus halve/double/tap)

- [x] **2.4 Tone shaping and parameter resolution**
  - [x] 2.4.1 TPT SVF (LP/BP/HP/notch 12 dB) + 24 dB LP, V/oct cutoff
  - [x] 2.4.2 Amp and filter ADSR, bipolar amount, per-slice/per-note retrigger, AMP/FILT ENV outputs
  - [x] 2.4.3 Global + CV×attenuverter + per-slice parameter resolution, smoothing of continuous params

- [x] **2.5 Loop mode and transport**
  - [x] 2.5.1 RUN, CONT, BAR inputs; bar length; bars per cycle with automatic CONT
  - [x] 2.5.2 Loop-mode transport voice with direction/count and V/oct slice offset

- [x] **2.6 Panel and display**
  - [x] 2.6.1 Final 36 HP panel SVG and control layout
  - [x] 2.6.2 Page framework: tab row, `Page`/`DisplayContext`, Loop and Settings pages
  - [x] 2.6.3 Loop page drawing: OKLCH slice hues, grid, region, playheads, readouts, REX badge, light layer
  - [x] 2.6.4 Mouse editing: select/deselect/multi-select, drag boundaries and trims, add/delete slices, region handles, zoom/scroll, undo history
  - [x] 2.6.5 Selection editing: knob retargeting to the selection mean, relative multi-edit, shift-click = set to mean, spread ring
  - [x] 2.6.6 Crop view with handles

- [ ] **2.7 Tail modes**
  - [x] 2.7.1 Per-slice features: transient end, YIN fundamental period with confidence
  - [x] 2.7.2 Cut and Ring modes
  - [x] 2.7.3 Manual Loop mode (forward/ping-pong, crossfade) + Slice page with loop-point handles
  - [x] 2.7.4 Auto-loop point search (period multiples, coherence score, zero-crossing snap)
  - [ ] 2.7.5 Granular stretch/squeeze with transient preserve and tail %

- [ ] **2.8 Performance and polish**
  - [ ] 2.8.1 `make bench`; eco interpolation; skip idle voices; reach the CPU target
  - [ ] 2.8.2 User manual in `docs/`, panel screenshots, README update

- [ ] **2.9 Slice remap sequencer (Pattern mode)**
  - [ ] 2.9.1 Pattern model and step playback: order, length/nudge, duration, reverse, pitch
  - [ ] 2.9.2 Ratchets with rate curve and velocity ramps; pattern loop/count/direction
  - [ ] 2.9.3 Pattern page editor and menu actions; persistence in JSON and `spls`

- [ ] **2.10 Per-slice sends and CV**
  - [ ] 2.10.1 FX send A/B outputs with per-slice levels
  - [ ] 2.10.2 Per-slice CV A/B outputs

- [ ] **2.11 Resizable panel**
  - [ ] 2.11.1 NanoVG-drawn panel, resize handle, display that grows with width
