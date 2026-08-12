import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

SERVICES_DIR = Path(__file__).resolve().parents[1]
FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-session.jsonl"
DEATHS_FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-deaths-session.jsonl"


INITIALIZE_PARAMS = {
    "protocolVersion": "2025-03-26",
    "capabilities": {},
    "clientInfo": {"name": "monidash-stdio-test", "version": "1.0.0"},
}


@pytest.fixture
def mcp_process(tmp_path):
    env = os.environ.copy()
    env["MONIDASH_INGEST_TOKEN"] = "test-token"
    env["MONIDASH_DATA_DIR"] = str(tmp_path)
    env["PYTHONPATH"] = str(SERVICES_DIR) + os.pathsep + env.get("PYTHONPATH", "")
    proc = subprocess.Popen(
        [sys.executable, "-u", "-m", "monidash_hub.mcp_server"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, env=env,
    )
    yield proc
    proc.stdin.close()
    proc.wait(timeout=5)
    proc.stderr.read()


def _send(proc, message: dict | str) -> str | None:
    line = message if isinstance(message, str) else json.dumps(message)
    proc.stdin.write(line + "\n")
    proc.stdin.flush()
    return proc.stdout.readline()


def _ready(proc) -> None:
    response = json.loads(_send(proc, {"jsonrpc": "2.0", "id": "initialize", "method": "initialize", "params": INITIALIZE_PARAMS}))
    assert response["result"]["protocolVersion"] == "2025-03-26"
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    proc.stdin.flush()


def test_initialize_protocol(tmp_path, mcp_process):
    resp = _send(mcp_process, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": INITIALIZE_PARAMS})
    data = json.loads(resp)
    assert data["jsonrpc"] == "2.0"
    assert data["id"] == 1
    assert data["result"]["protocolVersion"] == "2025-03-26"
    assert "tools" in data["result"]["capabilities"]
    assert data["result"]["serverInfo"]["name"] == "monidash-hub"


def test_initialized_notification_produces_no_response(tmp_path, mcp_process):
    # Prime the hub with a session so queries have data later.
    mcp_process  # noqa
    _ready(mcp_process)
    # A notification (no id) must not yield any response line.
    mcp_process.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
    mcp_process.stdin.flush()
    # The next response must correspond to the subsequent request, not the notification.
    resp = _send(mcp_process, {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
    data = json.loads(resp)
    assert data["id"] == 2
    assert {tool["name"] for tool in data["result"]["tools"]} >= {"latest_session", "session_summary", "death_clusters"}


def test_tools_call_latest_session_returns_imported_data(tmp_path, mcp_process):
    # Import a session into the same data dir the server reads.
    sys.path.insert(0, str(SERVICES_DIR))
    from monidash_hub.store import HubStore
    from monidash_hub.telemetry import parse_session

    session = parse_session(FIXTURE.read_bytes())
    HubStore(tmp_path / "monidash.sqlite3").import_session(session)
    _ready(mcp_process)

    resp = _send(mcp_process, {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "latest_session", "arguments": {}}})
    data = json.loads(resp)
    assert data["id"] == 3
    assert data["result"]["structuredContent"]["digest"] == session.digest


def test_tools_call_missing_digest_returns_invalid_params(tmp_path, mcp_process):
    _ready(mcp_process)
    resp = _send(mcp_process, {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "session_summary", "arguments": {}}})
    data = json.loads(resp)
    assert data["error"]["code"] == -32602
    assert data["id"] == 4


def test_unknown_method_returns_method_not_found(tmp_path, mcp_process):
    _ready(mcp_process)
    resp = _send(mcp_process, {"jsonrpc": "2.0", "id": 5, "method": "frobnicate"})
    data = json.loads(resp)
    assert data["error"]["code"] == -32601


def test_parse_error_returns_parse_error_with_null_id(tmp_path, mcp_process):
    resp = _send(mcp_process, "{not valid json")
    data = json.loads(resp)
    assert data["error"]["code"] == -32700
    assert data["id"] is None


def test_death_clusters_over_stdio_with_real_death_data(tmp_path, mcp_process):
    sys.path.insert(0, str(SERVICES_DIR))
    from monidash_hub.store import HubStore
    from monidash_hub.telemetry import parse_session

    session = parse_session(DEATHS_FIXTURE.read_bytes())
    HubStore(tmp_path / "monidash.sqlite3").import_session(session)
    _ready(mcp_process)

    resp = _send(mcp_process, {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": "death_clusters", "arguments": {"digest": session.digest}}})
    data = json.loads(resp)
    clusters = data["result"]["structuredContent"]
    assert clusters[0]["classification"] == "spike"
    assert clusters[0]["deaths"] == 2