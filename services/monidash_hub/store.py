"""Transactional SQLite index and read-only query layer."""

from __future__ import annotations

import json
import sqlite3
from collections.abc import Iterable
from pathlib import Path
from typing import Any

from .telemetry import ParsedSession


class HubStore:
    def __init__(self, database: Path):
        self.database = database
        database.parent.mkdir(parents=True, exist_ok=True)
        self._initialize()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.database)
        connection.row_factory = sqlite3.Row
        return connection

    def _initialize(self) -> None:
        with self._connect() as db:
            db.executescript("""
                CREATE TABLE IF NOT EXISTS sessions (
                  digest TEXT PRIMARY KEY, session_id TEXT NOT NULL, started_ms TEXT,
                  event_count INTEGER NOT NULL, imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
                );
                CREATE TABLE IF NOT EXISTS attempts (digest TEXT NOT NULL, attempt_id TEXT NOT NULL, outcome TEXT, start_x REAL, end_x REAL);
                CREATE TABLE IF NOT EXISTS deaths (digest TEXT NOT NULL, attempt_id TEXT, monotonic_seconds REAL, classification TEXT, context_json TEXT);
                CREATE TABLE IF NOT EXISTS references_run (digest TEXT NOT NULL, reference_id TEXT, attempt_id TEXT, start_x REAL, end_x REAL, link_status TEXT);
                CREATE TABLE IF NOT EXISTS death_tracker_snapshots (digest TEXT NOT NULL, level_id INTEGER, level_name TEXT, attempts INTEGER, new_best_percent INTEGER, real_end_percent INTEGER, difficulty INTEGER, snapshot_json TEXT);
            """)

    def import_session(self, session: ParsedSession) -> bool:
        return self.import_events(session.digest, session.session_id, session.events)

    def import_events(self, digest: str, session_id: str, events: Iterable[dict[str, Any]]) -> bool:
        events = list(events)
        with self._connect() as db:
            try:
                db.execute("BEGIN IMMEDIATE")
                if db.execute("SELECT 1 FROM sessions WHERE digest = ?", (digest,)).fetchone():
                    db.rollback()
                    return False
                started = next(event for event in events if event["event_type"] == "session_started")
                db.execute("INSERT INTO sessions(digest, session_id, started_ms, event_count) VALUES (?, ?, ?, ?)",
                           (digest, session_id, started["timestamp_ms"], len(events)))
                for event in events:
                    kind = event["event_type"]
                    if kind == "attempt_started":
                        db.execute("INSERT INTO attempts VALUES (?, ?, NULL, ?, NULL)", (digest, event["attempt_id"], event.get("start_x")))
                    elif kind == "attempt_ended":
                        db.execute("UPDATE attempts SET outcome=?, end_x=? WHERE digest=? AND attempt_id=?", (event.get("outcome"), event.get("end_x"), digest, event["attempt_id"]))
                    elif kind == "death_context":
                        cause = event.get("cause") or {}
                        db.execute("INSERT INTO deaths VALUES (?, ?, ?, ?, ?)", (digest, event.get("attempt_id"), event["monotonic_seconds"], cause.get("classification"), json.dumps(event)))
                    elif kind == "reference_run_saved":
                        segment = event.get("segment") or {}
                        db.execute("INSERT INTO references_run VALUES (?, ?, ?, ?, ?, ?)", (digest, event.get("reference_id"), event.get("attempt_id"), segment.get("start_x"), segment.get("end_x"), event.get("link_status")))
                    elif kind == "death_tracker_snapshot":
                        db.execute("INSERT INTO death_tracker_snapshots VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                                   (digest, event.get("level_id"), event.get("level_name"), event.get("attempts"), event.get("new_best_percent"), event.get("real_end_percent"), event.get("difficulty"), json.dumps(event)))
                db.commit()
                return True
            except Exception:
                db.rollback()
                raise

    def latest_session(self) -> dict[str, Any] | None:
        with self._connect() as db:
            row = db.execute("SELECT digest, session_id, started_ms, event_count FROM sessions ORDER BY imported_at DESC, rowid DESC LIMIT 1").fetchone()
        return dict(row) if row else None

    def session_summary(self, digest: str) -> dict[str, Any] | None:
        with self._connect() as db:
            row = db.execute("SELECT digest, session_id, started_ms, event_count FROM sessions WHERE digest=?", (digest,)).fetchone()
            if not row: return None
            result = dict(row)
            for table, key in (("attempts", "attempt_count"), ("deaths", "death_count"), ("references_run", "reference_count")):
                result[key] = db.execute(f"SELECT COUNT(*) FROM {table} WHERE digest=?", (digest,)).fetchone()[0]
        return result

    def death_clusters(self, digest: str | None = None) -> list[dict[str, Any]]:
        sql = "SELECT classification, ROUND(monotonic_seconds, 0) AS second, COUNT(*) AS deaths FROM deaths"
        args: tuple[Any, ...] = ()
        if digest: sql += " WHERE digest=?"; args = (digest,)
        sql += " GROUP BY classification, ROUND(monotonic_seconds, 0) ORDER BY deaths DESC, second"
        with self._connect() as db: return [dict(row) for row in db.execute(sql, args)]

    def reference_runs(self, digest: str | None = None) -> list[dict[str, Any]]:
        sql, args = "SELECT reference_id, attempt_id, start_x, end_x, link_status FROM references_run", ()
        if digest: sql += " WHERE digest=?"; args = (digest,)
        with self._connect() as db: return [dict(row) for row in db.execute(sql, args)]

    def death_context(self, digest: str, attempt_id: str | None = None) -> list[dict[str, Any]]:
        sql, args = "SELECT attempt_id, monotonic_seconds, classification, context_json FROM deaths WHERE digest=?", [digest]
        if attempt_id: sql += " AND attempt_id=?"; args.append(attempt_id)
        with self._connect() as db: return [dict(row) for row in db.execute(sql, args)]

    def death_tracker_snapshot(self, digest: str | None = None) -> list[dict[str, Any]]:
        sql, args = "SELECT digest, level_id, level_name, attempts, new_best_percent, real_end_percent, difficulty, snapshot_json FROM death_tracker_snapshots", ()
        if digest: sql += " WHERE digest=?"; args = (digest,)
        with self._connect() as db: return [dict(row) for row in db.execute(sql, args)]
