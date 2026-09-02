import pytest
from monidash_hub.config import Settings


def test_settings_require_token_and_take_paths_from_environment(tmp_path, monkeypatch):
    monkeypatch.delenv("MONIDASH_INGEST_TOKEN", raising=False)
    with pytest.raises(ValueError, match="MONIDASH_INGEST_TOKEN"):
        Settings.from_environment()
    monkeypatch.setenv("MONIDASH_INGEST_TOKEN", "test-token")
    monkeypatch.setenv("MONIDASH_DATA_DIR", str(tmp_path))
    settings = Settings.from_environment()
    assert settings.data_dir == tmp_path
    assert settings.host == "127.0.0.1"


def test_settings_configure_a_positive_maximum_ingest_body_size(monkeypatch):
    monkeypatch.setenv("MONIDASH_INGEST_TOKEN", "test-token")
    monkeypatch.setenv("MONIDASH_MAX_BODY_BYTES", "1234")

    assert Settings.from_environment().max_body_bytes == 1234
