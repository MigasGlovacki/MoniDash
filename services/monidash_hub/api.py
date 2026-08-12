"""Authenticated FastAPI ingestion endpoint."""
from __future__ import annotations

import hmac

from fastapi import FastAPI, HTTPException, Request, status
from fastapi.responses import JSONResponse

from .config import Settings
from .store import HubStore
from .telemetry import TelemetryValidationError, parse_session, store_raw


def _declared_body_is_too_large(request: Request, maximum: int) -> bool:
    content_length = request.headers.get("content-length")
    if content_length is None:
        return False
    try:
        return int(content_length) > maximum
    except ValueError:
        return False


async def _read_body_limited(request: Request, maximum: int) -> bytes:
    if _declared_body_is_too_large(request, maximum):
        raise HTTPException(status.HTTP_413_CONTENT_TOO_LARGE, "request body too large")
    chunks: list[bytes] = []
    size = 0
    async for chunk in request.stream():
        size += len(chunk)
        if size > maximum:
            raise HTTPException(status.HTTP_413_CONTENT_TOO_LARGE, "request body too large")
        chunks.append(chunk)
    return b"".join(chunks)


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = HubStore(settings.database)
    app = FastAPI(title="MoniDash Hub", docs_url=None, redoc_url=None)

    @app.get("/healthz")
    def healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.post("/v1/sessions")
    async def ingest(request: Request) -> JSONResponse:
        authorization = request.headers.get("Authorization")
        if authorization is None:
            raise HTTPException(status.HTTP_401_UNAUTHORIZED, "missing bearer token")
        scheme, _, token = authorization.partition(" ")
        if scheme.lower() != "bearer" or not hmac.compare_digest(token, settings.ingest_token):
            raise HTTPException(status.HTTP_403_FORBIDDEN, "invalid bearer token")
        content_type = request.headers.get("content-type", "").split(";", 1)[0].lower()
        if content_type not in {"application/x-ndjson", "application/jsonl"}:
            raise HTTPException(status.HTTP_415_UNSUPPORTED_MEDIA_TYPE, "content type must be JSONL")
        try:
            session = parse_session(await _read_body_limited(request, settings.max_body_bytes))
        except TelemetryValidationError as exc:
            raise HTTPException(status.HTTP_422_UNPROCESSABLE_CONTENT, str(exc)) from exc
        # Persist the immutable raw bytes first; this is acceptable because a crash
        # here is recovered by a later import that re-indexes from the same bytes.
        store_raw(settings.raw_dir, session)
        # Idempotency is decided by the index, not the raw file: a fresh index import
        # (even when the raw file already existed from an earlier crashed attempt) is a
        # new, non-idempotent import; a pre-existing index entry is a true duplicate.
        created_index = store.import_session(session)
        idempotent = not created_index
        return JSONResponse({"digest": session.digest, "session_id": session.session_id, "idempotent": idempotent}, status_code=200 if idempotent else 201)

    return app


app = create_app
