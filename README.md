# MoniDash

Mod Geode local para coletar telemetria de Geometry Dash 2.2081 para análise
posterior conduzida por um companheiro de IA. O contrato JSONL permanece
append-only: uma linha JSON por evento. `session_started` abre a sessão e uma
única linha final `session_ended` a fecha. O mod continua offline; a
sincronização é feita fora dele por um relay separado.

**Status:** o MVP narrow v0.1.0 (sessões em pasta com manifest + snapshot do
Death Tracker) foi validado no Windows. O v0.2.0 substitui esse contrato pela
telemetria rica (schema 2.0.0) e adiciona o pipeline privado relay → hub.

## Eventos do mod

- `session_started` e `session_ended`: limite de uma sessão e metadados conhecidos do nível.
- `attempt_started` e `attempt_ended`: tentativa, intervalo espacial e resultado.
- `gameplay_event`: input, interação com objeto ou mudança observável do estado do jogador.
- `death_context`: snapshot fatal (incluindo modo, mini, mirror, gravidade e velocidade) mais os cinco segundos precedentes de eventos.
- `copy_level_link`: começa como `needs_confirmation`; o mod não adivinha vínculo oficial.
- `reference_run_saved`: referência de treino ativa para uma cópia.

Campos factuais permanecem separados de inferências; classificações desconhecidas ficam `unknown`.

## Por que dois dados?

Death Tracker continua sendo a fonte macro (mortes agrupadas por percentual).
MoniDash fornece o micro-contexto de cada morte: modo, mini, espelhamento,
velocidade, posição, objeto fatal e os eventos precedentes. Os papéis são
separados: MoniDash não modifica nem duplica os registros do Death Tracker.

## Local-first privacy

- Não há upload de telemetria feito pelo mod; ele permanece offline.
- O relay só envia sessões finalizadas (`session_ended`) e nunca altera os JSONLs fonte.
- O hub é privado e valida estritamente cada sessão antes de indexá-la.

## Build do mod

1. Instale Geode SDK/binários (`geode sdk install` e `geode sdk install-binaries`).
2. Execute `geode build` no diretório do projeto.

O pacote esperado é `build-ninja\migas.monidash.geode`.

## Sync MVP (relay → hub)

O hub Python aceita somente JSONL UTF-8 completo e estrito: cada linha precisa ter
`schema_version`, `event_type`, `timestamp_ms` e `monotonic_seconds`; exatamente
um `session_started`; exatamente um `session_ended` como evento final; e IDs de
sessão coerentes. Ele grava bytes originais uma vez em `raw/<sha256>.jsonl` e
indexa resumos em SQLite transacionalmente. Reenvios do mesmo SHA-256 são
idempotentes.

### Hub privado

Copie `services/.env.example` para um arquivo de ambiente local e preencha um
token real fora do Git. Para desenvolvimento:

```sh
cd services
uv run --with-requirements requirements.txt uvicorn monidash_hub.api:create_app --factory --host 127.0.0.1 --port 8787
```

`POST /v1/sessions` requer `Authorization: Bearer <token>` e
`Content-Type: application/x-ndjson` ou `application/jsonl`. `GET /healthz` não
requer autenticação. O binding padrão é loopback; publicação por proxy/VPN é
decisão de deploy.

### Relay Windows

Configure variáveis de `relay/.env.example` localmente e execute
`python relay/monidash_relay.py`. O relay usa polling, só envia um arquivo cujo
último evento é `session_ended`, nunca altera os JSONLs fonte e guarda somente
SHA-256 reconhecidos em seu estado local. Erros transitórios (rede, 5xx, 408/429)
não marcam o arquivo como reconhecido; rejeições permanentes 4xx são registradas
como `rejected`.

### MCP local read-only

O servidor MCP stdio fala JSON-RPC 2.0 e oferece `latest_session`,
`session_summary`, `death_clusters`, `reference_runs` e `death_context`; não tem
ferramentas de mutação nem expõe caminhos de arquivos. Execute no VPS com
`PYTHONPATH=services python -m monidash_hub.mcp_server` e o mesmo ambiente de
dados do hub.

## Testes

```sh
UV_CACHE_DIR=/tmp/monidash-uv-cache uv run --with-requirements services/requirements.txt pytest services/tests relay/tests -q
```

As fixtures `tests/fixtures/*.jsonl` contêm sessões finalizadas válidas e podem
ser validadas no Windows com `tests/validate-jsonl.ps1`.

## Distribution and updates

MoniDash não será publicado no Geode Index e não oferecerá atualizações
automáticas baseadas em GitHub. GitHub permanece como plataforma de
source-control e revisão. Builds, instalações e atualizações são coordenados
manualmente.
