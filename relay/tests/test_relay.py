import json
import sys
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError, URLError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from monidash_relay import Relay, UploadOutcome, http_uploader, run_loop


def _write_session(path: Path) -> None:
    path.write_text('{"event_type":"session_started"}\n{"event_type":"session_ended"}\n')


def test_relay_skips_incomplete_acknowledges_once_and_retries(tmp_path):
    telemetry = tmp_path / "telemetry"; telemetry.mkdir()
    incomplete = telemetry / "open.jsonl"; incomplete.write_text('{"event_type":"session_started"}\n')
    complete = telemetry / "done.jsonl"; _write_session(complete)
    calls = []
    relay = Relay(telemetry, tmp_path / "state.json", lambda content: calls.append(content) or UploadOutcome.TRANSIENT)
    assert relay.poll_once() == 0
    assert not (tmp_path / "state.json").exists()
    relay.upload = lambda content: calls.append(content) or UploadOutcome.SUCCESS
    assert relay.poll_once() == 1
    assert relay.poll_once() == 0
    assert len(calls) == 2
    assert complete.read_text().endswith("session_ended\"}\n")


def test_permanent_rejection_is_acknowledged_and_not_retried(tmp_path):
    telemetry = tmp_path / "telemetry"; telemetry.mkdir()
    complete = telemetry / "done.jsonl"; _write_session(complete)
    calls = []
    relay = Relay(telemetry, tmp_path / "state.json", lambda content: calls.append(content) or UploadOutcome.PERMANENT)

    assert relay.poll_once() == 0  # nothing uploaded
    assert relay.poll_once() == 0  # not retried again
    assert len(calls) == 1

    state = json.loads((tmp_path / "state.json").read_text(encoding="utf-8"))
    assert complete.name not in {Path(f).name for f in state["acknowledged"]}
    assert len(state["rejected"]) == 1
    # source JSONL is untouched
    assert complete.read_text().endswith("session_ended\"}\n")


def test_transient_failure_is_retried_on_next_poll(tmp_path):
    telemetry = tmp_path / "telemetry"; telemetry.mkdir()
    complete = telemetry / "done.jsonl"; _write_session(complete)
    calls = []
    outcomes = [UploadOutcome.TRANSIENT, UploadOutcome.SUCCESS]
    relay = Relay(telemetry, tmp_path / "state.json", lambda content: calls.append(content) or outcomes.pop(0))

    assert relay.poll_once() == 0
    assert relay.poll_once() == 1
    assert relay.poll_once() == 0
    assert len(calls) == 2
    assert set(json.loads((tmp_path / "state.json").read_text(encoding="utf-8"))["acknowledged"]).__len__() == 1


def test_http_uploader_classifies_status_codes(monkeypatch):
    uploader = http_uploader("http://hub/v1/sessions", "token")

    def make_http_error(code):
        return HTTPError("http://hub", code, "err", {}, None)  # type: ignore[arg-type]

    for code in (400, 415, 422):
        with patch("monidash_relay.urlopen", side_effect=make_http_error(code)):
            assert uploader(b"x") is UploadOutcome.PERMANENT, code

    # Authentication and endpoint configuration can be corrected after the relay
    # starts; retaining the raw JSONL for retry avoids silently losing a session.
    for code in (401, 403, 404, 408, 429, 500, 502, 503, 504):
        with patch("monidash_relay.urlopen", side_effect=make_http_error(code)):
            assert uploader(b"x") is UploadOutcome.TRANSIENT, code

    with patch("monidash_relay.urlopen", side_effect=URLError("net")):
        assert uploader(b"x") is UploadOutcome.TRANSIENT


class _FakeResponse:
    def __init__(self, status): self.status = status
    def __enter__(self): return self
    def __exit__(self, *a): return False


def test_http_uploader_success_on_2xx(monkeypatch):
    uploader = http_uploader("http://hub/v1/sessions", "token")
    with patch("monidash_relay.urlopen", return_value=_FakeResponse(201)):
        assert uploader(b"x") is UploadOutcome.SUCCESS
    with patch("monidash_relay.urlopen", return_value=_FakeResponse(200)):
        assert uploader(b"x") is UploadOutcome.SUCCESS


def test_main_loop_survives_transient_filesystem_error(tmp_path):
    relay = Relay(tmp_path / "telemetry", tmp_path / "state.json", lambda content: UploadOutcome.SUCCESS)
    calls = []

    def boom_then_ok():
        calls.append(1)
        if len(calls) == 1:
            raise OSError("disk wobble")
        return 0

    relay.poll_once = boom_then_ok  # type: ignore[assignment]
    run_loop(relay, interval=0, max_iterations=3, sleeper=lambda _: None)
    assert len(calls) == 3  # the loop never terminated after the first error


def test_main_loop_survives_corrupt_state_file(tmp_path):
    telemetry = tmp_path / "telemetry"; telemetry.mkdir()
    complete = telemetry / "done.jsonl"; _write_session(complete)
    state = tmp_path / "state.json"; state.write_text("{not json", encoding="utf-8")
    relay = Relay(telemetry, state, lambda content: UploadOutcome.SUCCESS)

    # Corrupt state must not kill the loop; poll_once should tolerate it and continue.
    iterations = {"n": 0}

    def sleeper(_):
        iterations["n"] += 1

    run_loop(relay, interval=0, max_iterations=1, sleeper=sleeper)
    assert iterations["n"] == 1