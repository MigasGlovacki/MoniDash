# MoniDash MVP Core: First Implementation Run

## Goal

Build the first testable core of MoniDash, a local-first Geometry Dash/Geode
telemetry collector. It is a collector, not a gameplay mod, cheat, cloud
service, hub, relay, or replacement for Death Tracker.

The first run must establish a session when the game/process starts, append
MoniDash events incrementally as they become observable, and seal MoniDash's
own session state on clean shutdown. It must persist JSONL events plus a
manifest with an explicit schema version and status, finalize atomically, and
make the next launch able to detect sessions left incomplete by a crash or
forced termination.

The per-death event target is:

- Session ID and event/death timestamps.
- Level identity.
- Attempt/order when observable.
- Raw X and a compatible percentage bin.
- Gamemode, mini state, gravity/reverse state, and speed.
- Practice/normal state and dual state when safely observable.
- Recent/last input only when actually observable; otherwise omit it rather
  than infer it.

Death Tracker export/snapshot integration is explicitly deferred until Joao is
awake and his Windows PC and Geometry Dash are available. Do not design an
online service as a substitute.

## Current Repository Context

- The repository is `/opt/data/MoniDash` on branch `main`, tracking
  `origin/main`.
- The only commit is `78573b8 docs: establish MoniDash project foundation`.
- Current tracked project files are `README.md` and `.gitignore`.
- `README.md` describes a local-only telemetry mod, no networking, no writes to
  Death Tracker files, and a documentation-only foundation.
- There is currently no C++ source, test suite, CMake project, `mod.json`, CI,
  Geode SDK checkout, or existing `.hermes` plan.
- `.gitignore` already excludes local telemetry/session data, secrets, CMake
  artifacts, and `*.geode`; do not modify it for this run.
- Linux VPS development is the starting environment. A Windows `.geode` build
  is not assumed or promised.

## Assumptions and Explicit Unknowns

### Assumptions

- The first implementation can deliver and test durable session/schema logic
  without a live Geometry Dash process.
- Events can represent unavailable observations as omitted optional fields, with
  a clear distinction between absent and known false/zero.
- JSONL is the append format; a manifest is separate metadata and the final
  state is written atomically using the platform mechanism confirmed during
  implementation.
- The collector owns only its MoniDash session directory and never mutates
  Death Tracker data.
- The first run should prefer a narrow, honest event contract over guessed
  telemetry fields or synthetic gameplay data.

### Unknowns to resolve before hook implementation

- Which Geode SDK version, Cocos2d/Geometry Dash version, compiler, generator,
  and platform targets are installed or available on the VPS.
- Whether the repository has a usable official Geode template locally, and the
  exact current Geode mod entry-point, lifecycle, event, level-state, player,
  input, and shutdown APIs.
- Which process/game lifecycle callback reliably means "session start" and
  which callback is safe for clean sealing.
- How raw X, percentage, gamemode, mini, gravity/reverse, speed,
  practice/normal, dual, attempt order, death, and input are exposed in the
  verified target APIs and in which game states they are valid.
- Whether the target filesystem and C++ standard provide the required atomic
  rename/replace semantics and how interruptions behave on Windows.
- The exact desired on-disk root and level identity fields, pending product
  confirmation. Use a configurable/default local root only after the SDK/build
  discovery confirms the mod's supported path APIs.
- Whether a safe input observation exists at all. If not, the MVP must omit
  input rather than reconstruct it.
- The real Death Tracker export/snapshot shape. No adapter should be started
  until Joao's Windows PC and Geometry Dash are available.

## Proposed Architecture

Keep the platform-independent core separate from Geode-specific hooks:

1. `session` owns session IDs, lifecycle state, event sequencing, append-only
   JSONL writing, manifest transitions, incomplete-session recovery detection,
   and atomic finalization. It accepts plain observations and does not include
   Geode types.
2. `schema` defines the versioned event and manifest shapes, required fields,
   optional observable fields, omission rules, and serialization/parsing. It
   must reject malformed or contradictory records without silently inventing
   values.
3. `adapters/geode` is a thin translation boundary. Verified Geode callbacks
   produce observations, and only observations supported by official docs,
   template code, and compilation are forwarded to `session`.
4. `storage` is a small filesystem boundary used by `session`; production
   storage performs append, flush/close, temporary-file write, and atomic
   replace. The core tests use an in-memory or temporary-directory fake.
5. The on-disk layout should be finalized during implementation only after
   path/API discovery, but must have one directory per session, an append-only
   `events.jsonl`, and a `manifest.json`. A new session starts as `active`; a
   clean shutdown writes `complete` through an atomic finalization step. A
   launch scan reports prior `active` sessions as `incomplete` without rewriting
   their event history.

Candidate event boundaries:

- Session lifecycle events: `session_started`, `session_ended`.
- Telemetry events: `level_started`, `checkpoint` only if genuinely needed,
  and `death` for each observed death. Do not add a guessed event taxonomy.
- Every event has schema version, session ID, event ID/sequence, and an
  unambiguous timestamp. Death fields are optional unless the observation is
  verified; an explicit observability marker may be used where it improves
  downstream interpretation.
- Compatible percentage bin must document its coordinate/source convention and
  must not pretend raw X is a percentage. If conversion cannot be verified for
  the active level, omit the bin.

## Exact Candidate File Paths

These are implementation targets, not files to create during planning. The
worker must adjust only after the discovery gate confirms the official template
layout and naming conventions.

- `CMakeLists.txt`: candidate top-level build entry once the verified Geode
  template dictates its required contents. Do not invent this file now.
- `mod.json`: candidate Geode metadata path once the official template and
  required identifiers are confirmed. Do not invent metadata now.
- `src/core/schema.hpp`: versioned domain types and validation declarations.
- `src/core/schema.cpp`: validation and JSON serialization/parsing.
- `src/core/session_store.hpp`: storage/lifecycle interfaces independent of
  Geode.
- `src/core/session_store.cpp`: JSONL append, manifest writes, recovery scan,
  and atomic finalization implementation.
- `src/core/observations.hpp`: plain observable telemetry input types and
  omission semantics.
- `src/core/monidash_core.hpp`: session orchestration API used by adapters and
  tests.
- `src/core/monidash_core.cpp`: start, append, death recording, and clean
  shutdown orchestration.
- `src/platform/geode_hooks.hpp`: narrow declarations for verified hook glue.
- `src/platform/geode_hooks.cpp`: only documented/compiled Geode integration;
  no guessed symbols or hooks.
- `tests/unit/schema_tests.cpp`: schema validation and serialization tests.
- `tests/unit/session_store_tests.cpp`: append, manifest, atomicity, and
  recovery tests.
- `tests/unit/monidash_core_tests.cpp`: vertical lifecycle and death-recording
  tests using fake observations/storage.
- `tests/fixtures/session_active/events.jsonl`: deterministic incomplete-session
  fixture, if file fixtures are needed by the verified test framework.
- `tests/fixtures/session_complete/manifest.json`: deterministic completed
  session fixture.
- `tests/fixtures/events/death_minimal.json`: minimum honest death event.
- `tests/fixtures/events/death_full_observable.json`: all fields only when the
  adapter can genuinely observe them.
- `tests/fixtures/events/death_missing_optional.json`: explicit omission cases.

If the official Geode template requires different source paths, preserve the
separation and record the actual replacement paths in the implementation PR.

## Vertical TDD Tasks

Work in this order. Each slice must have a failing test first, the smallest
implementation that passes it, and a focused commit.

### 0. Discovery gate

- Inspect official Geode documentation and an official/current Geode template
  before writing any hook code.
- Discover installed SDK, toolchain, CMake/generator, compiler, and available
  target configuration on Linux. Record commands and actual results in the PR.
- Verify the template's metadata, entry point, lifecycle, logging, filesystem,
  and test conventions. If unavailable, stop hook work and continue only with
  explicitly buildable platform-independent core code.
- Write a short discovery note in the PR description or an implementation
  note, not in `README.md`, including unknown APIs that remain unresolved.

### 1. Schema contract

- Red test: valid minimal lifecycle and death records serialize and round-trip;
  invalid schema version, missing identity, non-monotonic sequence, and invalid
  timestamp are rejected.
- Implement `schema.hpp/.cpp` with stable field names, schema version, required
  identity/lifecycle fields, and optional observability fields.
- Red/green test: omitted optional values remain omitted after round-trip;
  known `false`, `0`, and empty-but-observed values are not confused with
  absence.
- Red/green test: a compatible percentage bin is accepted only under its
  documented convention and is absent when conversion is unavailable.

### 2. Append-only session storage

- Red test: starting a session creates an active manifest and appends a
  `session_started` event without truncating existing sessions.
- Implement the storage seam and JSONL append path with sequence assignment,
  flush/error propagation, and manifest schema version/status.
- Red test: each appended event is independently parseable and a partial final
  line is detected/reported rather than treated as a valid death.
- Red test: clean shutdown writes/finalizes `session_ended`, closes the event
  stream, and atomically publishes a `complete` manifest.
- Red test: injected write/rename failure leaves no falsely `complete` session.

### 3. Recovery detection

- Red test: a prior `active` manifest is found on the next launch and surfaced
  as incomplete; its JSONL remains untouched.
- Implement launch recovery scan and a deterministic result for corrupt,
  missing, or unreadable manifests. Do not claim recovery can reconstruct
  missing telemetry.
- Red test: completed sessions are ignored by the incomplete scan and a second
  finalization is rejected or safely idempotent according to the chosen
  contract.

### 4. Core lifecycle and death recording

- Red test: core start creates one session ID and timestamps/events in order.
- Implement `monidash_core` against the platform-independent observation and
  storage interfaces.
- Red test: a death with only safely observable fields persists all required
  identity/timing fields and omits unavailable fields.
- Red test: a fully observable synthetic observation persists each target field
  with no type/meaning changes.
- Red test: repeated deaths preserve order and do not overwrite prior events;
  attempt/order is included only when supplied by the adapter.
- Red test: clean shutdown seals the core once; abnormal termination is visible
  as an active session for next-launch detection.

### 5. Geode adapter, only after discovery

- Add adapter tests around a fake/documented callback boundary first.
- Implement only hooks and type conversions confirmed by official docs/template
  inspection and compiler errors/results. Never guess class names, event names,
  offsets, or memory addresses.
- Add one integration-facing test for each verified observable field and one
  test proving unavailable input or state is omitted.
- Keep Death Tracker integration out of this slice. Do not add network code,
  relay code, or a substitute export reader.

## Test and Fixture Strategy

- Prefer deterministic C++ unit tests for `schema`, `session_store`, and
  `monidash_core`; they must run on Linux without Geometry Dash or Geode.
- Use an injected clock, UUID/session-ID generator, filesystem root, and failure
  injector so tests do not depend on wall-clock timing, random IDs, or the VPS
  filesystem.
- Use temporary directories for atomicity/recovery behavior and clean them up;
  use JSON fixture files only for stable round-trip examples and corruption
  cases.
- Fixtures must include minimal, full-observable, missing-optional, incomplete,
  completed, truncated-last-line, invalid-version, and invalid-sequence cases.
- Assert both semantic values and serialized structure: one JSON object per
  line, no partial successful finalization, correct manifest status, schema
  version, session ID, and monotonic sequence.
- Never use real telemetry, Death Tracker exports, credentials, personal paths,
  or generated `.geode`/session output as committed fixtures. Keep generated
  telemetry under already ignored locations.
- If no test framework is present, select the framework required by the
  verified official template rather than adding an arbitrary dependency before
  toolchain discovery.

## Build Validation Ladder

Run the lowest applicable rung first and report exact commands and outcomes.

1. Repository hygiene: inspect `git diff`, `git status --short`, and confirm no
   generated telemetry, secrets, `README.md`, `.gitignore`, or unapproved
   metadata changes are present.
2. Core configure/build: use the discovered compiler/build system to compile
   the platform-independent core and tests on Linux.
3. Core tests: run schema, storage, recovery, and lifecycle tests; include
   failure-injection and fixture cases.
4. Static quality: run the template-supported formatter, compiler warnings,
   and static analysis if actually available; do not fabricate tool results.
5. Geode configure/build: only if the installed/discovered SDK and official
   template support it. Record SDK/toolchain versions and the exact command.
6. Geode package/install smoke check: only if a verified target environment or
   documented packaging path exists. A Linux core build is not evidence of a
   Windows `.geode` build.
7. Windows/Geometry Dash runtime validation: deferred until Joao's Windows PC
   and Geometry Dash are available. Validate real hooks, lifecycle, death data,
   and shutdown there; report this rung as not run rather than passing it.

## Git/PR Workflow

- Before edits, inspect status and branch; create a feature branch from the
  current `main`, using a descriptive name such as
  `feat/monidash-mvp-core`.
- Make small commits in vertical order: discovery/core contract, schema,
  storage/recovery, core lifecycle, then verified adapter. Keep tests with the
  behavior they protect. Do not commit generated telemetry, local paths,
  secrets, SDK downloads, build directories, or `.geode` artifacts.
- Before each commit, inspect `git diff` and `git status`; stage only intended
  files. Never modify `README.md` or `.gitignore` for this implementation run.
- Push the feature branch to the configured GitHub remote and open a GitHub PR
  against `main`. Do not push directly to `main`.
- PR description must state scope/non-goals, discovery findings, exact build and
  test commands/results, unrun Windows/runtime validation, and any unresolved
  observability fields. Link no fabricated Death Tracker integration.
- Wait for or report CI/PR checks if configured. The worker must return the
  branch name, commit list, PR URL, and actual results; if remote access is not
  available, stop after local validation and explicitly report the blocker.

## Risks and Stop Conditions

- Stop hook work immediately if official Geode docs/template or the installed
  SDK cannot establish the correct API. Continue only with isolated core work
  and label the adapter unimplemented.
- Stop and ask for clarification if the target Geode/game version, storage
  location, schema ownership, or event semantics materially conflict with this
  plan; do not silently choose a compatibility story.
- Do not claim a Windows `.geode` artifact from Linux unless the exact SDK and
  cross-toolchain build has been discovered, configured, and verified. A
  package-looking file is not proof of compatibility.
- Stop on any data-integrity test failure involving append order, partial lines,
  manifest status, atomic finalization, or incomplete-session detection.
- Stop adding a field when it requires inference, memory offsets, undocumented
  APIs, or gameplay modification. Omit it and record it as unknown.
- Stop Death Tracker work until Joao's Windows PC and Geometry Dash are
  available; do not build an online or relay substitute.
- Stop before commit/push if secrets, generated telemetry, build artifacts, or
  unrelated files appear in the diff.
- If a clean shutdown callback cannot be verified, preserve active-session
  semantics and report clean sealing as pending rather than falsely marking
  sessions complete.

## First-Run Acceptance Checklist

- [ ] Official Geode docs/template and installed Linux SDK/toolchain discovery
      were completed and recorded, or hook work was explicitly stopped.
- [ ] Platform-independent schema/session tests pass on Linux.
- [ ] JSONL events append incrementally with versioned manifests and ordered
      events.
- [ ] Clean finalization is atomic and never falsely reports `complete` after a
      forced write/rename failure.
- [ ] Next launch detects prior active sessions as incomplete without rewriting
      their event history.
- [ ] Death records include only verified observations; unavailable fields are
      omitted, not inferred.
- [ ] No Death Tracker export/snapshot or online substitute was implemented.
- [ ] No gameplay alteration, cheat, cloud service, hub, or relay was added.
- [ ] No generated telemetry, secrets, `README.md`, `.gitignore`, unverified
      CMake/mod metadata, or unverified Windows `.geode` claim entered the PR.
- [ ] Feature branch has small focused commits, is pushed, and has a GitHub PR
      with actual build/test results and explicit unrun-platform notes.
