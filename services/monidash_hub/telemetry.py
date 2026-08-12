"""Strict validation and immutable raw storage for MoniDash JSONL sessions."""

from __future__ import annotations

import json
import math
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from hashlib import sha256
from pathlib import Path
from typing import Any

_REQUIRED = frozenset({"schema_version", "event_type", "timestamp_ms", "monotonic_seconds"})
_SCHEMA_VERSION = "2.0.0"
_EVENT_TYPES = frozenset(
    {
        "session_started",
        "session_ended",
        "attempt_started",
        "attempt_ended",
        "gameplay_event",
        "death_context",
        "copy_level_link",
        "reference_run_saved",
    }
)
_ATTEMPT_BOUND_EVENTS = frozenset(
    {"attempt_ended", "gameplay_event", "death_context", "copy_level_link", "reference_run_saved"}
)
_GAMEPLAY_EVENT_TYPES = frozenset({"input", "interaction", "player_state"})
_OUTCOMES = frozenset({"reset", "death", "completed", "abandoned"})


class TelemetryValidationError(ValueError):
    """Raised when uploaded bytes are not a completed MoniDash session."""


@dataclass(frozen=True)
class ParsedSession:
    digest: str
    raw: bytes
    events: tuple[dict[str, Any], ...]
    session_id: str


def _is_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _require_string(event: dict[str, Any], field: str, kind: str) -> str:
    value = event.get(field)
    if value is None or value == "":
        raise TelemetryValidationError(f"{kind} requires {field}")
    if not isinstance(value, str):
        raise TelemetryValidationError(f"{kind} {field} must be a string")
    return value


def _require_fields(event: dict[str, Any], fields: set[str], kind: str) -> None:
    missing = fields.difference(event)
    if missing:
        raise TelemetryValidationError(f"{kind} missing fields: {', '.join(sorted(missing))}")


def _validate_event_specific_fields(event: dict[str, Any]) -> None:
    kind = event["event_type"]
    if kind in {"session_started", "session_ended"}:
        _require_string(event, "session_id", kind)
    elif kind == "attempt_started":
        _require_string(event, "attempt_id", kind)
        _require_fields(event, {"attempt_number", "training_segment", "start_x"}, kind)
        if not isinstance(event["attempt_number"], int) or isinstance(event["attempt_number"], bool):
            raise TelemetryValidationError("attempt_started attempt_number must be an integer")
        if not isinstance(event["training_segment"], bool) or not _is_number(event["start_x"]):
            raise TelemetryValidationError("attempt_started has invalid fields")
    elif kind == "attempt_ended":
        _require_string(event, "attempt_id", kind)
        _require_fields(event, {"outcome", "start_x", "end_x", "training_segment"}, kind)
        if event["outcome"] not in _OUTCOMES or not all(_is_number(event[field]) for field in ("start_x", "end_x")) or not isinstance(event["training_segment"], bool):
            raise TelemetryValidationError("attempt_ended has invalid fields")
    elif kind in _ATTEMPT_BOUND_EVENTS:
        _require_string(event, "attempt_id", kind)
        if kind == "gameplay_event":
            gameplay = event.get("event")
            if not isinstance(gameplay, dict) or gameplay.get("event_type") not in _GAMEPLAY_EVENT_TYPES or not _is_number(gameplay.get("at")):
                raise TelemetryValidationError("gameplay_event requires a known event with finite at")
        elif kind == "copy_level_link":
            _require_fields(event, {"copy_level_id", "official_level_id", "link_status", "start_x"}, kind)
            if not _is_number(event["copy_level_id"]) or event["link_status"] != "needs_confirmation" or not _is_number(event["start_x"]):
                raise TelemetryValidationError("copy_level_link has invalid fields")
        elif kind == "reference_run_saved":
            _require_fields(event, {"reference_id", "link_status", "segment", "active_key", "active"}, kind)
            _require_string(event, "reference_id", kind)
            segment = event["segment"]
            if event["link_status"] != "needs_confirmation" or not isinstance(segment, dict) or not all(_is_number(segment.get(field)) for field in ("start_x", "end_x")) or not isinstance(event["active_key"], str) or not isinstance(event["active"], bool):
                raise TelemetryValidationError("reference_run_saved has invalid fields")
        elif kind == "death_context":
            _require_fields(event, {"player_snapshot", "fatal_object", "cause", "context_seconds", "preceding_events"}, kind)
            cause = event["cause"]
            if not isinstance(cause, dict) or cause.get("classification") not in {"spike", "block", "unknown"} or not _is_number(event["context_seconds"]) or not isinstance(event["preceding_events"], list):
                raise TelemetryValidationError("death_context has invalid fields")


def parse_session(raw: bytes) -> ParsedSession:
    """Validate a completed producer-compatible UTF-8 JSONL session and digest it."""
    if not raw or not raw.strip():
        raise TelemetryValidationError("payload must be non-empty JSONL")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise TelemetryValidationError("payload must be UTF-8") from exc

    events: list[dict[str, Any]] = []
    previous_monotonic = -1.0
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            raise TelemetryValidationError(f"blank line at {line_number}")
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            raise TelemetryValidationError(f"invalid JSON at line {line_number}") from exc
        if not isinstance(event, dict):
            raise TelemetryValidationError(f"line {line_number} must be a JSON object")
        missing = _REQUIRED.difference(event)
        if missing:
            raise TelemetryValidationError(f"missing required fields at line {line_number}: {', '.join(sorted(missing))}")
        if event["schema_version"] != _SCHEMA_VERSION:
            raise TelemetryValidationError(f"unsupported schema_version at line {line_number}")
        if event["event_type"] not in _EVENT_TYPES:
            raise TelemetryValidationError(f"unknown event_type at line {line_number}")
        if not isinstance(event["timestamp_ms"], str) or not event["timestamp_ms"].isdigit():
            raise TelemetryValidationError(f"timestamp_ms must be a decimal string at line {line_number}")
        monotonic = event["monotonic_seconds"]
        if not _is_number(monotonic) or monotonic < 0:
            raise TelemetryValidationError(f"monotonic_seconds must be finite and nonnegative at line {line_number}")
        if monotonic < previous_monotonic:
            raise TelemetryValidationError("monotonic_seconds must be nondecreasing")
        previous_monotonic = monotonic
        _validate_event_specific_fields(event)
        events.append(event)

    started = [event for event in events if event["event_type"] == "session_started"]
    ended = [event for event in events if event["event_type"] == "session_ended"]
    if len(started) != 1:
        raise TelemetryValidationError("requires exactly one session_started")
    if len(ended) != 1:
        raise TelemetryValidationError("requires exactly one session_ended")
    if events[0]["event_type"] != "session_started":
        raise TelemetryValidationError("session_started must be the first event")
    if events[-1]["event_type"] != "session_ended":
        raise TelemetryValidationError("session_ended must be the final event")

    session_id = _require_string(started[0], "session_id", "session_started")
    for event in events:
        value = event.get("session_id")
        if value is not None and value != session_id:
            raise TelemetryValidationError("mismatched session IDs")

    active_attempt: str | None = None
    seen_attempts: set[str] = set()
    for event in events:
        kind = event["event_type"]
        if kind == "attempt_started":
            attempt_id = event["attempt_id"]
            if active_attempt is not None:
                raise TelemetryValidationError("attempt_started while another attempt is already active")
            if attempt_id in seen_attempts:
                raise TelemetryValidationError("attempt_id may not be reused")
            active_attempt = attempt_id
            seen_attempts.add(attempt_id)
        elif kind == "attempt_ended":
            if active_attempt != event["attempt_id"]:
                raise TelemetryValidationError("attempt_ended references an attempt that is not active")
            active_attempt = None
        elif kind in _ATTEMPT_BOUND_EVENTS:
            if active_attempt != event["attempt_id"]:
                raise TelemetryValidationError(f"{kind} references an attempt that is not active")
    if active_attempt is not None:
        raise TelemetryValidationError("session ended with an active attempt")

    return ParsedSession(sha256(raw).hexdigest(), raw, tuple(events), session_id)


def store_raw(raw_directory: Path, session: ParsedSession) -> tuple[Path, bool]:
    """Create the digest-addressed raw JSONL once, without changing an existing file."""
    raw_directory.mkdir(parents=True, exist_ok=True)
    target = raw_directory / f"{session.digest}.jsonl"
    try:
        with target.open("xb") as output:
            output.write(session.raw)
        return target, True
    except FileExistsError:
        if target.read_bytes() != session.raw:
            raise RuntimeError("digest collision or corrupt raw session")
        return target, False


def _utc_date(timestamp_ms: object) -> str:
    try:
        return datetime.fromtimestamp(int(timestamp_ms) / 1000, tz=timezone.utc).strftime("%Y-%m-%d")
    except (TypeError, ValueError, OSError):
        return "unknown-date"


def _sanitize_name(name: str) -> str:
    cleaned = "".join("_" if character in '<>:"/\\|?*' or ord(character) < 32 else character for character in name)
    cleaned = cleaned.strip(" .")
    return cleaned[:80] or "unnamed"


# Training copies saved in the editor follow João's convention: the official
# level name plus a trailing "SP" / "Start Position" marker. Those sessions are
# grouped under the official level's folder so the copy and the original live
# side by side in the organized view.
_TRAINING_COPY_SUFFIX = re.compile(r"\s+(?:SP|Start\s*Position|StartPosition|StartPos)\s*$", re.IGNORECASE)


def _official_level_name(name: str) -> str:
    cleaned = _sanitize_name(name)
    stripped = _TRAINING_COPY_SUFFIX.sub("", cleaned).strip()
    return stripped or cleaned


def organize_session(raw_directory: Path, session: ParsedSession) -> Path | None:
    """Write an organized copy at sessions/<local_date>/<level_name>/<digest>.jsonl.

    The raw digest-addressed file remains the immutable source of truth; this
    copy is a human-friendly view grouped by the player's local date and the
    level name. Training copies named "<level> SP"/"<level> Start Position"
    are grouped under the official level name. Missing metadata falls back to
    UTC date and level-<id>.
    """
    started = next((event for event in session.events if event["event_type"] == "session_started"), None)
    if started is None:
        return None
    level = started.get("level") or {}
    local_date = str(started.get("local_date") or _utc_date(started.get("timestamp_ms")))
    level_name = _official_level_name(str(level.get("name") or "") or f"level-{level.get('id', '?')}")
    target = raw_directory.parent / "sessions" / local_date / level_name / f"{session.digest}.jsonl"
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as output:
            output.write(session.raw)
    except FileExistsError:
        pass
    return target
