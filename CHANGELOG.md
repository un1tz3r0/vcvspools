# Changelog

Versions follow `TODO.md`: each `2.N.p` heading is one task, and the last one in a milestone is the released version.

## 2.1.1

- Rack-independent `core/` library with host unit tests (`make test`, doctest, ASan/UBSan).
- CI: `Test` job ahead of the platform builds, version-bump check on PRs, skipping builds for docs-only changes, release notes from this file, Rack SDK caching.
- License: GPL-3.0-or-later.
- `plugin.json`: license, brand, URLs and module tags.
