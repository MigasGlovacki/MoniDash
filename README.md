# MoniDash

MoniDash is a local-first Geometry Dash telemetry mod intended to complement,
not replace, the existing Death Tracker mod.

**Status:** The narrow MoniDash MVP flow has been validated in the real Windows
runtime. A normal Geometry Dash exit produced a complete manifest, one
`session_ended` event, and the sequence `session_started` ->
`level_started(level_id 1)` -> `death(level_id 1)` -> `session_ended`. It also
produced `death_tracker_snapshot/1-local/{metadata,general.dt}`; SHA-256 copies
exactly matched the current Death Tracker sources. This validates the observed
single-player flow only, not all level types, duplicate or dual-player cases,
death causes, richer telemetry, analysis, networking, or PR merge status.

## Why two data sources?

Death Tracker is the read-only macro source. It provides deaths grouped by
percentage, level metadata, and session aggregates.

MoniDash is intended to provide micro-context for each death when that context
is reliably observable: game mode, mini state, gravity or reverse state, speed,
raw X position, a compatible percentage bin, and recent inputs or events.
Keeping these roles separate lets MoniDash add context without changing or
duplicating Death Tracker's records.

## Local-first privacy

The design is local-first:

- There is no network upload of telemetry.
- MoniDash will not modify Death Tracker files.
- Session data stays on the player's machine.

Any future integration with local data will be read-only with respect to Death
Tracker's files. This repository does not yet contain a collector, relay, hub,
or other networking component.

## Companion-led analysis

A future companion will analyze local records rather than having the mod
declare what caused a death. Analysis should be presented as hypotheses with
uncertainty, for example: "This attempt may be consistent with a timing issue
near the speed change" or "The available context is insufficient to infer a
cause." It must not pretend to diagnose the player, invent causes, or turn
limited telemetry into automatic verdicts.

## Initial non-goals

The initial project does not aim to provide:

- Gameplay alteration or cheats
- Cloud telemetry
- Automatic diagnosis
- A replacement for Death Tracker

## Build

Core-only mode works on Linux without the Geode SDK:

```sh
cmake -S . -B build/core
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Windows Geode package mode requires the Geode SDK source and CLI paths to be
provided explicitly. Replace both placeholders with paths on the build PC:

```sh
cmake -S . -B build/geode \
  -DMONIDASH_BUILD_GEODE_MOD=ON \
  -DMONIDASH_GEODE_SDK=<path-to-geode-sdk-5.8.2> \
  -DGEODE_CLI=<path-to-geode-cli-3.8.0>
cmake --build build/geode --config Release
```

The narrow Windows MVP flow has been runtime-validated: packaging, manifest
completion on a normal exit, the session/event sequence described above, and
the Death Tracker snapshot files are present and source-matched by SHA-256.
This is not a claim of generality across all level types or duplicate/dual-player
cases, and does not include causes, richer telemetry, analysis, networking, or
PR merge status.

## Future distribution

When MoniDash becomes installable, the intended first distribution path is
versioned GitHub Releases. A later option may be publication through the
Geode Index for in-game installation and updates; the Geode Index is not set
up for this project yet.

Each release should be immutable. Changes should be published as a new
versioned release rather than rewriting an existing release.

## Project scope

The current runtime has a narrow, Windows-validated MVP flow: a normal exit
produces a complete manifest with one `session_ended` event, the sequence
`session_started` -> `level_started(level_id 1)` -> `death(level_id 1)` ->
`session_ended`, and a read-only snapshot at
`death_tracker_snapshot/1-local/{metadata,general.dt}` whose SHA-256 copies
match the current Death Tracker sources. This scope does not claim coverage of
all level types, duplicate or dual-player cases, death causes, richer telemetry,
analysis, networking, or PR merge status.
