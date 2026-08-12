# Changelog

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
- O pacote continua sendo `migas.monidash` (v0.2.0), atualizando o mod
  instalado em vez de criar um mod paralelo.

## v0.1.0

- Added the narrow Windows-validated local MVP: session start, `level_started`,
  minimal player death, normal-exit `session_ended`, and a read-only exact-key
  conditional snapshot of Death Tracker metadata/general.dt.
- Validation is limited to the observed flow, not all level types, duplicate or
  dual play, causes or richer telemetry, analysis, networking, releases, or PR
  merging.
