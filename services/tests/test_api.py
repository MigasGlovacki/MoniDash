from pathlib import Path

from fastapi.testclient import TestClient
from monidash_hub.api import create_app
from monidash_hub.config import Settings

FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-session.jsonl"


def test_ingestion_auth_validation_and_idempotency(tmp_path):
    app = create_app(Settings("test-token", tmp_path))
    client = TestClient(app)
    payload = FIXTURE.read_bytes()

    assert client.get("/healthz").json() == {"status": "ok"}
    assert client.post("/v1/sessions", content=payload).status_code == 401
    assert client.post("/v1/sessions", headers={"Authorization": "Bearer wrong"}, content=payload).status_code == 403
    first = client.post("/v1/sessions", headers={"Authorization": "Bearer test-token", "Content-Type": "application/x-ndjson"}, content=payload)
    assert first.status_code == 201
    assert first.json()["idempotent"] is False
    duplicate = client.post("/v1/sessions", headers={"Authorization": "Bearer test-token", "Content-Type": "application/jsonl"}, content=payload)
    assert duplicate.status_code == 200
    assert duplicate.json()["idempotent"] is True
    assert client.post("/v1/sessions", headers={"Authorization": "Bearer test-token", "Content-Type": "application/x-ndjson"}, content=b"bad").status_code == 422


def test_recovery_when_raw_exists_but_index_missing_reports_fresh_import(tmp_path):
    from monidash_hub.telemetry import parse_session, store_raw

    app = create_app(Settings("test-token", tmp_path))
    client = TestClient(app)
    payload = FIXTURE.read_bytes()
    session = parse_session(payload)

    # Simulate a crash between raw persistence and index import: raw file already
    # exists on disk, but the SQLite index is empty.
    store_raw(tmp_path / "raw", session)
    from monidash_hub.store import HubStore
    assert HubStore(tmp_path / "monidash.sqlite3").latest_session() is None

    recovery = client.post(
        "/v1/sessions",
        headers={"Authorization": "Bearer test-token", "Content-Type": "application/x-ndjson"},
        content=payload,
    )

    assert recovery.status_code == 201
    assert recovery.json()["idempotent"] is False
    assert recovery.json()["digest"] == session.digest

    # A later true duplicate (raw + index both present) is idempotent.
    duplicate = client.post(
        "/v1/sessions",
        headers={"Authorization": "Bearer test-token", "Content-Type": "application/x-ndjson"},
        content=payload,
    )
    assert duplicate.status_code == 200
    assert duplicate.json()["idempotent"] is True


def test_ingestion_rejects_a_body_larger_than_the_configured_limit(tmp_path):
    payload = FIXTURE.read_bytes()
    app = create_app(Settings("test-token", tmp_path, max_body_bytes=len(payload) - 1))
    client = TestClient(app)

    response = client.post(
        "/v1/sessions",
        headers={"Authorization": "Bearer test-token", "Content-Type": "application/x-ndjson"},
        content=payload,
    )

    assert response.status_code == 413
    assert not (tmp_path / "raw").exists()
