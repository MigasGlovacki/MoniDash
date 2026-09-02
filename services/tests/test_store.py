from pathlib import Path

from monidash_hub.store import HubStore
from monidash_hub.telemetry import parse_session

FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-session.jsonl"


def test_import_indexes_fixture_and_is_idempotent(tmp_path):
    store = HubStore(tmp_path / "hub.sqlite3")
    session = parse_session(FIXTURE.read_bytes())

    first = store.import_session(session)
    second = store.import_session(session)
    summary = store.session_summary(session.digest)

    assert first is True
    assert second is False
    assert summary["session_id"] == "session-test"
    assert summary["attempt_count"] == 1
    assert summary["death_count"] == 0
    assert summary["reference_count"] == 1


def test_failed_import_rolls_back_all_rows(tmp_path):
    store = HubStore(tmp_path / "hub.sqlite3")
    session = parse_session(FIXTURE.read_bytes())
    session_events = list(session.events)
    session_events.append({"event_type": "death_context"})

    import pytest
    with pytest.raises(KeyError):
        store.import_events(session.digest, session.session_id, session_events)
    assert store.latest_session() is None


def test_death_tracker_snapshot_is_indexed_and_queryable(tmp_path):
    store = HubStore(tmp_path / "hub.sqlite3")
    raw = (
        b'{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"1","monotonic_seconds":0,"session_id":"x"}\n'
        b'{"schema_version":"2.0.0","event_type":"death_tracker_snapshot","timestamp_ms":"2","monotonic_seconds":1,"session_id":"x","level_id":75603568,"level_name":"Tabasco","attempts":537,"new_best_percent":69,"real_end_percent":100,"difficulty":7}\n'
        b'{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"3","monotonic_seconds":2,"session_id":"x"}\n'
    )
    session = parse_session(raw)

    assert store.import_session(session) is True
    rows = store.death_tracker_snapshot(session.digest)

    assert len(rows) == 1
    assert rows[0]["level_id"] == 75603568
    assert rows[0]["level_name"] == "Tabasco"
    assert rows[0]["attempts"] == 537
    assert rows[0]["new_best_percent"] == 69
    assert rows[0]["real_end_percent"] == 100
    assert rows[0]["difficulty"] == 7


def test_death_percent_is_indexed_and_queryable(tmp_path):
    deaths_fixture = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-deaths-session.jsonl"
    store = HubStore(tmp_path / "hub.sqlite3")
    session = parse_session(deaths_fixture.read_bytes())
    assert store.import_session(session) is True

    rows = store.death_context(session.digest)
    percents = {row["attempt_id"]: row["death_percent"] for row in rows}
    assert percents == {
        "session-deaths-attempt-1": 15.0,
        "session-deaths-attempt-2": 15.5,
        "session-deaths-attempt-3": 45.0,
    }


def test_existing_database_is_migrated_with_death_percent_column(tmp_path):
    import sqlite3

    database = tmp_path / "hub.sqlite3"
    with sqlite3.connect(database) as db:
        db.execute(
            "CREATE TABLE deaths (digest TEXT NOT NULL, attempt_id TEXT, monotonic_seconds REAL, classification TEXT, context_json TEXT)"
        )
    store = HubStore(database)
    with sqlite3.connect(database) as db:
        columns = {row[1] for row in db.execute("PRAGMA table_info(deaths)")}
    assert "death_percent" in columns
