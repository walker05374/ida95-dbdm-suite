# Relatório Técnico de Engenharia de Sistemas & Build — 11/11/2026 (`2026-11-11`)

**Projeto:** `ida95-dbdm-suite` — Suite de Telemetria, Diagnóstico e Overlay 3D para Dead by Daylight Mobile (`com.netease.dbdena`, Build `292949`)  
**Arquitetura Alvo:** ELF64 AArch64 (`arm64-v8a`) | Unreal Engine 4.27 (`libUE4.so`)  
**Padrão de Compilação:** C++20 (`-std=c++20 -O3 -fno-exceptions -fno-rtti -fPIC`) | Android NDK Clang 18.0.3  
**Data:** 11 de Novembro de 2026 (`2026-11-11`)

---

## 1. Resumo Executivo

Nesta sessão de engenharia foram concluídas quatro frentes críticas para estabilização de memória, precisão matemática de projeção 3D e fidelidade de telemetria de entidades em partida:

1. **Extração e Formatação de Progresso de Geradores (`%`):**
   - Implementada leitura direta e segura do progresso de reparo (`0.0f` a `100.0f`) a partir do offset de componente/interação (`+0x0338` / `UChargeableComponent`), normalizando razões (`0.0f..1.0f`), unidades de carga (`0.0f..90.0f`) e porcentagens diretas (`0.0f..100.0f`).
   - Formatação padronizada como `"gerador %.0f%% [%dm]"` na cor Verde (`#2ECC71`), com filtro e ocultação automática assim que o progresso atinge ou ultrapassa `99.5%` (`>= 99.5f`).

2. **Restauração de Totens (`Shrine`) no Array de Telemetria:**
   - Corrigida a causa-raiz que descartava Totens (`Shrine`) e Pallets (`Barrier`) durante a sincronização (`progress_pct` era inicializado como `-1.0f` para atores que não fossem geradores, fazendo com que `telemetry_adapter.hpp` e `imgui_gles3_hud.cpp` os interpretassem como destruídos/purificados).
   - Corrigido o filtro de `UClass` em `classify_actor_category` (`entry.cpp`) para não rejeitar `HexSpawner`/`Totem` na verificação genérica de `"Spawner"`.
   - Nós classificados como `Shrine` (Totens) agora são preservados com `progress_pct >= 0.0f`, renderizando `"totem [%dm]"` em Amarelo (`#F1C40F`) até que flags de interação ou visibilidade confirmem a purificação (`progress_pct < 0.0f`).

3. **Integração do Pipeline Matemático 3D (`math3d_pipeline.hpp`) no HUD (`imgui_gles3_hud.cpp`):**
   - Integrado o cabeçalho `math3d_pipeline.hpp` ao renderizador OpenGL ES 3 / Dear ImGui (`imgui_gles3_hud.cpp`).
   - Substituída a lógica legada de esqueleto colapsado na origem e caixas estáticas manuais pela chamada a `math3d::integrate_math3d_into_existing_hud(...)`, alimentada com os descritores de atores (`Antagonist` em `#FF2A2A` e `Teammate` em `#00E5FF`), dimensões reais do viewport e estado da câmera (`math3d::CameraState`).

4. **Alocação em Buffers Estáticos (`kMaxSceneNodes = 256`), Estabilidade de RAM e Rebuild Limpo (`build_module.ps1`):**
   - Criado o pool estático alinhado a 64 bytes `DiagnosticTickStaticPool` (`kDiagnosticPoolCapacity = 256`) em `diagnostic_tick_bridge.hpp`, eliminando alocações dinâmicas ou pressão de pilha a cada tick de 16ms e prevenindo falhas durante transições de estado de gancho/jaula (`Hooked` / `Caged`).
   - Atualizado o script `build_module.ps1` (e `scripts/build_module.ps1`) com limpeza profunda de caches (`build-arm64/`, `app/build/`, `.gradle/`, `./gradlew clean`), recompilação completa sem avisos ou erros e rotina de deploy automatizado via ADB (`192.168.1.68:10001`).

---

## 2. Detalhamento Técnico por Arquivo Modificado

### 2.1. `jni/telemetry/scene_collector_helpers.hpp`
- **Constantes de Cor e Limiar de Conclusão:**
  - `kGeneratorCompleteThresholdPct = 99.5f`
  - `kGeneratorColorAbgr = 0xFF71CC2EU` / `kGeneratorColorArgb = 0xFF2ECC71U` (Verde `#2ECC71`)
  - `kTotemColorAbgr = 0xFF0FC4F1U` / `kTotemColorArgb = 0xFFF1C40FU` (Amarelo `#F1C40F`)
- **Refinamento de `classify_node_category(const char* class_name)`:**
  - Mapeia `"Generator"`, `"Gerador"` e `"PowerNode"` para `hud::NodeCategory::PowerNode`, excluindo geradores-fantasma e gerenciadores (`"LevelGenerator"`, `"GeneratorEntity"`, `"GeneratorDriven"`, `"Portal_Generator"`).
  - Mapeia `"Totem"`, `"Hex"`, `"Boon"`, `"HexSpawner"` e `"Shrine"` para `hud::NodeCategory::Shrine`, excluindo totens já purificados ou quebrados (`"Cleansed"`, `"Destroyed"`, `"Broken"`).
- **Novas Funções Auxiliares `constexpr`/`inline`:**
  - `normalize_generator_progress_pct(float raw_progress, float max_charge = 0.0f)`: converte valores brutos de reparo em porcentagem `[0.0f, 100.0f]`.
  - `should_hide_generator(float progress_pct)`: retorna `true` quando `!std::isfinite(progress_pct) || progress_pct < 0.0f || progress_pct >= 99.5f`.
  - `is_totem_active(float progress_pct, const char* label = nullptr)`: valida que `progress_pct >= 0.0f` e que o rótulo não indica purificação/destruição.
  - `format_generator_telemetry_label(...)`, `format_totem_telemetry_label(...)` e `format_node_telemetry_label(...)`: formatam `"gerador %.0f%% [%dm]"` e `"totem [%dm]"` sem alocação dinâmica.

### 2.2. `jni/core/entry.cpp`
- **Leitura de Progresso de Geradores (`SpatialAndKinematicPipeline::read_generator_progress_pct`):**
  - Inspeciona `actor_ptr + off.chargeable_current_charge` (`+0x0338`) suportando tanto ponteiro indireto para `UChargeableComponent*` validado via `validate_canonical_uobject` quanto float direto no ator, normalizando o resultado em `[0.0f, 100.0f]`.
  - Aplica `hud::adapter::should_hide_generator(gen_pct)` em `collect_nearby_entities_multi_level`, `execute_diagnostic_tick` e `nativePollOverlayMarkers`, descartando geradores concluídos (`>= 99.5%`).
- **Restauração de Totens (`SpatialAndKinematicPipeline::read_totem_state_progress`):**
  - Verifica se o ator `Totem`/`Shrine` e seu `RootComponent` estão vivos e visíveis (`scene_comp_flags` em `+0x0164`: se `bHiddenInGame` `0x1000` estiver ativo sem `bVisible` `0x0040`, retorna `-1.0f`).
  - Monitora o progresso de purificação no offset de interação (`+0x0338`): retorna `>= 0.0f` enquanto o totem está de pé/ativo e `-1.0f` (`< 0.0f`) assim que purificado (`>= 99.5%`).
- **Pool Estático no Loop de Tick (`execute_diagnostic_tick`):**
  - Substituídos os arrays locais de pilha por `static std::array<ue4::EntitySnapshot, hud::kMaxSceneNodes> s_snapshots_pool` e `hud::adapter::get_diagnostic_tick_pool()`.
  - Adicionada validação `hud::is_immobilize_transition_safe` em `apply_immobilize_state` para proteger transições de estado de gancho/jaula (`EImmobilizeState` em `+0x1158`).

### 2.3. `jni/telemetry/diagnostic_tick_bridge.hpp`
- **Pool Estático Pré-Alocado (`DiagnosticTickStaticPool`):**
  - Estrutura `alignas(64) DiagnosticTickStaticPool` com capacidade fixa `kDiagnosticPoolCapacity = hud::kMaxSceneNodes = 256`, contendo `samples` (`std::array<RawEntitySample, 256>`) e `label_storage` (`std::array<std::array<char, 64>, 256>`).
  - Função `push_static_entity_sample(...)` para inserção com cópia segura de string sem ponteiros pendentes (`dangling pointers`) e zero alocações no heap.
  - Função `is_immobilize_transition_safe(...)` para garantir estabilidade durante animações de gancho (`Hooked`) e jaula (`Caged`).

### 2.4. `jni/hud/imgui_gles3_hud.cpp`
- **Integração com `math3d_pipeline.hpp`:**
  - Adicionado `#include "math3d_pipeline.hpp"`.
  - Em `render_hud_frame(float width, float height)`, os atores `Antagonist` (`#FF2A2A`) e `Teammate` (`#00E5FF`) são coletados nos buffers estáticos `s_math3d_actors` e `s_math3d_labels` (`kMaxSceneNodes = 256`) e renderizados via `math3d::integrate_math3d_into_existing_hud(...)` usando `math3d::CameraState` e as dimensões do viewport (`width`, `height`).

### 2.5. `build_module.ps1` & `scripts/build_module.ps1`
- **Etapa `[0/6]` — Limpeza Profunda (`Deep Clean`):**
  - Remove `build-arm64/`, `app/build/`, `app/.gradle/` e `modulos/floating_mod_menu_apk/build/`, executando `./gradlew clean` quando presente.
- **Etapa `[1/6]` a `[5/6]` — Compilação C++20 e Empacotamento:**
  - Compila `libinstrument_core.so`, `dbdm_telemetry_daemon`, `libdbdm_overlay_ui.so` e `libue4_diagnostics.so` via CMake + Ninja (`--clean-first --parallel`), remove símbolos desnecessários via `llvm-strip` e gera `modulos/ue4_instrument_zygisk.zip` e `modulos/DBDM_FloatingModMenu.apk`.
- **Etapa `[6/6]` — Deploy ADB (`192.168.1.68:10001`):**
  - Executa `adb connect 192.168.1.68:10001`, instala o módulo Zygisk e o APK standalone, concede permissão de sobreposição (`appops set com.dbdm.modmenu SYSTEM_ALERT_WINDOW allow`), inicia `com.dbdm.modmenu/.MainActivity` e coleta os logs de inicialização via `logcat`.

---

## 3. Artefatos Gerados e Verificados

| Artefato | Caminho | Status |
| :--- | :--- | :--- |
| Biblioteca Core Zygisk / JNI | `build-arm64/libinstrument_core.so` | Compilado e Stripped (0 erros / 0 avisos) |
| Daemon de Telemetria | `build-arm64/dbdm_telemetry_daemon` | Compilado e Stripped (0 erros / 0 avisos) |
| Biblioteca de Overlay GLES3 / ImGui | `build-arm64/libdbdm_overlay_ui.so` | Compilado e Stripped (0 erros / 0 avisos) |
| Biblioteca Unificada de Diagnóstico | `build-arm64/libue4_diagnostics.so` | Compilado e Stripped (0 erros / 0 avisos) |
| Pacote Módulo Magisk/Zygisk | `modulos/ue4_instrument_zygisk.zip` | Empacotado com separadores POSIX (`/`) |
| APK Standalone Floating Mod Menu | `modulos/DBDM_FloatingModMenu.apk` | Alinhado (`zipalign`) e Assinado (`apksigner`) |

