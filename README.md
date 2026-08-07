# MoniDash

MoniDash is a local-first Geometry Dash telemetry mod intended to complement,
not replace, the existing Death Tracker mod.

**Status:** The real Windows mod runtime has validated one `level_started` event
and exactly one `playerDestroyed` death event for one real death. Exit
finalization and the Death Tracker snapshot remain unvalidated. The death
candidate must not yet be treated as a solution to duplicate death callbacks.

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

Windows packaging, one level-start event, and one player-lifecycle death event
have been validated on the target. Exit finalization and the Death Tracker
snapshot still require Windows runtime validation.

## Future distribution

When MoniDash becomes installable, the intended first distribution path is
versioned GitHub Releases. A later option may be publication through the
Geode Index for in-game installation and updates; the Geode Index is not set
up for this project yet.

Each release should be immutable. Changes should be published as a new
versioned release rather than rewriting an existing release.

## Project scope

The current runtime observes level starts locally, includes a player-lifecycle
death candidate, and is intended to take a read-only exit snapshot of selected
Death Tracker files. Exit finalization and snapshot runtime remain unvalidated.
Networking, a relay, a hub, and generated user telemetry are not part of this
slice.
