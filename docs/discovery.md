# Implementation Discovery

The Linux discovery environment has CMake 3.31.6 and g++ 14.2.0, but no Geode
CLI, SDK checkout, official template, or target toolchain. This historical
discovery did not establish verified hook, lifecycle, filesystem, or metadata
names on Linux.

Subsequent Windows validation established a narrow local MVP covering session
start, `level_started`, minimal player death, normal-exit `session_ended`, and
a read-only exact-key conditional snapshot of Death Tracker metadata/general.dt.
That validation covers only the observed flow. It does not claim all level
types, duplicate or dual play, causes or richer telemetry, analysis, networking,
releases, or PR merging.

Manifest publication writes a temporary file and uses C++ filesystem rename for
replacement. On Linux this maps to the supported atomic replacement behavior;
equivalent Windows semantics are intentionally not claimed without validating
the target toolchain and runtime.
