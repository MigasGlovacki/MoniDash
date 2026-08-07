# MoniDash

MoniDash is a local-first Geometry Dash telemetry mod intended to complement,
not replace, the existing Death Tracker mod.

**Status:** Early design. MoniDash is not installable yet.

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

## Future distribution

When MoniDash becomes installable, the intended first distribution path is
versioned GitHub Releases. A later option may be publication through the
Geode Index for in-game installation and updates; the Geode Index is not set
up for this project yet.

Each release should be immutable. Changes should be published as a new
versioned release rather than rewriting an existing release.

## Project scope

This repository currently contains documentation only. C++ code, CMake files,
`mod.json`, CI, releases, networking, a relay, a hub, and telemetry collection
are intentionally not part of this foundation.
