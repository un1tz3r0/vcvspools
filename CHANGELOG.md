# Changelog

Versions follow `TODO.md`: each `2.N.p` heading is one task, and the last one in a milestone is the released version.

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
