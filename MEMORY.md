# Development workflow

- Make changes on a new branch and submit them through a pull request; do not commit directly to `main`.
- Bump the `plugin.json` version for every change that requires rebuilding the plugin. Use increasing `2.minor.patch` versions for Rack 2.
- Merging a version bump into `main` builds all supported SDK targets and publishes a `v<version>` tag and GitHub release.
