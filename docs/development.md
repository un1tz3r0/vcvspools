# Development

## Layout

| Path | Contents |
|---|---|
| `core/` | Rack-independent C++17: decoding, analysis, DSP, sequencing |
| `src/` | Rack module and widgets |
| `tests/` | doctest unit tests for `core/` |
| `vendor/` | single-header third-party libraries |
| `tools/` | fixture generators and scripts |

## Testing

```sh
make test                          # host build with ASan/UBSan, no Rack SDK needed
make test TEST_ARGS='-tc="wrap*"'  # pass doctest options
```

Fixtures live in `tests/fixtures/` and are regenerated with `python3 tools/make_fixtures.py`, which needs numpy, plus ffmpeg for the compressed formats. Tests find them through `SPOOLS_FIXTURES`.

Host objects go to `build/host/`. The plugin build (`make RACK_DIR=…`) also compiles `core/`, with `-std=c++17`.

## Workflow

Work through `TODO.md`. Each second-level task gets tests, code, docs, a `CHANGELOG.md` entry, a patch bump in `plugin.json` matching the task number, and one commit. Each top-level milestone ends with a PR to `main`, and merging it publishes a release. CI fails a PR that changes code without raising the version.

## Vendored code

See `vendor/README.md` for the pinned versions and licenses.
