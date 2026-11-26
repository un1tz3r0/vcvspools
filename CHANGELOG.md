# Changelog

Versions follow `TODO.md`: each `2.N.p` heading is one task, and the last one in a milestone is the released version.

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
