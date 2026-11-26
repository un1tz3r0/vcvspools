# Development workflow

- Make changes on a new branch and submit them through a pull request; do not commit directly to `main`.
- Bump the `plugin.json` version for every change that requires rebuilding the plugin. Use increasing `2.minor.patch` versions for Rack 2.
- Merging a version bump into `main` builds all supported SDK targets and publishes a `v<version>` tag and GitHub release.
- Work through `TODO.md` in order. Each second-level task gets tests, code, docs/ and CHANGELOG.md updates, a version matching its task number, and one commit. Each top-level milestone ends with a PR to `main`.
- UI text must be legible at 100% zoom (see DESIGN.md §8).
