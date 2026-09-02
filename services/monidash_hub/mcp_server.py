"""Local read-only MCP stdio server (JSON-RPC 2.0 over newline-delimited stdio)."""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from typing import Any

from .config import Settings
from .store import HubStore


class RpcError(Exception):
    """JSON-RPC 2.0 error carrying a protocol error code."""

    def __init__(self, code: int, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


class ParseError(RpcError):
    def __init__(self, message: str = "parse error"):
        super().__init__(-32700, message)


class InvalidRequest(RpcError):
    def __init__(self, message: str = "invalid request"):
        super().__init__(-32600, message)


class MethodNotFound(RpcError):
    def __init__(self, message: str = "method not found"):
        super().__init__(-32601, message)


class InvalidParams(RpcError):
    def __init__(self, message: str = "invalid params"):
        super().__init__(-32602, message)


class InternalError(RpcError):
    def __init__(self, message: str = "internal error"):
        super().__init__(-32603, message)


_MISSING = object()
# Protocol versions the official MCP SDK clients may send; the server echoes
# the client's version when it is supported (standard negotiation).
_SUPPORTED_PROTOCOL_VERSIONS = frozenset(
    {"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"}
)


@dataclass
class ProtocolState:
    initialized: bool = False
    ready: bool = False


class QueryTools:
    def __init__(self, store: HubStore): self.store = store
    def latest_session(self) -> dict[str, Any] | None: return self.store.latest_session()
    def session_summary(self, digest: str) -> dict[str, Any] | None: return self.store.session_summary(digest)
    def death_clusters(self, digest: str | None = None) -> list[dict[str, Any]]: return self.store.death_clusters(digest)
    def reference_runs(self, digest: str | None = None) -> list[dict[str, Any]]: return self.store.reference_runs(digest)
    def death_context(self, digest: str, attempt_id: str | None = None) -> list[dict[str, Any]]: return self.store.death_context(digest, attempt_id)


_TOOLS = [
 {"name": "latest_session", "description": "Latest imported session", "inputSchema": {"type": "object", "properties": {}}},
 {"name": "session_summary", "description": "Summary by digest", "inputSchema": {"type": "object", "properties": {"digest": {"type": "string"}}, "required": ["digest"]}},
 {"name": "death_clusters", "description": "Death clusters", "inputSchema": {"type": "object", "properties": {"digest": {"type": "string"}}}},
 {"name": "reference_runs", "description": "Reference runs", "inputSchema": {"type": "object", "properties": {"digest": {"type": "string"}}}},
 {"name": "death_context", "description": "Death context by session (includes death_percent)", "inputSchema": {"type": "object", "properties": {"digest": {"type": "string"}, "attempt_id": {"type": "string"}}, "required": ["digest"]}},
]
_TOOL_BY_NAME = {tool["name"]: tool for tool in _TOOLS}


def _validate_arguments(spec: dict[str, Any], arguments: dict[str, Any]) -> None:
    schema = spec.get("inputSchema", {})
    required = schema.get("required", [])
    properties = schema.get("properties", {})
    for field in required:
        if field not in arguments:
            raise InvalidParams(f"missing argument: {field}")
    for field, value in arguments.items():
        if field not in properties:
            raise InvalidParams(f"unknown argument: {field}")
        expected = properties[field].get("type")
        if expected == "string" and not isinstance(value, str):
            raise InvalidParams(f"argument {field} must be a string")


def _dispatch(method: str, params: dict[str, Any], tools: QueryTools, state: ProtocolState) -> Any:
    if method == "initialize":
        if state.initialized:
            raise InvalidRequest("initialize may only be called once")
        requested_version = params.get("protocolVersion")
        if requested_version not in _SUPPORTED_PROTOCOL_VERSIONS:
            raise InvalidParams(
                "unsupported protocol version; supported: "
                + ", ".join(sorted(_SUPPORTED_PROTOCOL_VERSIONS))
            )
        capabilities = params.get("capabilities")
        client_info = params.get("clientInfo")
        if not isinstance(capabilities, dict):
            raise InvalidParams("initialize requires capabilities as an object")
        if not isinstance(client_info, dict):
            raise InvalidParams("initialize requires clientInfo as an object")
        if not isinstance(client_info.get("name"), str) or not client_info["name"]:
            raise InvalidParams("initialize clientInfo requires a non-empty name")
        if not isinstance(client_info.get("version"), str) or not client_info["version"]:
            raise InvalidParams("initialize clientInfo requires a non-empty version")
        state.initialized = True
        return {
            "protocolVersion": requested_version,
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "monidash-hub", "version": "0.1.0"},
        }
    if method == "notifications/initialized":
        if not state.initialized:
            raise InvalidRequest("initialize must be called before notifications/initialized")
        state.ready = True
        return None
    if not state.ready:
        raise InvalidRequest("initialize and notifications/initialized are required before requests")
    if method == "tools/list":
        return {"tools": _TOOLS}
    if method == "tools/call":
        name = params.get("name")
        if not isinstance(name, str):
            raise InvalidParams("tools/call requires a string 'name'")
        arguments = params.get("arguments", {})
        if not isinstance(arguments, dict):
            raise InvalidParams("arguments must be an object")
        spec = _TOOL_BY_NAME.get(name)
        if spec is None:
            raise MethodNotFound(f"unknown tool: {name}")
        _validate_arguments(spec, arguments)
        value = getattr(tools, name)(**arguments)
        return {"content": [{"type": "text", "text": json.dumps(value)}], "structuredContent": value}
    raise MethodNotFound(f"unsupported method: {method}")


def handle_request(line: str, tools: QueryTools, state: ProtocolState | None = None) -> dict[str, Any] | None:
    """Handle one JSON-RPC 2.0 line; stateful servers must pass ProtocolState."""
    try:
        data = json.loads(line)
    except json.JSONDecodeError:
        return _error(None, -32700, "parse error")
    if not isinstance(data, dict):
        return _error(None, -32600, "invalid request")
    ident = data.get("id", _MISSING)
    is_notification = ident is _MISSING
    if not is_notification and (ident is None or isinstance(ident, bool) or not isinstance(ident, (str, int))):
        return _error(None, -32600, "id must be a string or integer")
    # The optional state keeps this pure helper backward-compatible for callers
    # that only dispatch individual requests. The stdio server always supplies it.
    state = state or ProtocolState(ready=True)

    def err(error: RpcError) -> dict[str, Any] | None:
        return None if is_notification else _error(ident, error.code, error.message)

    try:
        if data.get("jsonrpc") != "2.0":
            raise InvalidRequest("jsonrpc must be 2.0")
        method = data.get("method")
        if not isinstance(method, str):
            raise InvalidRequest("method must be a string")
        if method == "notifications/initialized" and not is_notification:
            raise InvalidRequest("notifications/initialized must be a notification")
        params = data.get("params", {})
        if not isinstance(params, dict):
            raise InvalidParams("params must be an object")
        result = _dispatch(method, params, tools, state)
    except RpcError as exc:
        return err(exc)
    except Exception:  # noqa: BLE001
        return err(InternalError("internal error"))
    if is_notification:
        return None
    return {"jsonrpc": "2.0", "id": ident, "result": result}


def _error(ident: Any, code: int, message: str) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": ident, "error": {"code": code, "message": message}}


def serve(stdin, stdout, tools: QueryTools) -> None:
    state = ProtocolState()
    for line in stdin:
        line = line.strip()
        if not line:
            continue
        response = handle_request(line, tools, state)
        if response is not None:
            print(json.dumps(response), file=stdout, flush=True)


def main() -> None:
    tools = QueryTools(HubStore(Settings.from_environment().database))
    serve(sys.stdin, sys.stdout, tools)


if __name__ == "__main__": main()