from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    ingest_token: str
    data_dir: Path
    host: str = "127.0.0.1"
    port: int = 8787
    max_body_bytes: int = 1_048_576

    @classmethod
    def from_environment(cls) -> Settings:
        token = os.environ.get("MONIDASH_INGEST_TOKEN")
        if not token:
            raise ValueError("MONIDASH_INGEST_TOKEN must be set")
        return cls(
            token,
            Path(os.environ.get("MONIDASH_DATA_DIR", "./monidash-data")),
            os.environ.get("MONIDASH_HOST", "127.0.0.1"),
            int(os.environ.get("MONIDASH_PORT", "8787")),
            int(os.environ.get("MONIDASH_MAX_BODY_BYTES", "1048576")),
        )

    @property
    def raw_dir(self) -> Path: return self.data_dir / "raw"
    @property
    def database(self) -> Path: return self.data_dir / "monidash.sqlite3"
