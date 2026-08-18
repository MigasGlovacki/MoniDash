# Changelog

## Unreleased

- `death_context` agora registra `death_percent`: a porcentagem da morte
  calculada com a mesma fórmula do Death Tracker v3.0.9 (posição do player 1 ÷
  comprimento do nível, com fallback por tempo no timestamp da música),
  clampada em `[0, 100]`; `null` quando não houver base confiável.
- O hub indexa `death_percent` na tabela `deaths` (com migração automática de
  bancos existentes) e a tool MCP `death_context` passa a devolvê-lo.

## v0.2.0

- Substitui o contrato MVP (schema 1, sessões em pasta + manifest) pela
  telemetria rica schema 2.0.0: `session_started/ended` com metadados do nível,
  `attempt_started/ended`, `gameplay_event`, `death_context` (com modo, mini,
  mirror, gravidade, velocidade, objeto fatal e 5s de eventos precedentes),
  `copy_level_link` e `reference_run_saved`.
- Adiciona o pipeline privado: relay Windows (polling, somente sessões
  finalizadas) e hub (FastAPI + SQLite + MCP read-only).
- Adiciona o ajuste `capture-enabled` e o índice `telemetry/active-references/`.
- Adiciona filtro de dificuldade configurável no menu do Geode (`Apenas Demons`
  e `Mínimo 9 estrelas`); fases fora do filtro não geram arquivo local.
- `session_started` agora carrega `local_date`, `stars` e `is_demon`;
  o hub organiza as sessões em `sessions/<data>/<fase>/`.
- Cópias de treino (`Fase SP`/`Start Position`) são sempre capturadas e
  agrupadas na pasta da fase oficial.
- Novo evento `death_tracker_snapshot` no fim da sessão (leitura read-only do
  Death Tracker: attempts, newBest, realEndPercent, difficulty, general.dt).
- Classificação de mortes ampliada: objetos desconhecidos ganham tipo do GD
  (hazard/block/slope) com confiança baixa; `object_type` cru fica no payload.
- O validador do hub aceita as classificações de morte `hazard` e `slope`
  (alinhado ao que o mod grava; antes, sessões com essas mortes eram rejeitadas
  com 422).
- Hub com watchdog via cron (reinício automático se cair), independente da
  sessão do Hermes.
- O pacote continua sendo `migas.monidash` (v0.2.0), atualizando o mod
  instalado em vez de criar um mod paralelo.

## v0.1.0

- Added the narrow Windows-validated local MVP: session start, `level_started`,
  minimal player death, normal-exit `session_ended`, and a read-only exact-key
  conditional snapshot of Death Tracker metadata/general.dt.
- Validation is limited to the observed flow, not all level types, duplicate or
  dual play, causes or richer telemetry, analysis, networking, releases, or PR
  merging.
