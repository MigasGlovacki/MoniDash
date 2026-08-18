from pathlib import Path

from monidash_hub.mcp_server import QueryTools
from monidash_hub.store import HubStore
from monidash_hub.telemetry import parse_session

FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-session.jsonl"
DEATHS_FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-deaths-session.jsonl"


def test_query_tools_are_read_only_and_return_json_data(tmp_path):
    tools = QueryTools(HubStore(tmp_path / "hub.sqlite3"))
    assert tools.latest_session() is None
    session = parse_session(FIXTURE.read_bytes())
    tools.store.import_session(session)
    assert tools.latest_session()["digest"] == session.digest
    assert tools.session_summary(session.digest)["reference_count"] == 1
    assert tools.death_clusters(session.digest) == []
    assert tools.reference_runs(session.digest)[0]["reference_id"] == "session-test-reference-1"
    assert tools.death_context(session.digest) == []


def test_death_clusters_group_real_deaths_by_classification_and_second(tmp_path):
    store = HubStore(tmp_path / "hub.sqlite3")
    session = parse_session(DEATHS_FIXTURE.read_bytes())
    assert store.import_session(session) is True

    summary = store.session_summary(session.digest)
    assert summary["attempt_count"] == 3
    assert summary["death_count"] == 3
    assert summary["reference_count"] == 0

    clusters = store.death_clusters(session.digest)
    assert len(clusters) == 2
    spike, block = clusters[0], clusters[1]
    assert spike["classification"] == "spike"
    assert spike["second"] == 12
    assert spike["deaths"] == 2
    assert block["classification"] == "block"
    assert block["second"] == 46
    assert block["deaths"] == 1


def test_death_context_returns_real_death_rows_and_filters_by_attempt(tmp_path):
    tools = QueryTools(HubStore(tmp_path / "hub.sqlite3"))
    session = parse_session(DEATHS_FIXTURE.read_bytes())
    tools.store.import_session(session)

    all_deaths = tools.death_context(session.digest)
    assert len(all_deaths) == 3
    assert {row["attempt_id"] for row in all_deaths} == {
        "session-deaths-attempt-1",
        "session-deaths-attempt-2",
        "session-deaths-attempt-3",
    }
    classifications = [row["classification"] for row in all_deaths]
    assert classifications.count("spike") == 2
    assert classifications.count("block") == 1
    spike_context = next(row for row in all_deaths if row["attempt_id"] == "session-deaths-attempt-1")
    assert spike_context["monotonic_seconds"] == 12.3
    assert spike_context["death_percent"] == 15.0
    assert "fatal_object" in spike_context["context_json"]

    only_block = tools.death_context(session.digest, "session-deaths-attempt-3")
    assert len(only_block) == 1
    assert only_block[0]["classification"] == "block"

    assert tools.death_clusters(session.digest)[0]["deaths"] == 2
