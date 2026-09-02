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

- `session_started` e `session_ended`: limite de uma sessão e metadados conhecidos do nível (incluindo `local_date`, `stars`, `is_demon` e `mirror_mode`). A dificuldade exata do demon (Easy/Medium/Hard/Insane/Extreme) não é lida do nível no início da sessão — o campo não é populado de forma confiável — e é determinada na análise por fonte verificável.
- `attempt_started` e `attempt_ended`: tentativa, intervalo espacial e resultado.
- `gameplay_event`: input, interação com objeto ou mudança observável do estado do jogador.
- `death_context`: snapshot fatal (incluindo modo, mini, mirror, gravidade e velocidade) mais os cinco segundos precedentes de eventos. Cada morte agora carrega `death_percent`, a porcentagem calculada com a mesma fórmula do Death Tracker (posição do player 1 ÷ comprimento do nível, com fallback por tempo no timestamp da música), clampada em `[0, 100]`; fica `null` quando não há base confiável para calcular.
- `copy_level_link`: começa como `needs_confirmation`; o mod não adivinha vínculo oficial.
- `reference_run_saved`: referência de treino ativa para uma cópia.
- `death_tracker_snapshot`: no fechamento da sessão, o mod lê (read-only) os dados do Death Tracker do nível — `attempts`, `new_best_percent` (último newBest), `real_end_percent`, `difficulty` e o `general.dt` bruto — para a análise ter a porcentagem real.

Campos factuais permanecem separados de inferências; classificações desconhecidas ficam `unknown`. Objetos não reconhecidos pela tabela de IDs ganham classificação por tipo do GD (hazard/block/slope) com confiança baixa, mantendo o `object_type` cru no payload.

## Filtro de dificuldade

O mod tem ajustes no menu do Geode (Settings):

- `Capture telemetry` — liga/desliga a captura.
- `Apenas Demons` (padrão: ligado) — só grava sessões de fases Demon.
- `Mínimo 9 estrelas` (padrão: desligado) — só grava sessões com 9★ ou mais.

Com os dois desligados, todas as fases são gravadas. Com qualquer um ligado, fases fora do filtro **não geram arquivo local** — não poluem eventos nem chegam ao hub. Se você jogar uma fase leve depois de uma elegível, a fase leve simplesmente não é registrada.

Cópias de treino salvas no editor (`Fase SP` / `Fase Start Position`) são **sempre capturadas**, mesmo com os filtros ligados: elas não carregam o rating de demon do servidor, e o objetivo é justamente registrar o treino. No hub, elas são agrupadas na pasta da fase oficial.

## Por que dois dados?

Death Tracker continua sendo a fonte macro (mortes agrupadas por percentual).
MoniDash fornece o micro-contexto de cada morte: porcentagem, modo, mini,
espelhamento, velocidade, posição, objeto fatal e os eventos precedentes. Os
papéis são separados: MoniDash não modifica nem duplica os registros do Death
Tracker.

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
sessão coerentes. Ele grava bytes originais uma vez em `raw/<sha256>.jsonl`,
indexa resumos em SQLite transacionalmente e cria uma cópia organizada em
`sessions/<data-local>/<nome-da-fase>/<sha256>.jsonl` (data local do jogador,
nome da fase legível). Cópias de treino salvas no editor com o sufixo
`SP`/`Start Position` (ex.: `Stereo Madness SP`) são agrupadas na pasta da fase
oficial. Reenvios do mesmo SHA-256 são idempotentes.

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
ferramentas de mutação nem expõe caminhos de arquivos. O `death_context` inclui
`death_percent` de cada morte (mesma base do Death Tracker). Execute no VPS com
`PYTHONPATH=services python -m monidash_hub.mcp_server` e o mesmo ambiente de
dados do hub.

## Enriquecimento de fases (esboço)

`services/monidash_hub/enrich.py` enriquece uma sessão validada com metadados
oficiais das fases usando a API pública do **GDBrowser**
(`https://gdbrowser.com/api`). A saída separa sempre o que o mod observou
(`observed`) do que a API informou (`registry`) — nenhum dado externo é
misturado com a telemetria.

### O que a API oferece (validado ao vivo)

- `GET /api/level/<id>` — detalhe completo da fase: nome oficial, criador,
  dificuldade (`Easy Demon`, `Extreme Demon`, etc.), estrelas, length, coins,
  música, featured/epic, downloads/likes. **Funciona de forma confiável.**
- `GET /api/search/<nome>?type=search` — busca textual por nome. **Degradada no
  momento**: retorna a mesma lista fixa independente da query; não dependa dela.
- Rate limit: `150` requisições/hora por IP (headers `x-ratelimit-*`); CORS
  aberto (`*`); JSON puro.
- O autor avisa que a API é "slow, unreliable" e pode ser **removida no
  futuro** — por isso o cache em disco e o modo offline são parte do esboço,
  não enfeite.

### Como o esboço funciona

1. `parse_session` valida o JSONL (mesmo contrato do hub).
2. `_session_stats` agrega métricas da sessão: `local_date`, tentativas, mortes,
   treinos de SP e duração (sem inferir causa).
3. `extract_level_ids` coleta IDs únicos do `session_started.level.id` e de
   `copy_level_link.copy_level_id`; IDs `0` (fase sem ID oficial) são ignorados.
4. `LevelClient` consulta `api/level/<id>` com **cache em disco**
   (`enrich-cache.json`), intervalo mínimo entre requisições e modo `--offline`
   que nunca toca a rede.
5. `render_markdown` gera uma **entrada de diário** pronta para o Obsidian;
   `--out` também grava o JSON estruturado.

### Saída de exemplo

```markdown
### Sessão — 2026-08-12
- Fonte: `sample-live-enrich.jsonl` | Sessão: `session-live-test`
- Duração: 20s · 2 tentativas · 1 mortes · 1 treino(s) de SP

**Bloodbath** (Extreme Demon · 10★) — por Riot
- Melhor marca: **47%**, 231 tentativas
- Length: Long | Música: At the Speed of Light por Dimrain47
- Link: https://gdbrowser.com/level/10565740

**ID 999999999** — `999999999` ⚠️ sem registro na API
- ⚠️ Sem registro na API (http_500) — fase local/cópia não encontrada
- Treino: 1 trecho(s) com start position — vínculo oficial permanece `needs_confirmation`
```

A **melhor marca** e as tentativas vêm do `death_tracker_snapshot` (fonte
read-only do Death Tracker), não da API — a API só adiciona o contexto oficial
da fase (dificuldade, estrelas, criador, música).

### Uso

```sh
PYTHONPATH=services python -m monidash_hub.enrich <sessao.jsonl> --out <dir> [--offline] [--refresh] [--cache <path>]
```

- `--offline`: usa apenas o cache (útil quando a API estiver fora).
- `--refresh`: ignora o cache e re-consulta.
- `--cache`: caminho do cache (padrão `enrich-cache.json` no diretório atual).

### Comportamentos de borda

- Fase inexistente / erro HTTP → a API devolve `-1` ou 5xx; o esboço grava
  `{"error": ...}` no cache e o Markdown mostra `⚠️ Sem registro na API` em vez
  de inventar dificuldade.
- Trecho de treino (`copy_level_link`) → contado como `training_copies` no
  bloco `observed`; o vínculo com a fase oficial permanece `needs_confirmation`,
  nunca é promovido pelo enricher.
- Fase local/salva com ID fora do servidor → aviso, com os dados observados
  preservados.

Testes em `services/tests/test_enrich.py` (offline, sem rede) + fixture ao vivo
`tests/fixtures/sample-live-enrich.jsonl` para smoke test manual.

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
