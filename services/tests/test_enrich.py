"""Tests for the MoniDash level enrichment esboço."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from monidash_hub.enrich import (
    EnrichedSession,
    LevelClient,
    enrich_session,
    extract_level_ids,
    render_markdown,
)
from monidash_hub.telemetry import parse_session


def _session(raw_lines: list[str]) -> bytes:
    return ("\n".join(raw_lines) + "\n").encode("utf-8")


SESSION_WITH_COPY = _session(
    [
        '{"schema_version":"2.0.0","event_type":"session_started","timestamp_ms":"0","monotonic_seconds":0.000,"session_id":"s1","level":{"id":123,"name":"Test","creator":"Creator","length_category":4,"extent_x":10000.000,"local_or_saved":true,"platformer":false}}',
        '{"schema_version":"2.0.0","event_type":"attempt_started","timestamp_ms":"1","monotonic_seconds":0.100,"attempt_id":"s1-attempt-1","attempt_number":1,"training_segment":true,"start_x":5000.000}',
        '{"schema_version":"2.0.0","event_type":"copy_level_link","timestamp_ms":"1","monotonic_seconds":0.100,"attempt_id":"s1-attempt-1","copy_level_id":123,"official_level_id":null,"link_status":"needs_confirmation","start_x":5000.000}',
        '{"schema_version":"2.0.0","event_type":"attempt_ended","timestamp_ms":"3","monotonic_seconds":10.050,"attempt_id":"s1-attempt-1","outcome":"completed","start_x":5000.000,"end_x":10000.000,"training_segment":true}',
        '{"schema_version":"2.0.0","event_type":"session_ended","timestamp_ms":"4","monotonic_seconds":10.100,"session_id":"s1"}',
    ]
)


def test_extract_level_ids_dedupes_and_ignores_zero() -> None:
    events = [
        {"event_type": "session_started", "level": {"id": 123}},
        {"event_type": "copy_level_link", "copy_level_id": 123},
        {"event_type": "copy_level_link", "copy_level_id": 0},
        {"event_type": "copy_level_link", "copy_level_id": 456},
    ]
    assert extract_level_ids(events) == [123, 456]


def test_extract_level_ids_from_real_fixture(tmp_path: Path) -> None:
    fixture = tmp_path / "sample-session.jsonl"
    fixture.write_bytes(
        Path(__file__).resolve().parents[2].joinpath("tests", "fixtures", "sample-session.jsonl").read_bytes()
    )
    parsed = parse_session(fixture.read_bytes())
    assert extract_level_ids(parsed.events) == [123]


def test_enrich_offline_keeps_observed_separate(tmp_path: Path) -> None:
    parsed = parse_session(SESSION_WITH_COPY)
    client = LevelClient(cache_path=tmp_path / "cache.json", offline=True)
    enriched = enrich_session(parsed, client)
    assert isinstance(enriched, EnrichedSession)
    assert len(enriched.levels) == 1
    level = enriched.levels[0]
    # Observado veio da telemetria; registry vazio por estar offline.
    assert level["observed"]["name"] == "Test"
    assert level["observed"]["training_copies"] == 1
    assert level["registry"] == {}


def test_render_markdown_warns_when_unregistered(tmp_path: Path) -> None:
    parsed = parse_session(SESSION_WITH_COPY)
    client = LevelClient(cache_path=tmp_path / "cache.json", offline=True)
    enriched = enrich_session(parsed, client)
    md = render_markdown(enriched)
    assert "⚠️ Sem registro na API" in md
    assert "Treino: 1 trecho(s)" in md


def test_render_markdown_shows_registry_data(tmp_path: Path) -> None:
    parsed = parse_session(SESSION_WITH_COPY)
    cache = tmp_path / "cache.json"
    cache.write_text(json.dumps({"123": {"name": "Test (API)", "difficulty": "Easy Demon", "stars": 10, "author": "Tau"}}), encoding="utf-8")
    client = LevelClient(cache_path=cache, offline=True)
    enriched = enrich_session(parsed, client)
    md = render_markdown(enriched)
    assert "Easy Demon" in md
    assert "Tau" in md


def test_render_markdown_warns_when_api_error(tmp_path: Path) -> None:
    """Erro HTTP no cache vira aviso, não bloco de dados com '?'."""
    parsed = parse_session(SESSION_WITH_COPY)
    cache = tmp_path / "cache.json"
    cache.write_text(json.dumps({"123": {"error": "http_500"}}), encoding="utf-8")
    client = LevelClient(cache_path=cache, offline=True)
    enriched = enrich_session(parsed, client)
    md = render_markdown(enriched)
    assert "⚠️ Sem registro na API (http_500)" in md
    assert "Dificuldade: **?**" not in md


def test_cache_respects_offline_without_network(tmp_path: Path) -> None:
    """Offline não faz requisição; cache pré-existente é usado."""
    cache = tmp_path / "cache.json"
    cache.write_text(json.dumps({"999": {"name": "Cached", "difficulty": "Hard"}}), encoding="utf-8")
    client = LevelClient(cache_path=cache, offline=True)
    assert client.level(999)["name"] == "Cached"
    assert client.level(1) == {}
