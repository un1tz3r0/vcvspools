# Changelog

Versions follow `TODO.md`: each `2.N.p` heading is one task, and the last one in a milestone is the released version.

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
