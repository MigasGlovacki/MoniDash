import json
from pathlib import Path

import pytest
from monidash_hub.mcp_server import ProtocolState, QueryTools, handle_request
from monidash_hub.store import HubStore
from monidash_hub.telemetry import parse_session

FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-session.jsonl"
DEATHS_FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "sample-deaths-session.jsonl"


def _tools(tmp_path, fixture=FIXTURE):
    tools = QueryTools(HubStore(tmp_path / "hub.sqlite3"))
    session = parse_session(fixture.read_bytes())
    tools.store.import_session(session)
    return tools, session


def _req(method, ident=1, params=None, jsonrpc="2.0"):
    msg = {"jsonrpc": jsonrpc, "method": method}
    if ident is not None:
        msg["id"] = ident
    if params is not None:
        msg["params"] = params
    return json.dumps(msg)


def _initialize_params(**overrides):
    params = {
        "protocolVersion": "2025-03-26",
        "capabilities": {},
        "clientInfo": {"name": "monidash-test-client", "version": "1.0.0"},
    }
    params.update(overrides)
    return params


def test_initialize_returns_protocol_capabilities_and_server_info(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("initialize", ident=1, params=_initialize_params()), tools)
    assert resp["jsonrpc"] == "2.0"
    assert resp["id"] == 1
    assert resp["result"]["protocolVersion"] == "2025-03-26"
    assert "tools" in resp["result"]["capabilities"]
    assert resp["result"]["serverInfo"]["name"] == "monidash-hub"


def test_initialize_negotiates_supported_client_version(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("initialize", ident=1, params=_initialize_params(protocolVersion="2025-11-25")), tools)
    assert resp is not None
    assert resp["result"]["protocolVersion"] == "2025-11-25"


def test_initialized_notification_has_no_response(tmp_path):
    tools, _ = _tools(tmp_path)
    assert handle_request(_req("initialized", ident=None), tools) is None


def test_mcp_requires_supported_initialize_then_initialized_before_calls(tmp_path):
    tools, _ = _tools(tmp_path)
    state = ProtocolState()

    before_ready = handle_request(_req("tools/list", ident=1), tools, state)
    assert before_ready["error"]["code"] == -32600
    unsupported = handle_request(_req("initialize", ident=2, params=_initialize_params(protocolVersion="2099-01-01")), tools, state)
    assert unsupported["error"]["code"] == -32602
    initialized = handle_request(_req("initialize", ident=3, params=_initialize_params()), tools, state)
    assert initialized["result"]["protocolVersion"] == "2025-03-26"
    before_notification = handle_request(_req("tools/list", ident=4), tools, state)
    assert before_notification["error"]["code"] == -32600
    assert handle_request(_req("notifications/initialized", ident=None), tools, state) is None
    assert "tools" in handle_request(_req("tools/list", ident=5), tools, state)["result"]


@pytest.mark.parametrize("ident", [None, True, False, {}, []])
def test_mcp_rejects_invalid_request_ids(tmp_path, ident):
    tools, _ = _tools(tmp_path)
    message = {"jsonrpc": "2.0", "id": ident, "method": "initialize", "params": _initialize_params()}
    response = handle_request(json.dumps(message), tools, ProtocolState())
    assert response["error"]["code"] == -32600


@pytest.mark.parametrize(
    "params",
    [
        {"protocolVersion": "2025-03-26"},
        {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {}},
        {"protocolVersion": "2025-03-26", "capabilities": [], "clientInfo": {"name": "client", "version": "1"}},
    ],
)
def test_initialize_requires_valid_client_capabilities_and_info(tmp_path, params):
    tools, _ = _tools(tmp_path)
    response = handle_request(_req("initialize", params=params), tools, ProtocolState())
    assert response["error"]["code"] == -32602


def test_tools_list_returns_all_tools(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("tools/list", ident=2), tools)
    names = {tool["name"] for tool in resp["result"]["tools"]}
    assert names == {"latest_session", "session_summary", "death_clusters", "reference_runs", "death_context"}
    for tool in resp["result"]["tools"]:
        assert tool["description"]
        assert "inputSchema" in tool


def test_tools_call_latest_session(tmp_path):
    tools, session = _tools(tmp_path)
    resp = handle_request(_req("tools/call", ident=3, params={"name": "latest_session", "arguments": {}}), tools)
    assert resp["result"]["structuredContent"]["digest"] == session.digest
    assert resp["result"]["content"][0]["type"] == "text"


def test_tools_call_session_summary_and_death_clusters_with_real_data(tmp_path):
    tools, session = _tools(tmp_path, fixture=DEATHS_FIXTURE)
    resp = handle_request(_req("tools/call", ident=4, params={"name": "session_summary", "arguments": {"digest": session.digest}}), tools)
    assert resp["result"]["structuredContent"]["death_count"] == 3
    resp2 = handle_request(_req("tools/call", ident=5, params={"name": "death_clusters", "arguments": {"digest": session.digest}}), tools)
    assert resp2["result"]["structuredContent"][0]["classification"] == "spike"


def test_tools_call_missing_required_argument_is_invalid_params(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("tools/call", ident=6, params={"name": "session_summary", "arguments": {}}), tools)
    assert resp["error"]["code"] == -32602
    assert "digest" in resp["error"]["message"]


def test_tools_call_wrong_argument_type_is_invalid_params(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("tools/call", ident=7, params={"name": "session_summary", "arguments": {"digest": 123}}), tools)
    assert resp["error"]["code"] == -32602


def test_tools_call_unknown_tool_is_method_not_found(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("tools/call", ident=8, params={"name": "nope", "arguments": {}}), tools)
    assert resp["error"]["code"] == -32601


def test_unknown_method_is_method_not_found(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("frobnicate", ident=9), tools)
    assert resp["error"]["code"] == -32601


def test_parse_error_returns_parse_error_class(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request("{not json", tools)
    assert resp["error"]["code"] == -32700
    assert resp["id"] is None


def test_not_a_request_object_is_invalid_request(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request("[1, 2, 3]", tools)
    assert resp["error"]["code"] == -32600
    assert resp["id"] is None


def test_missing_jsonrpc_version_is_invalid_request(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(json.dumps({"id": 10, "method": "tools/list"}), tools)
    assert resp["error"]["code"] == -32600


def test_notification_with_unknown_method_has_no_response(tmp_path):
    tools, _ = _tools(tmp_path)
    assert handle_request(_req("frobnicate", ident=None), tools) is None


def test_notification_with_invalid_params_has_no_response(tmp_path):
    tools, _ = _tools(tmp_path)
    msg = json.dumps({"jsonrpc": "2.0", "method": "tools/call", "params": {"arguments": {}}})
    assert handle_request(msg, tools) is None


def test_tools_call_without_name_is_invalid_params(tmp_path):
    tools, _ = _tools(tmp_path)
    resp = handle_request(_req("tools/call", ident=11, params={"arguments": {}}), tools)
    assert resp["error"]["code"] == -32602


def test_internal_error_class_for_unexpected_failure(tmp_path, monkeypatch):
    tools, _ = _tools(tmp_path)

    def boom(**kwargs):
        raise RuntimeError("kaboom")

    monkeypatch.setattr(tools, "session_summary", boom)
    resp = handle_request(_req("tools/call", ident=12, params={"name": "session_summary", "arguments": {"digest": "x"}}), tools)
    assert resp["error"]["code"] == -32603