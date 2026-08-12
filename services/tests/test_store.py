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
