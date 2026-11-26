# Development

## Layout

| Path | Contents |
|---|---|
| `core/` | Rack-independent C++17: decoding, analysis, DSP, sequencing |
| `src/` | Rack module and widgets |
| `tests/` | doctest unit tests for `core/` |
| `vendor/` | single-header third-party libraries |
| `tools/` | fixture generators and scripts |

## Core concepts

- `Audio`: planar float at the file's native rate. `Loop`: the source, a crop, a cropped copy for playback, slices with beat positions, the region, and waveform mips. A `Loop` is immutable once published. An edit builds a new one (`recrop`, `sliceEvenly`, …).
- `Exchange<Loop>`: the worker publishes and the audio thread `acquire()`s without locking or allocating. Old loops come back through a lock-free queue and are freed on the worker's next `publish`/`collect`.
- `Worker`: one background thread per module for decoding, analysis and file I/O.

## Testing

```sh
make test                          # host build with ASan/UBSan, no Rack SDK needed
make test TEST_ARGS='-tc="wrap*"'  # pass doctest options
```

Fixtures live in `tests/fixtures/` and are regenerated with `python3 tools/make_fixtures.py`, which needs numpy, plus ffmpeg for the compressed formats. Tests find them through `SPOOLS_FIXTURES`.

Host objects go to `build/host/`. The plugin build (`make RACK_DIR=…`) also compiles `core/`, with `-std=c++17`.

## Benchmarks

```sh
make bench               # 16 voices at 48 kHz per playback mode, best of three 4 s runs
build/bench/bench 1 Stretch  # 1 s runs, only cases whose name contains "Stretch"
```

It builds `core/` with Rack's flags (`-O3 -march=nehalem -funsafe-math-optimizations`) and reports % of one core in real time and ns per voice-sample. The target is under 3 % for 16 voices on a modern x86 (DESIGN §11). On the 2.1 GHz Xeon cloud VM used for development (noisy, ±15 %):

| Case | % of one core | ns / voice-sample |
|---|---|---|
| Ring, best | 3.2 | 42 |
| Ring, eco | 3.0 | 39 |
| Ring, best, LP24 | 3.8 | 49 |
| Auto-loop, best | 3.3 | 43 |
| Stretch, best | 4.5–5.1 | 58–67 |
| Stretch, eco | 3.8 | 49 |
| Idle (no gate) | 1.2 | 15 |

What keeps it there: one sinc kernel lookup per frame for both channels, the kernel's cutoff band cached per rate, fixed-size dot products the compiler vectorises, envelope and glide coefficients computed only when their inputs change, the filter re-tuned only when its cutoff, resonance or type moves, a Hann table for grains, and voices that have been silent for 0.1 s skipping everything but their envelopes.

## Workflow

Work through `TODO.md`. Each second-level task gets tests, code, docs, a `CHANGELOG.md` entry, a patch bump in `plugin.json` matching the task number, and one commit. Each top-level milestone ends with a PR to `main`, and merging it publishes a release. CI fails a PR that changes code without raising the version.

## Vendored code

See `vendor/README.md` for the pinned versions and licenses.
