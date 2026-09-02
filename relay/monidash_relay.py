"""Polling-only, Windows-compatible relay for finalized MoniDash sessions."""
from __future__ import annotations

import json
import os
import sys
import time
from collections.abc import Callable
from enum import Enum
from hashlib import sha256
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class UploadOutcome(Enum):
    """Result of an upload attempt, controlling relay retry behaviour."""

    SUCCESS = "success"        # uploaded; acknowledge and never retry
    TRANSIENT = "transient"    # network/5xx/timeout; leave un-acknowledged to retry
    PERMANENT = "permanent"    # unrecoverable 4xx validation rejection; stop retrying


def is_finalized(payload: bytes) -> bool:
    try:
        lines = [json.loads(line) for line in payload.decode("utf-8").splitlines() if line.strip()]
    except (UnicodeDecodeError, json.JSONDecodeError):
        return False
    return bool(lines) and lines[-1].get("event_type") == "session_ended"


class Relay:
    def __init__(self, telemetry_dir: Path, state_file: Path, upload: Callable[[bytes], UploadOutcome]):
        self.telemetry_dir, self.state_file, self.upload = telemetry_dir, state_file, upload

    def _load_state(self) -> tuple[set[str], set[str]]:
        """Return (acknowledged, rejected) digests; a missing/corrupt state is treated as empty."""
        if not self.state_file.exists():
            return set(), set()
        try:
            data = json.loads(self.state_file.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return set(), set()
        if not isinstance(data, dict):
            return set(), set()
        acknowledged = {str(d) for d in data.get("acknowledged", [])}
        rejected = {str(d) for d in data.get("rejected", [])}
        acknowledged.difference_update(rejected)
        return acknowledged, rejected

    def _save(self, acknowledged: set[str], rejected: set[str]) -> None:
        temporary = self.state_file.with_suffix(self.state_file.suffix + ".tmp")
        temporary.write_text(
            json.dumps({"acknowledged": sorted(acknowledged), "rejected": sorted(rejected)}),
            encoding="utf-8",
        )
        temporary.replace(self.state_file)

    def poll_once(self) -> int:
        acknowledged, rejected = self._load_state()
        known = acknowledged | rejected
        count = 0
        changed = False
        for path in sorted(self.telemetry_dir.glob("*.jsonl")):
            payload = path.read_bytes()
            digest = sha256(payload).hexdigest()
            if digest in known or not is_finalized(payload):
                continue
            outcome = self.upload(payload)
            if outcome is UploadOutcome.SUCCESS:
                acknowledged.add(digest); count += 1
                known.add(digest); changed = True
            elif outcome is UploadOutcome.PERMANENT:
                rejected.add(digest)
                known.add(digest); changed = True
            # TRANSIENT: leave un-acknowledged so a later poll retries it.
        if changed:
            self._save(acknowledged, rejected)
        return count


def http_uploader(endpoint: str, token: str) -> Callable[[bytes], UploadOutcome]:
    def upload(payload: bytes) -> UploadOutcome:
        request = Request(endpoint, data=payload, headers={"Authorization": f"Bearer {token}", "Content-Type": "application/x-ndjson"}, method="POST")
        try:
            with urlopen(request, timeout=20) as response:
                return UploadOutcome.SUCCESS if response.status in (200, 201) else UploadOutcome.TRANSIENT
        except HTTPError as exc:
            code = exc.code
            if code in (401, 403, 404, 408, 429) or 500 <= code < 600:
                return UploadOutcome.TRANSIENT
            if code in (400, 413, 415, 422):
                return UploadOutcome.PERMANENT
            return UploadOutcome.TRANSIENT
        except URLError:
            return UploadOutcome.TRANSIENT
        except OSError:
            return UploadOutcome.TRANSIENT
    return upload


def run_loop(relay: Relay, interval: float, *, max_iterations: int | None = None, sleeper: Callable[[float], None] = time.sleep) -> None:
    """Run the polling loop, never terminating on transient filesystem/state errors."""
    iterations = 0
    while max_iterations is None or iterations < max_iterations:
        try:
            relay.poll_once()
        except Exception as exc:  # noqa: BLE001 - transient FS/state errors must not kill the loop
            print(f"monidash-relay: poll skipped due to transient error: {exc}", file=sys.stderr)
        sleeper(interval)
        iterations += 1


def main() -> None:
    telemetry = Path(os.environ["MONIDASH_TELEMETRY_DIR"])
    endpoint = os.environ["MONIDASH_HUB_URL"]
    token = os.environ["MONIDASH_INGEST_TOKEN"]
    relay = Relay(telemetry, Path(os.environ.get("MONIDASH_RELAY_STATE", "relay-state.json")), http_uploader(endpoint, token))
    interval = float(os.environ.get("MONIDASH_POLL_SECONDS", "10"))
    run_loop(relay, interval)


if __name__ == "__main__": main()