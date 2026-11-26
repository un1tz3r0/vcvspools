# Spools

Spools is a VCV Rack 2 loop player and slicer, in development. See
[DESIGN.md](DESIGN.md) for the plan, [TODO.md](TODO.md) for progress,
[docs/](docs/) for the manual and development notes, and
[CHANGELOG.md](CHANGELOG.md). Licensed GPL-3.0-or-later.

## Building

Download and unpack the [Rack 2 SDK](https://vcvrack.com/manual/Building)
for your platform. Install a C++ compiler, GNU Make, `jq`, GNU tar and `zstd`,
then build and package the plugin:

```sh
make RACK_DIR=/path/to/Rack-SDK
make dist RACK_DIR=/path/to/Rack-SDK
```

The package is written to `dist/`. Run the core unit tests with `make test`
(no SDK required). CI builds Linux x64, Windows x64, macOS x64,
and macOS ARM64 with the Rack 2.6.6 SDK. On Windows use the MSYS2 MinGW64
toolchain; on macOS ensure GNU tar is on `PATH`.

## Contributions and releases

Work on a new branch and open a pull request against `main`. Bump the
`plugin.json` version for every change requiring a plugin rebuild. Versions
must increase and use the Rack 2 format `2.minor.patch`. After all builds pass
on `main`, a version increase creates a `v<version>` tag and a GitHub release
with all four packages and notes taken from `CHANGELOG.md`. Documentation-only
changes do not need a version bump, and CI skips the builds for them. CI fails
a pull request that changes code without raising the version.

## Repository settings

Branch protection is a GitHub repository setting, not a file in this project.
An administrator must configure a rule or ruleset for `main` to block force
pushes (including administrator bypass), require pull requests, and preferably
require the build checks to pass. Verify the rule is active in Settings →
Branches or Settings → Rules → Rulesets; adding these instructions alone does
not enable protection.
