# MoniDash Telemetry — Agent Guide

## Objetivo do projeto

Este é um mod Geode para Geometry Dash 2.2081. Ele coleta telemetria local para análise posterior por agentes de IA; não deve alterar gameplay, saves, progresso, recordes, objetos do nível ou enviar dados pela rede.

## Regras de implementação

- Preserve o contrato JSONL versionado em `src/main.cpp`. Dados observados e inferências devem permanecer separados.
- Nunca invente uma causa de morte: quando o objeto ou classificação não for identificável, use `null` ou `unknown` com a confiança apropriada.
- Nunca associe uma cópia/start position à fase oficial sem evidência. Use `link_status: "needs_confirmation"` enquanto a ligação não for confiável.
- Sessões JSONL são append-only e nunca podem ser sobrescritas. Apenas o índice em `telemetry/active-references/` pode substituir a referência ativa de um trecho.
- Mantenha dual suportado e não adicione suporte a platformer ou two-player sem ampliar o esquema e os testes.
- Isole hooks Geode em modificações de classes existentes. Antes de adicionar um hook, confirme a assinatura nos bindings da versão 2.2081.

## Build e testes

- SDK Geode: `C:\Users\migas\Documents\Geode`.
- Versão alvo: Geode `5.8.2`, Geometry Dash `2.2081`, Windows x64.
- O build local usa Ninja e MSVC. Configure com `CMAKE_CXX_FLAGS=/utf-8`; o `CMakeLists.txt` já aplica isso quando necessário.
- Valide fixtures com:

  `powershell -ExecutionPolicy Bypass -File tests\validate-jsonl.ps1 -Path tests\fixtures\sample-session.jsonl`

- O pacote esperado é `build-ninja\migas.monidash.geode`.

## Mudanças de telemetria

Ao acrescentar campos ou eventos:

1. Atualize `schema_version` se a alteração quebrar consumidores existentes.
2. Atualize o `README.md` com a semântica do novo campo/evento.
3. Adicione ou atualize uma fixture JSONL e execute o validador.
4. Mantenha escrita incremental e tolerante a encerramento inesperado.

## Verificação manual

Antes de considerar uma mudança pronta, teste no Geometry Dash ao menos: nível normal, prática, dual, troca de modo/gravidade/velocidade, morte e conclusão de cópia com start position. Confirme que o JSONL produzido continua parseável.
