"""MoniDash level enrichment via the GDBrowser public API.

Esboço de integração: lê sessões JSONL validadas, extrai os IDs de fase
jogados, consulta https://gdbrowser.com/api/level/<id> (com cache em disco
respeitando o rate limit de 150 req/h) e produz:
  - um JSON estruturado (observado x enriquecido separados), e
  - um Markdown pronto para o Obsidian.

Princípio do projeto mantido: dados observados pela telemetria nunca são
misturados com dados vindos da API. O resumo marca `observed` (do mod) e
`registry` (da API) como blocos distintos.

CLI:
  PYTHONPATH=services python -m monidash_hub.enrich <sessao.jsonl> [--out DIR] [--offline] [--refresh]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import httpx

from .telemetry import ParsedSession, parse_session

# --- Endpoint e limites -----------------------------------------------------

API_BASE = "https://gdbrowser.com/api"
LEVEL_PATH = "/level/{level_id}"
DEFAULT_CACHE_PATH = "enrich-cache.json"
# Conservador: a API avisa 150/h; usamos bem menos para não derrubar nada.
MIN_INTERVAL_SECONDS = 0.4
# Respiração quando o servidor devolve -1 ou erro (não é fatal).
NOT_FOUND = -1


# --- Extração de IDs a partir dos eventos -----------------------------------

def extract_level_ids(events: Sequence[dict[str, Any]]) -> list[int]:
    """IDs únicos de fases mencionados na sessão, na ordem de aparição.

    Usa `level.id` do session_started e `copy_level_id` dos copy_level_link.
    IDs 0 (fase sem ID oficial) são ignorados: não há o que consultar.
    """
    seen: list[int] = []
    for event in events:
        kind = event["event_type"]
        candidate: int | None = None
        if kind == "session_started":
            level = event.get("level") or {}
            candidate = level.get("id")
        elif kind == "copy_level_link":
            candidate = event.get("copy_level_id")
        if isinstance(candidate, int) and candidate > 0 and candidate not in seen:
            seen.append(candidate)
    return seen


# --- Cliente com cache e ritmo ----------------------------------------------

@dataclass
class LevelClient:
    """Consulta /api/level/<id> com cache em disco e intervalo mínimo."""

    cache_path: Path
    offline: bool = False
    refresh: bool = False
    _cache: dict[str, Any] = field(default_factory=dict)
    _last_request_at: float = 0.0
    _client: httpx.Client = field(default_factory=httpx.Client, repr=False)

    def __post_init__(self) -> None:
        if self.cache_path.exists():
            try:
                self._cache = json.loads(self.cache_path.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, OSError):
                self._cache = {}

    def _save(self) -> None:
        self.cache_path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.cache_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self._cache, ensure_ascii=False, indent=2), encoding="utf-8")
        tmp.replace(self.cache_path)

    def _pace(self) -> None:
        elapsed = time.monotonic() - self._last_request_at
        if elapsed < MIN_INTERVAL_SECONDS:
            time.sleep(MIN_INTERVAL_SECONDS - elapsed)
        self._last_request_at = time.monotonic()

    def level(self, level_id: int) -> dict[str, Any]:
        """Retorna o registro da fase; `{}` quando não encontrada ou offline."""
        key = str(level_id)
        if not self.refresh and key in self._cache:
            return self._cache[key]
        if self.offline:
            return {}

        self._pace()
        try:
            response = self._client.get(f"{API_BASE}{LEVEL_PATH.format(level_id=level_id)}",
                                        headers={"User-Agent": "Mozilla/5.0"}, timeout=15.0)
            if response.status_code != 200:
                record = {"error": f"http_{response.status_code}"}
            else:
                payload = response.json()
                record = payload if isinstance(payload, dict) and payload != NOT_FOUND else {}
        except (httpx.HTTPError, ValueError) as exc:
            record = {"error": str(exc)}

        self._cache[key] = record
        self._save()
        return record


# --- Enriquecimento ---------------------------------------------------------

@dataclass(frozen=True)
class EnrichedSession:
    session_id: str
    digest: str
    levels: list[dict[str, Any]]


def enrich_session(session: ParsedSession, client: LevelClient) -> EnrichedSession:
    """Monta o resumo: observado (telemetria) + registry (API) por fase."""
    levels: list[dict[str, Any]] = []
    for level_id in extract_level_ids(session.events):
        observed: dict[str, Any] = {"id": level_id}
        # Preenche observado com o primeiro nome/criador que o mod registrou.
        for event in session.events:
            if event["event_type"] == "session_started":
                level = event.get("level") or {}
                if level.get("id") == level_id:
                    observed.setdefault("name", level.get("name"))
                    observed.setdefault("creator", level.get("creator"))
                    observed.setdefault("local_or_saved", level.get("local_or_saved"))
            elif event["event_type"] == "copy_level_link":
                if event.get("copy_level_id") == level_id:
                    observed.setdefault("training_copies", 0)
                    observed["training_copies"] += 1

        registry = client.level(level_id)
        levels.append({"observed": observed, "registry": registry})

    return EnrichedSession(session.session_id, session.digest, levels)


# --- Renderização -----------------------------------------------------------

def render_markdown(enriched: EnrichedSession, source_name: str = "") -> str:
    """Gera o bloco de sessão pronto para o diário no Obsidian."""
    lines: list[str] = []
    lines.append("### Sessão enriquecida")
    if source_name:
        lines.append(f"- Fonte: `{source_name}`")
    lines.append(f"- Sessão: `{enriched.session_id}`")

    for level in enriched.levels:
        observed = level["observed"]
        registry = level["registry"]
        name = registry.get("name") or observed.get("name") or f"ID {observed['id']}"
        lines.append("")
        lines.append(f"**{name}** — `{observed['id']}`")
        if observed.get("name") and observed.get("name") != registry.get("name"):
            lines.append(f"- Nome no jogo: {observed['name']}")
        if registry.get("error"):
            lines.append(f"- ⚠️ Sem registro na API ({registry['error']}) — fase local/cópia não encontrada")
        elif registry:
            diff = registry.get("difficulty", "?")
            stars = registry.get("stars", "?")
            author = registry.get("author", "?")
            lines.append(f"- Dificuldade: **{diff}** | Estrelas: {stars} | Criador: {author}")
            length = registry.get("length", "?")
            lines.append(f"- Length: {length} | Coins: {registry.get('coins', '?')} | "
                         f"Música: {registry.get('songName', '?')} por {registry.get('songAuthor', '?')}")
            if registry.get("featured") or registry.get("epic"):
                flags = [flag for flag, value in (("featured", registry.get("featured")), ("epic", registry.get("epic"))) if value]
                lines.append(f"- Flags: {', '.join(flags)}")
            if registry.get("downloads") is not None:
                lines.append(f"- Downloads: {registry['downloads']:,} | Likes: {registry.get('likes', 0):,}".replace(",", "."))
            lines.append(f"- Link: https://gdbrowser.com/level/{observed['id']}")
        else:
            lines.append("- ⚠️ Sem registro na API (fase local/cópia não encontrada ou offline)")
        if observed.get("training_copies"):
            copies = observed["training_copies"]
            lines.append(f"- Treino: {copies} trecho(s) com start position (cópia) — vínculo oficial "
                         "permanece `needs_confirmation`")
    return "\n".join(lines)


# --- CLI --------------------------------------------------------------------

def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Enriquece sessões MoniDash com a API do GDBrowser")
    parser.add_argument("session", type=Path, help="Arquivo JSONL de sessão")
    parser.add_argument("--out", type=Path, default=None, help="Diretório de saída (JSON + MD)")
    parser.add_argument("--cache", type=Path, default=Path(DEFAULT_CACHE_PATH), help="Cache de fases")
    parser.add_argument("--offline", action="store_true", help="Usa apenas o cache, sem rede")
    parser.add_argument("--refresh", action="store_true", help="Ignora cache e re-consulta")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv if argv is not None else sys.argv[1:])
    try:
        raw = args.session.read_bytes()
        session = parse_session(raw)
    except (OSError, ValueError) as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 2

    client = LevelClient(cache_path=args.cache, offline=args.offline, refresh=args.refresh)
    enriched = enrich_session(session, client)

    markdown = render_markdown(enriched, source_name=args.session.name)
    print(markdown)

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        payload = {
            "session_id": enriched.session_id,
            "digest": enriched.digest,
            "source": args.session.name,
            "levels": enriched.levels,
        }
        args.out.joinpath(f"{enriched.session_id}.enriched.json").write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        args.out.joinpath(f"{enriched.session_id}.enriched.md").write_text(
            markdown, encoding="utf-8")
        print(f"\n[ok] JSON e MD em {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
