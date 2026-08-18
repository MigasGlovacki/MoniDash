import json
from pathlib import Path

import pytest
from monidash_hub.telemetry import TelemetryValidationError, organize_session, parse_session, store_raw

FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-session.jsonl"


def test_valid_completed_fixture_parses_and_has_digest():
    session = parse_session(FIXTURE.read_bytes())

    assert session.session_id == "session-test"
    assert len(session.events) == 7
    assert len(session.digest) == 64


@pytest.mark.parametrize(
    ("event", "message"),
    [
        ({"schema_version": "2", "event_type": "session_started", "timestamp_ms": "1", "monotonic_seconds": 0, "session_id": "x"}, "schema_version"),
        ({"schema_version": "2.0.0", "event_type": "invented", "timestamp_ms": "1", "monotonic_seconds": 0}, "unknown event_type"),
        ({"schema_version": "2.0.0", "event_type": "session_started", "timestamp_ms": 1, "monotonic_seconds": 0, "session_id": "x"}, "timestamp_ms"),
        ({"schema_version": "2.0.0", "event_type": "session_started", "timestamp_ms": "1", "monotonic_seconds": float("nan"), "session_id": "x"}, "monotonic_seconds"),
    ],
)
def test_rejects_wrong_schema_unknown_event_and_invalid_time_types(event, message):
    payload = (json.dumps(event, allow_nan=True) + "\n").encode()
    with pytest.raises(TelemetryValidationError, match=message):
        parse_session(payload)


@pytest.mark.parametrize(
    ("payload", "message"),
    [
        (b'{"schema_version":"2.0.0","event_type":"session_started"}\n', "missing required fields"),
        (b"not json\n", "invalid JSON"),
        (b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x"}\n', "session_ended"),
    ],
)
def test_rejects_invalid_jsonl(payload, message):
    with pytest.raises(TelemetryValidationError, match=message):
        parse_session(payload)


def test_requires_exactly_one_started_and_final_ended():
    base = b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x"}\n'
    ended = b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"2","monotonic_seconds":1,"session_id":"x"}\n'

    with pytest.raises(TelemetryValidationError, match="exactly one session_started"):
        parse_session(base + base + ended)
    with pytest.raises(TelemetryValidationError, match="unknown event_type"):
        parse_session(base + ended + b'{"schema_version":"2.0.0","event_type":"input","timestamp_ms":"3","monotonic_seconds":2}\n')


def test_rejects_mismatched_session_ids():
    payload = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"a"}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"2","monotonic_seconds":1,"session_id":"b"}\n'
    )
    with pytest.raises(TelemetryValidationError, match="session IDs"):
        parse_session(payload)


def _session_with(event_line: bytes) -> bytes:
    return (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x"}\n'
        + event_line
        + b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"2","monotonic_seconds":1,"session_id":"x"}\n'
    )


def test_attempt_events_require_non_empty_attempt_id():
    started = b'{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.1}\n'
    ended = b'{"schema_version":"2.0.0","event_type":"attempt_ended","timestamp_ms":"2","monotonic_seconds":0.2}\n'
    empty = b'{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":""}\n'

    with pytest.raises(TelemetryValidationError, match="attempt_started requires attempt_id"):
        parse_session(_session_with(started))
    with pytest.raises(TelemetryValidationError, match="attempt_ended requires attempt_id"):
        parse_session(_session_with(ended))
    with pytest.raises(TelemetryValidationError, match="attempt_id"):
        parse_session(_session_with(empty))


def test_attempt_id_must_be_a_string():
    numeric = b'{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":3}\n'
    with pytest.raises(TelemetryValidationError, match="attempt_id must be a string"):
        parse_session(_session_with(numeric))


@pytest.mark.parametrize(
    ("middle", "message"),
    [
        ('{"schema_version":"2.0.0","event_type":"attempt_ended","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":"a","outcome":"death","start_x":0,"end_x":1,"training_segment":false}', "not active"),
        ('{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":"a","attempt_number":1,"training_segment":false,"start_x":0}\n{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"2","monotonic_seconds":0.2,"attempt_id":"b","attempt_number":2,"training_segment":false,"start_x":0}', "already active"),
        ('{"schema_version":"2.0.0","event_type":"gameplay_event","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":"missing","event":{"event_type":"input","at":0.1,"button":1,"player":1,"pressed":true}}', "not active"),
    ],
)
def test_attempt_references_follow_a_single_active_attempt(middle, message):
    payload = (
        '{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"0","monotonic_seconds":0,"session_id":"x"}\n'
        + middle
        + '\n{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"3","monotonic_seconds":1,"session_id":"x"}\n'
    ).encode()
    with pytest.raises(TelemetryValidationError, match=message):
        parse_session(payload)


def test_rejects_monotonic_time_regression_and_missing_event_specific_fields():
    regression = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"0","monotonic_seconds":1,"session_id":"x"}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x"}\n'
    )
    missing_event = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"0","monotonic_seconds":0,"session_id":"x"}\n'
        b'{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":"a","attempt_number":1,"training_segment":false,"start_x":0}\n'
        b'{"schema_version":"2.0.0","event_type":"gameplay_event","timestamp_ms":"2","monotonic_seconds":0.2,"attempt_id":"a","event":{"event_type":"input"}}\n'
        b'{"schema_version":"2.0.0","event_type":"attempt_ended","timestamp_ms":"3","monotonic_seconds":0.3,"attempt_id":"a","outcome":"death","start_x":0,"end_x":1,"training_segment":false}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"4","monotonic_seconds":0.4,"session_id":"x"}\n'
    )
    with pytest.raises(TelemetryValidationError, match="nondecreasing"):
        parse_session(regression)
    with pytest.raises(TelemetryValidationError, match="gameplay_event"):
        parse_session(missing_event)


def test_raw_storage_is_digest_named_and_never_overwrites(tmp_path):
    payload = FIXTURE.read_bytes()
    session = parse_session(payload)

    path, created = store_raw(tmp_path, session)
    again, created_again = store_raw(tmp_path, session)

    assert path == again
    assert path.name == f"{session.digest}.jsonl"
    assert path.read_bytes() == payload
    assert created is True
    assert created_again is False


def test_organize_session_groups_by_local_date_and_level_name(tmp_path):
    payload = FIXTURE.read_bytes()
    session = parse_session(payload)

    organized = organize_session(tmp_path, session)

    assert organized is not None
    assert organized == tmp_path.parent / "sessions" / "2026-08-11" / "Test" / f"{session.digest}.jsonl"
    assert organized.read_bytes() == payload


def test_organize_session_falls_back_to_utc_date_and_level_id(tmp_path):
    raw = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1786494431283","monotonic_seconds":0,"session_id":"x","level":{"id":21,"name":""}}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"1786494431284","monotonic_seconds":1,"session_id":"x"}\n'
    )
    session = parse_session(raw)

    organized = organize_session(tmp_path, session)

    # 1786494431283 ms = 2026-08-12 UTC (the player's local date may differ,
    # which is why the recorder also sends local_date); empty name -> level-21
    assert organized is not None
    assert "2026-08-12" in organized.parts
    assert "level-21" in organized.parts


def test_organize_session_sanitizes_unsafe_level_names(tmp_path):
    raw = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x","local_date":"2026-08-11","level":{"id":5,"name":"a/b\\\\c:d*e?f\\"g<h>i|j"}}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"2","monotonic_seconds":1,"session_id":"x"}\n'
    )
    session = parse_session(raw)

    organized = organize_session(tmp_path, session)

    assert organized is not None
    assert all(character not in organized.name for character in '<>:"/\\|?*')
    assert organized.parent.name == "a_b_c_d_e_f_g_h_i_j"


@pytest.mark.parametrize(
    ("raw_name", "expected_folder"),
    [
        ("Stereo Madness SP", "Stereo Madness"),
        ("Stereo Madness Start Position", "Stereo Madness"),
        ("Nightmare StartPosition", "Nightmare"),
        ("X startpos", "X"),
        ("NoSuffixHere", "NoSuffixHere"),
        ("SP", "SP"),
        ("SP Only", "SP Only"),
        ("Stereo Madness sp ", "Stereo Madness"),
    ],
)
def test_training_copy_suffix_groups_with_official_level(tmp_path, raw_name, expected_folder):
    name_json = json.dumps(raw_name, ensure_ascii=False)
    raw = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x","local_date":"2026-08-11","level":{"id":9,"name":'
        + name_json.encode()
        + b'}}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"2","monotonic_seconds":1,"session_id":"x"}\n'
    )
    session = parse_session(raw)

    organized = organize_session(tmp_path, session)

    assert organized is not None
    assert organized.parent.name == expected_folder


def _session_with_dt_snapshot() -> bytes:
    return (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x"}\n'
        b'{"schema_version":"2.0.0","event_type":"death_tracker_snapshot","timestamp_ms":"2","monotonic_seconds":1,"session_id":"x","level_id":75603568,"level_name":"Tabasco","attempts":537,"new_best_percent":69,"real_end_percent":100,"difficulty":7}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"3","monotonic_seconds":2,"session_id":"x"}\n'
    )


def test_death_tracker_snapshot_event_parses_and_requires_fields():
    session = parse_session(_session_with_dt_snapshot())
    assert session.session_id == "x"
    assert len(session.events) == 3

    bad = _session_with_dt_snapshot().replace(
        b'"level_id":75603568', b'"level_id":"75603568"'
    )
    with pytest.raises(TelemetryValidationError, match="death_tracker_snapshot has invalid fields"):
        parse_session(bad)

    missing = _session_with_dt_snapshot().replace(
        b',"level_id":75603568,"level_name":"Tabasco"', b""
    )
    with pytest.raises(TelemetryValidationError, match="missing fields"):
        parse_session(missing)


def _session_with_death(death_fields: bytes) -> bytes:
    return (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"0","monotonic_seconds":0,"session_id":"x"}\n'
        b'{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.1,"attempt_id":"a","attempt_number":1,"training_segment":false,"start_x":0}\n'
        b'{"schema_version":"2.0.0","event_type":"death_context","timestamp_ms":"2","monotonic_seconds":0.2,"attempt_id":"a","player_snapshot":{"player":1,"x":100,"y":100},"fatal_object":null,"cause":{"classification":"unknown","confidence":"none"},"context_seconds":5,"preceding_events":[]'
        + death_fields
        + b'}\n'
        b'{"schema_version":"2.0.0","event_type":"attempt_ended","timestamp_ms":"3","monotonic_seconds":0.3,"attempt_id":"a","outcome":"death","start_x":0,"end_x":100,"training_segment":false}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"4","monotonic_seconds":0.4,"session_id":"x"}\n'
    )


def test_death_context_accepts_valid_death_percent():
    session = parse_session(_session_with_death(b',"death_percent":42.5'))
    death = next(event for event in session.events if event["event_type"] == "death_context")
    assert death["death_percent"] == 42.5


def test_death_context_accepts_null_death_percent():
    session = parse_session(_session_with_death(b',"death_percent":null'))
    death = next(event for event in session.events if event["event_type"] == "death_context")
    assert death["death_percent"] is None


@pytest.mark.parametrize(
    ("bad_field", "message"),
    [
        (b',"death_percent":"fifty"', "death_percent"),
        (b',"death_percent":101', "death_percent"),
        (b',"death_percent":-1', "death_percent"),
    ],
)
def test_death_context_rejects_invalid_death_percent(bad_field, message):
    with pytest.raises(TelemetryValidationError, match=message):
        parse_session(_session_with_death(bad_field))
