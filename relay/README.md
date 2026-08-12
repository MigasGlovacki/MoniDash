# MoniDash polling relay

A Windows-compatible standard-library polling relay. Configure environment variables from `.env.example`, then run `python monidash_relay.py`. It reads only finalized JSONL files (last event `session_ended`), leaves source files unchanged, and records acknowledged SHA-256 values in its local state file.

Upload outcomes: successful 2xx uploads are acknowledged; transient failures (network errors, 5xx, 408/429) are retried on a later poll; permanent 4xx validation rejections are recorded as `rejected` and never retried. The main loop tolerates transient filesystem/state errors and keeps running.
