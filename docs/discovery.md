# Implementation Discovery

The Linux discovery environment has CMake 3.31.6 and g++ 14.2.0, but no Geode
CLI, SDK checkout, official template, or target toolchain. Official Geode API
and template details therefore cannot establish verified hook, lifecycle,
filesystem, or metadata names here. This run intentionally stops at the
platform-independent C++20 session/schema core. No `mod.json`, Geode CMake
configuration, hooks, gameplay code, networking, or Death Tracker integration
is included; real adapter work remains deferred until a verified target
environment is available.

Manifest publication writes a temporary file and uses C++ filesystem rename for
replacement. On Linux this maps to the supported atomic replacement behavior;
equivalent Windows semantics are intentionally not claimed without validating
the target toolchain and runtime.
