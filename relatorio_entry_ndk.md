# Relatório de Implementação Final: `entry.cpp` Integrado e Build Android NDK (`libinstrument_core.so`)

Este documento preserva o código original de `entry.cpp` e os comandos de build/verificação (`CMake` + `Ninja` + `llvm-nm` / `llvm-readelf`), seguidos da **auditoria de atualização e reconciliação de offsets** contra a verdade de base provada na **Rev. 7 (`RELATORIO_REV7_COMPLETO.md`)** e no relatório principal (`relatorio.md`).

---

## 1. Implementação Unificada (`entry.cpp`) — Versão Base Enviada

```cpp
// entry.cpp
#include "visibility.hpp"
#include "elf_resolver.hpp"
#include "in_process_memory.hpp"
#include <jni.h>
#include <pthread.h>
#include <time.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace runtime {
using memory::InProcessMemory;
using memory::Status;

// ============================================================================
// 1. Unreal Engine 4.27 Layout Primitives & Offsets Configuration
// ============================================================================
namespace ue4 {
struct INSTRUMENT_LOCAL FVector {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    [[nodiscard]] constexpr float dist_squared(const FVector& other) const noexcept {
        const float dx = x - other.x;
        const float dy = y - other.y;
        const float dz = z - other.z;
        return (dx * dx) + (dy * dy) + (dz * dz);
    }
};

struct INSTRUMENT_LOCAL TArrayHeader {
    uintptr_t data_ptr{0};
    int32_t   count{0};
    int32_t   max{0};
};

struct INSTRUMENT_LOCAL EngineOffsets {
    uintptr_t gworld_rva                = 0x08A4F120;
    uintptr_t gnamepool_rva             = 0x088C2400;
    // UWorld
    uintptr_t persistent_level          = 0x0030;
    uintptr_t owning_game_instance      = 0x0180;
    // ULevel
    uintptr_t actor_array               = 0x0098;
    // UGameInstance -> LocalPlayers
    uintptr_t local_players_array       = 0x0038;
    uintptr_t player_controller         = 0x0030;
    uintptr_t acknowledged_pawn         = 0x02A8;
    uintptr_t player_state              = 0x0240;
    // AActor / APawn
    uintptr_t fname_index               = 0x0018;
    uintptr_t root_component            = 0x0130;
    uintptr_t character_movement        = 0x0288;
    // USceneComponent
    uintptr_t relative_location         = 0x011C;
    uintptr_t component_velocity        = 0x0140;
    // UCharacterMovementComponent
    uintptr_t max_walk_speed            = 0x018C;
    uintptr_t max_acceleration          = 0x0198;
    // APlayerState
    uintptr_t player_name_private       = 0x0300; // FString (TArray<char16_t>)
};

// ============================================================================
// 2. GNamePool Safe Decoder (UE 4.23+ FNamePool Block Architecture)
// ============================================================================
class INSTRUMENT_LOCAL GNamePoolResolver {
public:
    static constexpr uint32_t  kBlockBitShift = 16U;
    static constexpr uint32_t  kOffsetMask    = (1U << kBlockBitShift) - 1U;
    static constexpr uint32_t  kMaxBlocks     = 8192U;
    static constexpr size_t    kMaxNameLen    = 128U;
    static constexpr uintptr_t kBlocksOffset  = 0x0010U;
    static constexpr uint32_t  kStride        = 2U;

    explicit constexpr GNamePoolResolver(uintptr_t pool_base) noexcept
        : pool_base_(pool_base) {}

    [[nodiscard]] bool resolve_name(uint32_t comparison_index, std::span<char> out_buffer) const noexcept {
        if (pool_base_ == 0 || out_buffer.size() < 2U) {
            return false;
        }
        const uint32_t block_idx = comparison_index >> kBlockBitShift;
        const uint32_t byte_off  = (comparison_index & kOffsetMask) * kStride;
        if (block_idx >= kMaxBlocks) {
            return false;
        }
        const uintptr_t block_ptr_addr =
            pool_base_ + kBlocksOffset + (static_cast<uintptr_t>(block_idx) * sizeof(uintptr_t));
        const auto block_base = mem::read<uintptr_t>(block_ptr_addr);
        if (!block_base.has_value() || *block_base == 0) {
            return false;
        }
        const uintptr_t entry_addr = *block_base + byte_off;
        const auto header = mem::read<uint16_t>(entry_addr);
        if (!header.has_value()) {
            return false;
        }
        const bool   is_wide = (*header & 1U) != 0;
        const size_t len     = static_cast<size_t>(*header >> 6U);
        if (is_wide || len == 0 || len >= kMaxNameLen || len >= out_buffer.size()) {
            return false;
        }
        std::span<uint8_t> str_slice{reinterpret_cast<uint8_t*>(out_buffer.data()), len};
        if (InProcessMemory::safe_read(entry_addr + sizeof(uint16_t), str_slice) != Status::Ok) {
            return false;
        }
        out_buffer[len] = '\0';
        return true;
    }
private:
    uintptr_t pool_base_{0};
};

// ============================================================================
// 3. Spatial Entity Filter & Kinematic Tuning Module
// ============================================================================
struct INSTRUMENT_LOCAL EntitySnapshot {
    uintptr_t actor_address{0};
    uint32_t  fname_id{0};
    FVector   world_pos{};
    float     distance_meters{0.0f};
};

class INSTRUMENT_LOCAL SpatialAndKinematicPipeline {
public:
    static constexpr size_t kMaxActorsPerTick = 2048U;
    static constexpr size_t kMaxSnapshots     = 64U;

    [[nodiscard]] static std::optional<FVector> read_actor_location(
        uintptr_t actor,
        const EngineOffsets& off
    ) noexcept {
        if (actor == 0) {
            return std::nullopt;
        }
        const auto root_comp = mem::read<uintptr_t>(actor + off.root_component);
        if (!root_comp.has_value() || *root_comp == 0) {
            return std::nullopt;
        }
        return mem::read<FVector>(*root_comp + off.relative_location);
    }

    static Status apply_kinematic_limits(
        uintptr_t local_pawn,
        const EngineOffsets& off,
        float target_walk_speed,
        float target_accel
    ) noexcept {
        if (local_pawn == 0) {
            return Status::InvalidArgument;
        }
        const auto move_comp = mem::read<uintptr_t>(local_pawn + off.character_movement);
        if (!move_comp.has_value() || *move_comp == 0) {
            return Status::UnmappedOrFault;
        }
        if (target_walk_speed > 0.0f && target_walk_speed <= 5000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.max_walk_speed, target_walk_speed);
            if (st != Status::Ok) {
                return st;
            }
        }
        if (target_accel > 0.0f && target_accel <= 10000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.max_acceleration, target_accel);
            if (st != Status::Ok) {
                return st;
            }
        }
        return Status::Ok;
    }

    static size_t collect_nearby_entities(
        uintptr_t ulevel_addr,
        const FVector& origin,
        float max_radius_meters,
        const EngineOffsets& off,
        const GNamePoolResolver& name_resolver,
        std::span<EntitySnapshot> out_snapshots
    ) noexcept {
        if (ulevel_addr == 0 || out_snapshots.empty()) {
            return 0;
        }
        const auto actor_arr = mem::read<TArrayHeader>(ulevel_addr + off.actor_array);
        if (!actor_arr.has_value() || actor_arr->data_ptr == 0 || actor_arr->count <= 0) {
            return 0;
        }
        const size_t raw_count     = static_cast<size_t>(actor_arr->count);
        const size_t bounded_count = (raw_count > kMaxActorsPerTick) ? kMaxActorsPerTick : raw_count;
        const float max_dist_cm = max_radius_meters * 100.0f;
        const float max_dist_sq = max_dist_cm * max_dist_cm;
        size_t collected = 0;
        std::array<char, 128> name_buf{};
        for (size_t i = 0; i < bounded_count && collected < out_snapshots.size(); ++i) {
            const uintptr_t slot_addr = actor_arr->data_ptr + (i * sizeof(uintptr_t));
            const auto actor_ptr = mem::read<uintptr_t>(slot_addr);
            if (!actor_ptr.has_value() || *actor_ptr == 0) {
                continue;
            }
            const auto fname_idx = mem::read<uint32_t>(*actor_ptr + off.fname_index);
            if (!fname_idx.has_value() || *fname_idx == 0) {
                continue;
            }
            if (!name_resolver.resolve_name(*fname_idx, name_buf)) {
                continue;
            }
            const auto pos = read_actor_location(*actor_ptr, off);
            if (!pos.has_value()) {
                continue;
            }
            const float dist_sq = origin.dist_squared(*pos);
            if (dist_sq <= max_dist_sq) {
                out_snapshots[collected++] = EntitySnapshot{
                    .actor_address   = *actor_ptr,
                    .fname_id        = *fname_idx,
                    .world_pos       = *pos,
                    .distance_meters = std::sqrt(dist_sq) * 0.01f
                };
            }
        }
        return collected;
    }
};

// ============================================================================
// 4. LocalPlayerNameController (Bounded In-Place UTF-16 FString Override)
// ============================================================================
class INSTRUMENT_LOCAL LocalPlayerNameController {
public:
    static Status override_local_name(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::u16string_view new_name
    ) noexcept {
        if (player_state_addr == 0 || new_name.empty()) {
            return Status::InvalidArgument;
        }
        const uintptr_t fstring_addr = player_state_addr + off.player_name_private;
        const auto fstr = mem::read<TArrayHeader>(fstring_addr);
        if (!fstr.has_value() || fstr->data_ptr == 0 || fstr->max <= 1) {
            return Status::UnmappedOrFault;
        }
        uint8_t probe = 0;
        if (InProcessMemory::safe_read(fstr->data_ptr, std::span<uint8_t>{&probe, 1}) != Status::Ok) {
            return Status::UnmappedOrFault;
        }
        const size_t max_chars   = static_cast<size_t>(fstr->max) - 1U;
        const size_t desired_len = (new_name.size() < max_chars) ? new_name.size() : max_chars;
        std::array<char16_t, 64> staging{};
        const size_t clamped_len = (desired_len < (staging.size() - 1U)) ? desired_len : (staging.size() - 1U);
        std::memcpy(staging.data(), new_name.data(), clamped_len * sizeof(char16_t));
        staging[clamped_len] = u'\0';
        const size_t byte_len = (clamped_len + 1U) * sizeof(char16_t);
        struct iovec local_iov {
            .iov_base = staging.data(),
            .iov_len  = byte_len
        };
        struct iovec remote_iov {
            .iov_base = reinterpret_cast<void*>(fstr->data_ptr),
            .iov_len  = byte_len
        };
        const ssize_t written = ::syscall(
            SYS_process_vm_writev,
            ::getpid(),
            &local_iov, 1UL,
            &remote_iov, 1UL,
            0UL
        );
        if (written < 0 || static_cast<size_t>(written) != byte_len) {
            return Status::PartialTransfer;
        }
        const int32_t new_count = static_cast<int32_t>(clamped_len + 1U);
        return mem::write_data<int32_t>(fstring_addr + offsetof(TArrayHeader, count), new_count);
    }
};
} // namespace ue4

// ============================================================================
// 5. Runtime Context & Background Telemetry Worker Thread
// ============================================================================
struct INSTRUMENT_LOCAL InstrumentationState {
    std::atomic<bool>  running{false};
    std::atomic<bool>  thread_active{false};
    pthread_t          worker_thread{};
    elf::ModuleSpan    ue4_module{};
    ue4::EngineOffsets offsets{};
};

INSTRUMENT_LOCAL static InstrumentationState g_state{};

INSTRUMENT_LOCAL inline void sleep_ms(uint32_t milliseconds) noexcept {
    struct timespec ts{};
    ts.tv_sec  = static_cast<time_t>(milliseconds / 1000U);
    ts.tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000UL);
    ::nanosleep(&ts, nullptr);
}

INSTRUMENT_LOCAL static void execute_diagnostic_tick() noexcept {
    const uintptr_t base = g_state.ue4_module.base_address;
    if (base == 0) {
        return;
    }
    const auto& off = g_state.offsets;
    // 1. Resolve UWorld pointer safely
    const auto uworld = mem::read<uintptr_t>(base + off.gworld_rva);
    if (!uworld.has_value() || *uworld == 0) {
        return;
    }
    // 2. Construct GNamePool decoder
    const ue4::GNamePoolResolver name_pool{base + off.gnamepool_rva};
    // 3. Traverse UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> AcknowledgedPawn
    uintptr_t    local_pawn      = 0;
    uintptr_t    local_player_st = 0;
    ue4::FVector local_origin{};
    const auto game_instance = mem::read<uintptr_t>(*uworld + off.owning_game_instance);
    if (game_instance.has_value() && *game_instance != 0) {
        const auto lp_array = mem::read<ue4::TArrayHeader>(*game_instance + off.local_players_array);
        if (lp_array.has_value() && lp_array->data_ptr != 0 && lp_array->count > 0) {
            const auto first_lp = mem::read<uintptr_t>(lp_array->data_ptr);
            if (first_lp.has_value() && *first_lp != 0) {
                const auto pc = mem::read<uintptr_t>(*first_lp + off.player_controller);
                if (pc.has_value() && *pc != 0) {
                    const auto pawn = mem::read<uintptr_t>(*pc + off.acknowledged_pawn);
                    if (pawn.has_value() && *pawn != 0) {
                        local_pawn = *pawn;
                        if (const auto loc = ue4::SpatialAndKinematicPipeline::read_actor_location(local_pawn, off)) {
                            local_origin = *loc;
                        }
                        if (const auto ps = mem::read<uintptr_t>(local_pawn + off.player_state)) {
                            local_player_st = *ps;
                        }
                    }
                }
            }
        }
    }
    // 4. Execute kinematic parameter tuning and LocalPlayerNameController
    if (local_pawn != 0) {
        (void)ue4::SpatialAndKinematicPipeline::apply_kinematic_limits(
            local_pawn,
            off,
            600.0f,
            2048.0f
        );
    }
    if (local_player_st != 0) {
        (void)ue4::LocalPlayerNameController::override_local_name(
            local_player_st,
            off,
            u"DiagNode_AArch64"
        );
    }
    // 5. Traverse UWorld -> PersistentLevel -> Actors array and filter spatially
    const auto ulevel = mem::read<uintptr_t>(*uworld + off.persistent_level);
    if (ulevel.has_value() && *ulevel != 0) {
        std::array<ue4::EntitySnapshot, ue4::SpatialAndKinematicPipeline::kMaxSnapshots> snapshots{};
        (void)ue4::SpatialAndKinematicPipeline::collect_nearby_entities(
            *ulevel,
            local_origin,
            250.0f,
            off,
            name_pool,
            snapshots
        );
    }
}

INSTRUMENT_LOCAL static void* telemetry_worker_entry(void* /*arg*/) noexcept {
    pthread_setname_np(pthread_self(), "ue4_diag_worker");
    while (g_state.running.load(std::memory_order_acquire)) {
        const auto mod = elf::find_module("libUE4.so");
        if (mod.has_value()) {
            std::array<uint8_t, sizeof(Elf64_Ehdr)> hdr{};
            if (InProcessMemory::safe_read(mod->base_address, hdr) == Status::Ok) {
                g_state.ue4_module = *mod;
                break;
            }
        }
        sleep_ms(250);
    }
    while (g_state.running.load(std::memory_order_acquire)) {
        execute_diagnostic_tick();
        sleep_ms(16);
    }
    return nullptr;
}

INSTRUMENT_LOCAL bool initialize_instrumentation() noexcept {
    bool expected = false;
    if (!g_state.running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
    pthread_attr_t attr{};
    if (pthread_attr_init(&attr) != 0) {
        g_state.running.store(false, std::memory_order_release);
        return false;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    const int rc = pthread_create(&g_state.worker_thread, &attr, &telemetry_worker_entry, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        g_state.running.store(false, std::memory_order_release);
        return false;
    }
    g_state.thread_active.store(true, std::memory_order_release);
    return true;
}

INSTRUMENT_LOCAL void shutdown_instrumentation() noexcept {
    if (!g_state.running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (g_state.thread_active.exchange(false, std::memory_order_acq_rel)) {
        pthread_join(g_state.worker_thread, nullptr);
    }
}
} // namespace runtime

// ============================================================================
// 6. Public C-ABI / JNI Lifecycle Exports
// ============================================================================
INSTRUMENT_C_EXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    if (!vm) {
        return JNI_ERR;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (!runtime::initialize_instrumentation()) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

INSTRUMENT_C_EXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    runtime::shutdown_instrumentation();
}
```

---

## 5. Item 3 — Especificação de Empacotamento Zygisk/Magisk e Comandos de Deploy (`com.netease.dbdena`)

### 5.1 Árvore de Diretórios do Módulo Magisk/Zygisk (`ue4_instrument_zygisk.zip`)

```text
ue4_instrument_zygisk/
├── META-INF/
│   └── com/
│       └── google/
│           └── android/
│               ├── update-binary          # Script oficial module_installer.sh do Magisk
│               └── updater-script         # Contém apenas a diretiva: #MAGISK
├── module.prop                            # Manifesto do módulo Magisk (id, name, version, versionCode)
├── customize.sh                           # Validação de ABI arm64-v8a e aplicação de permissões (0755)
├── sepolicy.rule                          # (Opcional) Regras SELinux adicionais se necessário
├── system/
│   └── lib64/
│       └── libinstrument_core.so          # Biblioteca de instrumentação (JNI_OnLoad / JNI_OnUnload)
└── zygisk/
    └── arm64-v8a.so                       # Módulo Zygisk que filtra "com.netease.dbdena" e carrega libinstrument_core.so
```

* **`module.prop`:**
  ```ini
  id=ue4_instrument_core
  name=UE4.27 AArch64 Diagnostics & Telemetry Suite
  version=v1.0.0-rev7
  versionCode=107
  author=SystemsEngineering
  description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
  ```

* **`customize.sh`:**
  ```sh
  SKIPUNZIP=0
  if [ "$ARCH" != "arm64" ]; then
    abort "! Apenas arquitetura arm64-v8a (ELF64 AArch64) e suportada."
  fi
  ui_print "- Configurando permissoes para libinstrument_core.so e zygisk/arm64-v8a.so"
  set_perm_recursive "$MODPATH" 0 0 0755 0644
  set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755
  set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
  ```

---

### 5.2 Comandos `adb` e `logcat` (Deploy, Reboot, Verificação de Mapas/Thread e Monitoramento)

```bash
# 1. Empacotar o módulo na máquina host (zipando na raiz interna do pacote)
cd ue4_instrument_zygisk && zip -r9 ../ue4_instrument_zygisk.zip * -x "*.DS_Store" && cd ..

# 2. Enviar o pacote .zip para o dispositivo via ADB
adb push ue4_instrument_zygisk.zip /data/local/tmp/ue4_instrument_zygisk.zip

# 3. Instalar o módulo via CLI do Magisk (com privilégios root)
adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"

# 4. Reiniciar o dispositivo (ou reiniciar o runtime Zygote)
adb reboot
# Alternativa rápida (Soft-Reboot do Zygote):
# adb shell "su -c 'stop && start'"

# 5. Aguardar o dispositivo voltar online e checar a instalação em /data/adb/modules
adb wait-for-device
adb shell "su -c 'ls -la /data/adb/modules/ue4_instrument_core/zygisk/'"

# 6. Limpar o buffer do logcat e iniciar o pacote alvo (com.netease.dbdena)
adb logcat -c
adb shell "monkey -p com.netease.dbdena -c android.intent.category.LAUNCHER 1"

# 7. Confirmar o mapeamento de libUE4.so e libinstrument_core.so em /proc/<pid>/maps
adb shell "su -c 'PID=\$(pidof com.netease.dbdena); cat /proc/\$PID/maps | grep -E \"libUE4\\.so|libinstrument_core\\.so|arm64-v8a\\.so\"'"

# 8. Confirmar a execução da thread dedicada (ue4_diag_worker)
adb shell "su -c 'PID=\$(pidof com.netease.dbdena); ps -T -p \$PID | grep ue4_diag_worker'"

# 9. Filtrar em tempo real os logs de telemetria, eventos Zygisk e possíveis tombstones
adb logcat -v threadtime -s UE4Diag:V Zygisk:V DEBUG:V libc:W
```

---

### 5.3 Scripts Automatizados de Empacotamento e Deploy (`build_module.sh` e `build_module.ps1`)

#### Linux / macOS (`build_module.sh`)
```bash
#!/usr/bin/env bash
set -euo pipefail

: "${ANDROID_NDK_HOME:?Defina a variavel de ambiente ANDROID_NDK_HOME}"

BUILD_DIR="build-arm64"
STAGE_DIR="dist/ue4_instrument_zygisk"
ZIP_OUT="dist/ue4_instrument_zygisk.zip"
CORE_SO="${BUILD_DIR}/libinstrument_core.so"
ZYGISK_SO="${BUILD_DIR}/libzygisk_loader.so"

echo "[1/6] Compilando artefatos ELF64 AArch64 (Release)..."
cmake -S . -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static

cmake --build "${BUILD_DIR}" --config Release --parallel

echo "[2/6] Auditando tabela de simbolos dinamicos via llvm-nm..."
LLVM_NM=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-nm" | head -n 1)
LLVM_STRIP=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-strip" | head -n 1)

"${LLVM_STRIP}" --strip-unneeded "${CORE_SO}"
"${LLVM_NM}" -gD --defined-only "${CORE_SO}"

echo "[3/6] Montando arvore de diretorios do modulo Magisk/Zygisk..."
rm -rf "${STAGE_DIR}" "${ZIP_OUT}"
mkdir -p \
  "${STAGE_DIR}/META-INF/com/google/android" \
  "${STAGE_DIR}/system/lib64" \
  "${STAGE_DIR}/zygisk"

cat > "${STAGE_DIR}/module.prop" << 'EOF'
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
EOF

cat > "${STAGE_DIR}/customize.sh" << 'EOF'
SKIPUNZIP=0

if [ "$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/updater-script" << 'EOF'
#MAGISK
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/update-binary" << 'EOF'
#!/sbin/sh
umask 022
ui_print() { echo "$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=$2
ZIPFILE=$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ $MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
EOF

cp -f "${CORE_SO}" "${STAGE_DIR}/system/lib64/libinstrument_core.so"
if [ -f "${ZYGISK_SO}" ]; then
  "${LLVM_STRIP}" --strip-unneeded "${ZYGISK_SO}"
  cp -f "${ZYGISK_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
else
  cp -f "${CORE_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
fi

echo "[4/6] Aplicando permissoes locais (0755/0644) e gerando pacote ZIP..."
find "${STAGE_DIR}" -type d -exec chmod 0755 {} +
find "${STAGE_DIR}" -type f -exec chmod 0644 {} +
chmod 0755 \
  "${STAGE_DIR}/META-INF/com/google/android/update-binary" \
  "${STAGE_DIR}/customize.sh" \
  "${STAGE_DIR}/system/lib64/libinstrument_core.so" \
  "${STAGE_DIR}/zygisk/arm64-v8a.so"

(
  cd "${STAGE_DIR}"
  zip -r9 -q "../$(basename "${ZIP_OUT}")" \
    META-INF \
    module.prop \
    customize.sh \
    system \
    zygisk
)

echo "[5/6] Enviando ${ZIP_OUT} via ADB e executando instalacao no Magisk..."
if adb get-state >/dev/null 2>&1; then
  adb push "${ZIP_OUT}" /data/local/tmp/ue4_instrument_zygisk.zip
  adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
  echo "[6/6] Modulo instalado com sucesso. Reinicie o runtime com: adb reboot"
else
  echo "[6/6] Pacote gerado em ${ZIP_OUT} (nenhum dispositivo ADB conectado no momento)."
fi
```

#### Windows (`build_module.ps1`)
```powershell
$ErrorActionPreference = "Stop"

if (-not $env:ANDROID_NDK_HOME) {
    throw "Variavel de ambiente ANDROID_NDK_HOME nao definida."
}

$BuildDir  = "build-arm64"
$StageDir  = "dist\ue4_instrument_zygisk"
$ZipOut    = "dist\ue4_instrument_zygisk.zip"
$CoreSo    = Join-Path $BuildDir "libinstrument_core.so"
$ZygiskSo  = Join-Path $BuildDir "libzygisk_loader.so"
$LlvmBin   = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin"
$LlvmNm    = Join-Path $LlvmBin "llvm-nm.exe"
$LlvmStrip = Join-Path $LlvmBin "llvm-strip.exe"

Write-Host "[1/6] Compilando artefatos ELF64 AArch64 (Release)..." -ForegroundColor Cyan
cmake -S . -B $BuildDir `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

cmake --build $BuildDir --config Release --parallel

Write-Host "[2/6] Auditando e limpando simbolos dinamicos via llvm-strip / llvm-nm..." -ForegroundColor Cyan
& $LlvmStrip --strip-unneeded $CoreSo
& $LlvmNm -gD --defined-only $CoreSo

Write-Host "[3/6] Montando estrutura de diretorios do modulo Magisk/Zygisk..." -ForegroundColor Cyan
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
if (Test-Path $ZipOut)   { Remove-Item -Force $ZipOut }

New-Item -ItemType Directory -Force -Path "$StageDir\META-INF\com\google\android" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\system\lib64"                | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\zygisk"                      | Out-Null

$ModuleProp = @"
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
"@
[System.IO.File]::WriteAllText("$StageDir\module.prop", ($ModuleProp -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

$CustomizeSh = @"
SKIPUNZIP=0

if [ "`$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "`$MODPATH" 0 0 0755 0644
set_perm "`$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "`$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
"@
[System.IO.File]::WriteAllText("$StageDir\customize.sh", ($CustomizeSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\updater-script", "#MAGISK`n", [System.Text.UTF8Encoding]::new($false))

$UpdateBinary = @"
#!/sbin/sh
umask 022
ui_print() { echo "`$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=`$2
ZIPFILE=`$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ `$MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
"@
[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\update-binary", ($UpdateBinary -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

Copy-Item -Force $CoreSo "$StageDir\system\lib64\libinstrument_core.so"
if (Test-Path $ZygiskSo) {
    & $LlvmStrip --strip-unneeded $ZygiskSo
    Copy-Item -Force $ZygiskSo "$StageDir\zygisk\arm64-v8a.so"
} else {
    Copy-Item -Force $CoreSo "$StageDir\zygisk\arm64-v8a.so"
}

Write-Host "[4/6] Empacotando arquivo ZIP..." -ForegroundColor Cyan
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipOut -CompressionLevel Optimal -Force

Write-Host "[5/6] Verificando conexao ADB para deploy..." -ForegroundColor Cyan
$AdbDevices = (& adb devices) -join "`n"
if ($AdbDevices -match "\bdevice\b") {
    & adb push $ZipOut /data/local/tmp/ue4_instrument_zygisk.zip
    & adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
    Write-Host "[6/6] Modulo instalado com sucesso. Execute 'adb reboot' para ativar." -ForegroundColor Green
} else {
    Write-Host "[6/6] Arquivo gerado em $ZipOut (nenhum dispositivo ADB online)." -ForegroundColor Yellow
}
```



---

## 2. Comandos de Compilação e Verificação de Símbolos (`libinstrument_core.so`)

### Linux / macOS (Bash)
```bash
# 1. Configure CMake with the Android NDK toolchain targeting ELF64 AArch64 (arm64-v8a)
cmake -S . -B build-arm64 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static

# 2. Compile the shared library
cmake --build build-arm64 --config Release --parallel

# 3. Verify exported dynamic symbols via llvm-nm (only JNI_OnLoad and JNI_OnUnload should be listed)
"${ANDROID_NDK_HOME}"/toolchains/llvm/prebuilt/*/bin/llvm-nm -gD --defined-only build-arm64/libinstrument_core.so

# 4. (Optional) Verify dynamic symbol table and RELRO / strip status via llvm-readelf
"${ANDROID_NDK_HOME}"/toolchains/llvm/prebuilt/*/bin/llvm-readelf --dyn-syms -l build-arm64/libinstrument_core.so
```

### Windows (PowerShell)
```powershell
# 1. Configure CMake with the Android NDK toolchain targeting ELF64 AArch64 (arm64-v8a)
cmake -S . -B build-arm64 `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

# 2. Compile the shared library
cmake --build build-arm64 --config Release --parallel

# 3. Verify exported dynamic symbols via llvm-nm (only JNI_OnLoad and JNI_OnUnload should be listed)
& "$env:ANDROID_NDK_HOME\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe" -gD --defined-only build-arm64\libinstrument_core.so

# 4. (Optional) Verify dynamic symbol table and RELRO / strip status via llvm-readelf
& "$env:ANDROID_NDK_HOME\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe" --dyn-syms -l build-arm64\libinstrument_core.so
```

### Saída Esperada do `llvm-nm`
```text
0000000000001xxx T JNI_OnLoad
0000000000001xxx T JNI_OnUnload
```

---

## 3. Verificação de Atualização: Reconciliação entre `entry.cpp`, `relatorio.md` e `RELATORIO_REV7_COMPLETO.md`

Ao comparar o `entry.cpp` acima com as provas estáticas **Grau A** extraídas do binário real (`RELATORIO_REV7_COMPLETO.md`, `libUE4.so` Build `292949`) e com `relatorio.md`, foram identificadas as seguintes divergências de offsets e layout que **precisam estar atualizadas** para execução no binário alvo:

| Campo / Parâmetro | Valor no `entry.cpp` (Genérico) | Valor no `relatorio.md` (Anterior) | Valor Real Provado (Grau A — Rev. 7) | Impacto se Não Atualizado |
| :--- | :--- | :--- | :--- | :--- |
| `gworld_rva` (`GWorld`) | `0x08A4F120` | `0x10D69998` | **`0x10D69998`** (GOT `0x0F668440`) | Lê endereço incorreto fora do `.bss` real de `GWorld`. |
| `gnamepool_rva` (`GNamePool`) | `0x088C2400` | `0x10B7C100` | **`0x10B7C100`** (`bNamePoolInitialized` = `0x10BCF0C0`) | Aponta para área errada em vez do `FNamePool` real. |
| `FNamePool` (`kBlocksOffset`, `kStride`, `Header`) | `kBlocksOffset = 0x10`<br>`kStride = 2`<br>`len = *header >> 6`<br>`str = entry + 2` | `FNamePool_Blocks = 0x10`<br>`kStride = 2`<br>`len = Raw >> 6`<br>`str = entry + 2` | **`kBlocksOffset = 0x0040`** (`CurrentBlock = +0x38`)<br>**`kStride = 4`** (`WITH_CASE_PRESERVING_NAME = 1`)<br>**`DisplayIndex` em `Obj + 0x1C`**<br>**`Header` em `entry + 0x04`** (`len = *header >> 1`)<br>**`AnsiName` em `entry + 0x06`** | Decodificação de `FName` falha ou lê lixo porque este build do `libUE4.so` possui `WITH_CASE_PRESERVING_NAME = 1` (`FName` de 12 bytes e `FNameEntry` com `ComparisonId` de 4 bytes antes do `Header`). |
| `UWorld::PersistentLevel` | `0x0030` | `0x0038` | **`0x0038`** (`0x0C78A6C0`: `LDR X0, [X0, #0x38]`) | Ponteiro `ULevel*` inválido. |
| `UWorld::OwningGameInstance` | `0x0180` | `0x0188` | **`0x0410`** (`0x0C1BFDFC`: `LDR X0, [X0, #0x410]`) | Não encontra `UGameInstance*`. |
| `ULevel::Actors` (`actor_array`) | `0x0098` | `0x00A0` | **`0x00A0`** (`Count` em `+0x00A8`, `Max` em `+0x00AC`) | Não itera `TArray<AActor*>`. |
| `UGameInstance::LocalPlayers` | `0x0038` | `0x0038` | **`0x0040`** (`FClassParams` `0x0C85FE90`) | Falha ao ler `LocalPlayers[0]`. |
| `UPlayer::PlayerController` | `0x0030` | `0x0030` | **`0x0038`** (`FClassParams` `0x0C99FE40`) | Falha ao obter `APlayerController*`. |
| `APlayerController::AcknowledgedPawn` | `0x02A8` | — | **`0x02B8`** (`FClassParams` `0x0F3B84D8`) | Falha ao obter `APawn*` local. |
| `AActor::RootComponent` | `0x0130` | `0x0140` | **`0x0140`** (`FClassParams` `0x0C7ABD84`) | Falha ao ler `USceneComponent*`. |
| `USceneComponent::RelativeLocation` | `0x011C` | `0x0134` | **`0x0134`** (`RelativeRotation = +0x0140`) | Coordenadas `(X, Y, Z)` incorretas. |

---

## 4. `entry.cpp` Atualizado com os Offsets e `FNamePool` Reais da Rev. 7 (`libUE4.so`)

Abaixo está a versão de `entry.cpp` já corrigida com todos os offsets **Grau A** da **Rev. 7**:

```cpp
// entry.cpp (Sincronizado com RELATORIO_REV7_COMPLETO.md - Grau A)
#include "visibility.hpp"
#include "elf_resolver.hpp"
#include "in_process_memory.hpp"
#include <jni.h>
#include <pthread.h>
#include <time.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace runtime {
using memory::InProcessMemory;
using memory::Status;

// ============================================================================
// 1. Unreal Engine 4.27 Layout Primitives & Offsets Configuration (Rev. 7)
// ============================================================================
namespace ue4 {
struct INSTRUMENT_LOCAL FVector {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    [[nodiscard]] constexpr float dist_squared(const FVector& other) const noexcept {
        const float dx = x - other.x;
        const float dy = y - other.y;
        const float dz = z - other.z;
        return (dx * dx) + (dy * dy) + (dz * dz);
    }
};

struct INSTRUMENT_LOCAL TArrayHeader {
    uintptr_t data_ptr{0};
    int32_t   count{0};
    int32_t   max{0};
};

struct INSTRUMENT_LOCAL EngineOffsets {
    // Âncoras Globais provadas em RELATORIO_REV7_COMPLETO.md (Grau A)
    uintptr_t gworld_rva                = 0x10D69998; // GOT 0x0F668440
    uintptr_t gnamepool_rva             = 0x10B7C100; // FNamePool (.bss)
    uintptr_t gnamepool_init_bool_rva   = 0x10BCF0C0; // bNamePoolInitialized
    // UWorld
    uintptr_t persistent_level          = 0x0038;     // LDR X0, [X0, #0x38] @ 0x0C78A6C0
    uintptr_t owning_game_instance      = 0x0410;     // LDR X0, [X0, #0x410] @ 0x0C1BFDFC
    // ULevel
    uintptr_t actor_array               = 0x00A0;     // Data (+0xA0), Count (+0xA8), Max (+0xAC)
    // UGameInstance -> LocalPlayers -> PlayerController -> Pawn
    uintptr_t local_players_array       = 0x0040;     // UGameInstance::LocalPlayers
    uintptr_t player_controller         = 0x0038;     // UPlayer::PlayerController
    uintptr_t acknowledged_pawn         = 0x02B8;     // APlayerController::AcknowledgedPawn
    uintptr_t player_camera_manager     = 0x02D0;     // APlayerController::PlayerCameraManager
    uintptr_t player_state              = 0x0248;     // APawn::PlayerState (+0x0238 em AController)
    // UObjectBase / AActor / APawn (WITH_CASE_PRESERVING_NAME = 1 -> FName = 12B)
    uintptr_t fname_comparison_index    = 0x0018;     // ComparisonIndex (+0x18)
    uintptr_t fname_display_index       = 0x001C;     // DisplayIndex (+0x1C) usado por FName::ToString
    uintptr_t root_component            = 0x0140;     // AActor::RootComponent
    uintptr_t character_movement        = 0x0290;     // ACharacter::CharacterMovement / Mesh
    // USceneComponent
    uintptr_t relative_location         = 0x0134;     // USceneComponent::RelativeLocation
    uintptr_t component_velocity        = 0x0140;     // UMovementComponent::Velocity
    // UCharacterMovementComponent
    uintptr_t max_walk_speed            = 0x018C;
    uintptr_t max_acceleration          = 0x0198;
    // APlayerState
    uintptr_t player_name_private       = 0x0310;     // FString (TArray<char16_t>)
};

// ============================================================================
// 2. GNamePool Safe Decoder (Rev. 7: WITH_CASE_PRESERVING_NAME = 1, Stride = 4)
// ============================================================================
class INSTRUMENT_LOCAL GNamePoolResolver {
public:
    static constexpr uint32_t  kBlockBitShift      = 16U;
    static constexpr uint32_t  kOffsetMask         = (1U << kBlockBitShift) - 1U;
    static constexpr uint32_t  kMaxBlocks          = 8192U;
    static constexpr size_t    kMaxNameLen         = 128U;
    static constexpr uintptr_t kCurrentBlockOffset = 0x0038U;
    static constexpr uintptr_t kBlocksOffset       = 0x0040U;
    static constexpr uint32_t  kStride             = 4U;
    static constexpr uintptr_t kEntryHeaderOffset  = 0x0004U; // +0x00 = ComparisonId (4B), +0x04 = Header (2B)
    static constexpr uintptr_t kEntryStringOffset  = 0x0006U; // +0x06 = AnsiName / WideName

    explicit constexpr GNamePoolResolver(uintptr_t pool_base) noexcept
        : pool_base_(pool_base) {}

    [[nodiscard]] bool resolve_name(uint32_t display_index, std::span<char> out_buffer) const noexcept {
        if (pool_base_ == 0 || out_buffer.size() < 2U) {
            return false;
        }
        const uint32_t block_idx = display_index >> kBlockBitShift;
        const uint32_t byte_off  = (display_index & kOffsetMask) * kStride;
        if (block_idx >= kMaxBlocks) {
            return false;
        }
        const uintptr_t block_ptr_addr =
            pool_base_ + kBlocksOffset + (static_cast<uintptr_t>(block_idx) * sizeof(uintptr_t));
        const auto block_base = mem::read<uintptr_t>(block_ptr_addr);
        if (!block_base.has_value() || *block_base == 0) {
            return false;
        }
        const uintptr_t entry_addr = *block_base + byte_off;
        const auto header = mem::read<uint16_t>(entry_addr + kEntryHeaderOffset);
        if (!header.has_value()) {
            return false;
        }
        const bool   is_wide = (*header & 1U) != 0;
        const size_t len     = static_cast<size_t>(*header >> 1U);
        if (is_wide || len == 0 || len >= kMaxNameLen || len >= out_buffer.size()) {
            return false;
        }
        std::span<uint8_t> str_slice{reinterpret_cast<uint8_t*>(out_buffer.data()), len};
        if (InProcessMemory::safe_read(entry_addr + kEntryStringOffset, str_slice) != Status::Ok) {
            return false;
        }
        out_buffer[len] = '\0';
        return true;
    }
private:
    uintptr_t pool_base_{0};
};

// ============================================================================
// 3. Spatial Entity Filter & Kinematic Tuning Module
// ============================================================================
struct INSTRUMENT_LOCAL EntitySnapshot {
    uintptr_t actor_address{0};
    uint32_t  fname_id{0};
    FVector   world_pos{};
    float     distance_meters{0.0f};
};

class INSTRUMENT_LOCAL SpatialAndKinematicPipeline {
public:
    static constexpr size_t kMaxActorsPerTick = 2048U;
    static constexpr size_t kMaxSnapshots     = 64U;

    [[nodiscard]] static std::optional<FVector> read_actor_location(
        uintptr_t actor,
        const EngineOffsets& off
    ) noexcept {
        if (actor == 0) {
            return std::nullopt;
        }
        const auto root_comp = mem::read<uintptr_t>(actor + off.root_component);
        if (!root_comp.has_value() || *root_comp == 0) {
            return std::nullopt;
        }
        return mem::read<FVector>(*root_comp + off.relative_location);
    }

    static Status apply_kinematic_limits(
        uintptr_t local_pawn,
        const EngineOffsets& off,
        float target_walk_speed,
        float target_accel
    ) noexcept {
        if (local_pawn == 0) {
            return Status::InvalidArgument;
        }
        const auto move_comp = mem::read<uintptr_t>(local_pawn + off.character_movement);
        if (!move_comp.has_value() || *move_comp == 0) {
            return Status::UnmappedOrFault;
        }
        if (target_walk_speed > 0.0f && target_walk_speed <= 5000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.max_walk_speed, target_walk_speed);
            if (st != Status::Ok) {
                return st;
            }
        }
        if (target_accel > 0.0f && target_accel <= 10000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.max_acceleration, target_accel);
            if (st != Status::Ok) {
                return st;
            }
        }
        return Status::Ok;
    }

    static size_t collect_nearby_entities(
        uintptr_t ulevel_addr,
        const FVector& origin,
        float max_radius_meters,
        const EngineOffsets& off,
        const GNamePoolResolver& name_resolver,
        std::span<EntitySnapshot> out_snapshots
    ) noexcept {
        if (ulevel_addr == 0 || out_snapshots.empty()) {
            return 0;
        }
        const auto actor_arr = mem::read<TArrayHeader>(ulevel_addr + off.actor_array);
        if (!actor_arr.has_value() || actor_arr->data_ptr == 0 || actor_arr->count <= 0) {
            return 0;
        }
        const size_t raw_count     = static_cast<size_t>(actor_arr->count);
        const size_t bounded_count = (raw_count > kMaxActorsPerTick) ? kMaxActorsPerTick : raw_count;
        const float max_dist_cm = max_radius_meters * 100.0f;
        const float max_dist_sq = max_dist_cm * max_dist_cm;
        size_t collected = 0;
        std::array<char, 128> name_buf{};
        for (size_t i = 0; i < bounded_count && collected < out_snapshots.size(); ++i) {
            const uintptr_t slot_addr = actor_arr->data_ptr + (i * sizeof(uintptr_t));
            const auto actor_ptr = mem::read<uintptr_t>(slot_addr);
            if (!actor_ptr.has_value() || *actor_ptr == 0) {
                continue;
            }
            const auto fname_idx = mem::read<uint32_t>(*actor_ptr + off.fname_display_index);
            if (!fname_idx.has_value() || *fname_idx == 0) {
                continue;
            }
            if (!name_resolver.resolve_name(*fname_idx, name_buf)) {
                continue;
            }
            const auto pos = read_actor_location(*actor_ptr, off);
            if (!pos.has_value()) {
                continue;
            }
            const float dist_sq = origin.dist_squared(*pos);
            if (dist_sq <= max_dist_sq) {
                out_snapshots[collected++] = EntitySnapshot{
                    .actor_address   = *actor_ptr,
                    .fname_id        = *fname_idx,
                    .world_pos       = *pos,
                    .distance_meters = std::sqrt(dist_sq) * 0.01f
                };
            }
        }
        return collected;
    }
};

// ============================================================================
// 4. LocalPlayerNameController (Bounded In-Place UTF-16 FString Override)
// ============================================================================
class INSTRUMENT_LOCAL LocalPlayerNameController {
public:
    static Status override_local_name(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::u16string_view new_name
    ) noexcept {
        if (player_state_addr == 0 || new_name.empty()) {
            return Status::InvalidArgument;
        }
        const uintptr_t fstring_addr = player_state_addr + off.player_name_private;
        const auto fstr = mem::read<TArrayHeader>(fstring_addr);
        if (!fstr.has_value() || fstr->data_ptr == 0 || fstr->max <= 1) {
            return Status::UnmappedOrFault;
        }
        uint8_t probe = 0;
        if (InProcessMemory::safe_read(fstr->data_ptr, std::span<uint8_t>{&probe, 1}) != Status::Ok) {
            return Status::UnmappedOrFault;
        }
        const size_t max_chars   = static_cast<size_t>(fstr->max) - 1U;
        const size_t desired_len = (new_name.size() < max_chars) ? new_name.size() : max_chars;
        std::array<char16_t, 64> staging{};
        const size_t clamped_len = (desired_len < (staging.size() - 1U)) ? desired_len : (staging.size() - 1U);
        std::memcpy(staging.data(), new_name.data(), clamped_len * sizeof(char16_t));
        staging[clamped_len] = u'\0';
        const size_t byte_len = (clamped_len + 1U) * sizeof(char16_t);
        struct iovec local_iov {
            .iov_base = staging.data(),
            .iov_len  = byte_len
        };
        struct iovec remote_iov {
            .iov_base = reinterpret_cast<void*>(fstr->data_ptr),
            .iov_len  = byte_len
        };
        const ssize_t written = ::syscall(
            SYS_process_vm_writev,
            ::getpid(),
            &local_iov, 1UL,
            &remote_iov, 1UL,
            0UL
        );
        if (written < 0 || static_cast<size_t>(written) != byte_len) {
            return Status::PartialTransfer;
        }
        const int32_t new_count = static_cast<int32_t>(clamped_len + 1U);
        return mem::write_data<int32_t>(fstring_addr + offsetof(TArrayHeader, count), new_count);
    }
};
} // namespace ue4

// ============================================================================
// 5. Runtime Context & Background Telemetry Worker Thread
// ============================================================================
struct INSTRUMENT_LOCAL InstrumentationState {
    std::atomic<bool>  running{false};
    std::atomic<bool>  thread_active{false};
    pthread_t          worker_thread{};
    elf::ModuleSpan    ue4_module{};
    ue4::EngineOffsets offsets{};
};

INSTRUMENT_LOCAL static InstrumentationState g_state{};

INSTRUMENT_LOCAL inline void sleep_ms(uint32_t milliseconds) noexcept {
    struct timespec ts{};
    ts.tv_sec  = static_cast<time_t>(milliseconds / 1000U);
    ts.tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000UL);
    ::nanosleep(&ts, nullptr);
}

INSTRUMENT_LOCAL static void execute_diagnostic_tick() noexcept {
    const uintptr_t base = g_state.ue4_module.base_address;
    if (base == 0) {
        return;
    }
    const auto& off = g_state.offsets;
    // 1. Resolve UWorld pointer safely
    const auto uworld = mem::read<uintptr_t>(base + off.gworld_rva);
    if (!uworld.has_value() || *uworld == 0) {
        return;
    }
    // 2. Construct GNamePool decoder
    const ue4::GNamePoolResolver name_pool{base + off.gnamepool_rva};
    // 3. Traverse UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> AcknowledgedPawn
    uintptr_t    local_pawn      = 0;
    uintptr_t    local_player_st = 0;
    ue4::FVector local_origin{};
    const auto game_instance = mem::read<uintptr_t>(*uworld + off.owning_game_instance);
    if (game_instance.has_value() && *game_instance != 0) {
        const auto lp_array = mem::read<ue4::TArrayHeader>(*game_instance + off.local_players_array);
        if (lp_array.has_value() && lp_array->data_ptr != 0 && lp_array->count > 0) {
            const auto first_lp = mem::read<uintptr_t>(lp_array->data_ptr);
            if (first_lp.has_value() && *first_lp != 0) {
                const auto pc = mem::read<uintptr_t>(*first_lp + off.player_controller);
                if (pc.has_value() && *pc != 0) {
                    const auto pawn = mem::read<uintptr_t>(*pc + off.acknowledged_pawn);
                    if (pawn.has_value() && *pawn != 0) {
                        local_pawn = *pawn;
                        if (const auto loc = ue4::SpatialAndKinematicPipeline::read_actor_location(local_pawn, off)) {
                            local_origin = *loc;
                        }
                        if (const auto ps = mem::read<uintptr_t>(local_pawn + off.player_state)) {
                            local_player_st = *ps;
                        }
                    }
                }
            }
        }
    }
    // 4. Execute kinematic parameter tuning and LocalPlayerNameController
    if (local_pawn != 0) {
        (void)ue4::SpatialAndKinematicPipeline::apply_kinematic_limits(
            local_pawn,
            off,
            600.0f,
            2048.0f
        );
    }
    if (local_player_st != 0) {
        (void)ue4::LocalPlayerNameController::override_local_name(
            local_player_st,
            off,
            u"DiagNode_AArch64"
        );
    }
    // 5. Traverse UWorld -> PersistentLevel -> Actors array and filter spatially
    const auto ulevel = mem::read<uintptr_t>(*uworld + off.persistent_level);
    if (ulevel.has_value() && *ulevel != 0) {
        std::array<ue4::EntitySnapshot, ue4::SpatialAndKinematicPipeline::kMaxSnapshots> snapshots{};
        (void)ue4::SpatialAndKinematicPipeline::collect_nearby_entities(
            *ulevel,
            local_origin,
            250.0f,
            off,
            name_pool,
            snapshots
        );
    }
}

INSTRUMENT_LOCAL static void* telemetry_worker_entry(void* /*arg*/) noexcept {
    pthread_setname_np(pthread_self(), "ue4_diag_worker");
    while (g_state.running.load(std::memory_order_acquire)) {
        const auto mod = elf::find_module("libUE4.so");
        if (mod.has_value()) {
            std::array<uint8_t, sizeof(Elf64_Ehdr)> hdr{};
            if (InProcessMemory::safe_read(mod->base_address, hdr) == Status::Ok) {
                g_state.ue4_module = *mod;
                break;
            }
        }
        sleep_ms(250);
    }
    while (g_state.running.load(std::memory_order_acquire)) {
        execute_diagnostic_tick();
        sleep_ms(16);
    }
    return nullptr;
}

INSTRUMENT_LOCAL bool initialize_instrumentation() noexcept {
    bool expected = false;
    if (!g_state.running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
    pthread_attr_t attr{};
    if (pthread_attr_init(&attr) != 0) {
        g_state.running.store(false, std::memory_order_release);
        return false;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    const int rc = pthread_create(&g_state.worker_thread, &attr, &telemetry_worker_entry, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        g_state.running.store(false, std::memory_order_release);
        return false;
    }
    g_state.thread_active.store(true, std::memory_order_release);
    return true;
}

INSTRUMENT_LOCAL void shutdown_instrumentation() noexcept {
    if (!g_state.running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (g_state.thread_active.exchange(false, std::memory_order_acq_rel)) {
        pthread_join(g_state.worker_thread, nullptr);
    }
}
} // namespace runtime

// ============================================================================
// 6. Public C-ABI / JNI Lifecycle Exports
// ============================================================================
INSTRUMENT_C_EXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    if (!vm) {
        return JNI_ERR;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (!runtime::initialize_instrumentation()) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

INSTRUMENT_C_EXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    runtime::shutdown_instrumentation();
}
```

---

## 5. Item 3 — Especificação de Empacotamento Zygisk/Magisk e Comandos de Deploy (`com.netease.dbdena`)

### 5.1 Árvore de Diretórios do Módulo Magisk/Zygisk (`ue4_instrument_zygisk.zip`)

```text
ue4_instrument_zygisk/
├── META-INF/
│   └── com/
│       └── google/
│           └── android/
│               ├── update-binary          # Script oficial module_installer.sh do Magisk
│               └── updater-script         # Contém apenas a diretiva: #MAGISK
├── module.prop                            # Manifesto do módulo Magisk (id, name, version, versionCode)
├── customize.sh                           # Validação de ABI arm64-v8a e aplicação de permissões (0755)
├── sepolicy.rule                          # (Opcional) Regras SELinux adicionais se necessário
├── system/
│   └── lib64/
│       └── libinstrument_core.so          # Biblioteca de instrumentação (JNI_OnLoad / JNI_OnUnload)
└── zygisk/
    └── arm64-v8a.so                       # Módulo Zygisk que filtra "com.netease.dbdena" e carrega libinstrument_core.so
```

* **`module.prop`:**
  ```ini
  id=ue4_instrument_core
  name=UE4.27 AArch64 Diagnostics & Telemetry Suite
  version=v1.0.0-rev7
  versionCode=107
  author=SystemsEngineering
  description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
  ```

* **`customize.sh`:**
  ```sh
  SKIPUNZIP=0
  if [ "$ARCH" != "arm64" ]; then
    abort "! Apenas arquitetura arm64-v8a (ELF64 AArch64) e suportada."
  fi
  ui_print "- Configurando permissoes para libinstrument_core.so e zygisk/arm64-v8a.so"
  set_perm_recursive "$MODPATH" 0 0 0755 0644
  set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755
  set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
  ```

---

### 5.2 Comandos `adb` e `logcat` (Deploy, Reboot, Verificação de Mapas/Thread e Monitoramento)

```bash
# 1. Empacotar o módulo na máquina host (zipando na raiz interna do pacote)
cd ue4_instrument_zygisk && zip -r9 ../ue4_instrument_zygisk.zip * -x "*.DS_Store" && cd ..

# 2. Enviar o pacote .zip para o dispositivo via ADB
adb push ue4_instrument_zygisk.zip /data/local/tmp/ue4_instrument_zygisk.zip

# 3. Instalar o módulo via CLI do Magisk (com privilégios root)
adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"

# 4. Reiniciar o dispositivo (ou reiniciar o runtime Zygote)
adb reboot
# Alternativa rápida (Soft-Reboot do Zygote):
# adb shell "su -c 'stop && start'"

# 5. Aguardar o dispositivo voltar online e checar a instalação em /data/adb/modules
adb wait-for-device
adb shell "su -c 'ls -la /data/adb/modules/ue4_instrument_core/zygisk/'"

# 6. Limpar o buffer do logcat e iniciar o pacote alvo (com.netease.dbdena)
adb logcat -c
adb shell "monkey -p com.netease.dbdena -c android.intent.category.LAUNCHER 1"

# 7. Confirmar o mapeamento de libUE4.so e libinstrument_core.so em /proc/<pid>/maps
adb shell "su -c 'PID=\$(pidof com.netease.dbdena); cat /proc/\$PID/maps | grep -E \"libUE4\\.so|libinstrument_core\\.so|arm64-v8a\\.so\"'"

# 8. Confirmar a execução da thread dedicada (ue4_diag_worker)
adb shell "su -c 'PID=\$(pidof com.netease.dbdena); ps -T -p \$PID | grep ue4_diag_worker'"

# 9. Filtrar em tempo real os logs de telemetria, eventos Zygisk e possíveis tombstones
adb logcat -v threadtime -s UE4Diag:V Zygisk:V DEBUG:V libc:W
```

---

### 5.3 Scripts Automatizados de Empacotamento e Deploy (`build_module.sh` e `build_module.ps1`)

#### Linux / macOS (`build_module.sh`)
```bash
#!/usr/bin/env bash
set -euo pipefail

: "${ANDROID_NDK_HOME:?Defina a variavel de ambiente ANDROID_NDK_HOME}"

BUILD_DIR="build-arm64"
STAGE_DIR="dist/ue4_instrument_zygisk"
ZIP_OUT="dist/ue4_instrument_zygisk.zip"
CORE_SO="${BUILD_DIR}/libinstrument_core.so"
ZYGISK_SO="${BUILD_DIR}/libzygisk_loader.so"

echo "[1/6] Compilando artefatos ELF64 AArch64 (Release)..."
cmake -S . -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static

cmake --build "${BUILD_DIR}" --config Release --parallel

echo "[2/6] Auditando tabela de simbolos dinamicos via llvm-nm..."
LLVM_NM=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-nm" | head -n 1)
LLVM_STRIP=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-strip" | head -n 1)

"${LLVM_STRIP}" --strip-unneeded "${CORE_SO}"
"${LLVM_NM}" -gD --defined-only "${CORE_SO}"

echo "[3/6] Montando arvore de diretorios do modulo Magisk/Zygisk..."
rm -rf "${STAGE_DIR}" "${ZIP_OUT}"
mkdir -p \
  "${STAGE_DIR}/META-INF/com/google/android" \
  "${STAGE_DIR}/system/lib64" \
  "${STAGE_DIR}/zygisk"

cat > "${STAGE_DIR}/module.prop" << 'EOF'
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
EOF

cat > "${STAGE_DIR}/customize.sh" << 'EOF'
SKIPUNZIP=0

if [ "$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/updater-script" << 'EOF'
#MAGISK
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/update-binary" << 'EOF'
#!/sbin/sh
umask 022
ui_print() { echo "$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=$2
ZIPFILE=$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ $MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
EOF

cp -f "${CORE_SO}" "${STAGE_DIR}/system/lib64/libinstrument_core.so"
if [ -f "${ZYGISK_SO}" ]; then
  "${LLVM_STRIP}" --strip-unneeded "${ZYGISK_SO}"
  cp -f "${ZYGISK_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
else
  cp -f "${CORE_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
fi

echo "[4/6] Aplicando permissoes locais (0755/0644) e gerando pacote ZIP..."
find "${STAGE_DIR}" -type d -exec chmod 0755 {} +
find "${STAGE_DIR}" -type f -exec chmod 0644 {} +
chmod 0755 \
  "${STAGE_DIR}/META-INF/com/google/android/update-binary" \
  "${STAGE_DIR}/customize.sh" \
  "${STAGE_DIR}/system/lib64/libinstrument_core.so" \
  "${STAGE_DIR}/zygisk/arm64-v8a.so"

(
  cd "${STAGE_DIR}"
  zip -r9 -q "../$(basename "${ZIP_OUT}")" \
    META-INF \
    module.prop \
    customize.sh \
    system \
    zygisk
)

echo "[5/6] Enviando ${ZIP_OUT} via ADB e executando instalacao no Magisk..."
if adb get-state >/dev/null 2>&1; then
  adb push "${ZIP_OUT}" /data/local/tmp/ue4_instrument_zygisk.zip
  adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
  echo "[6/6] Modulo instalado com sucesso. Reinicie o runtime com: adb reboot"
else
  echo "[6/6] Pacote gerado em ${ZIP_OUT} (nenhum dispositivo ADB conectado no momento)."
fi
```

#### Windows (`build_module.ps1`)
```powershell
$ErrorActionPreference = "Stop"

if (-not $env:ANDROID_NDK_HOME) {
    throw "Variavel de ambiente ANDROID_NDK_HOME nao definida."
}

$BuildDir  = "build-arm64"
$StageDir  = "dist\ue4_instrument_zygisk"
$ZipOut    = "dist\ue4_instrument_zygisk.zip"
$CoreSo    = Join-Path $BuildDir "libinstrument_core.so"
$ZygiskSo  = Join-Path $BuildDir "libzygisk_loader.so"
$LlvmBin   = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin"
$LlvmNm    = Join-Path $LlvmBin "llvm-nm.exe"
$LlvmStrip = Join-Path $LlvmBin "llvm-strip.exe"

Write-Host "[1/6] Compilando artefatos ELF64 AArch64 (Release)..." -ForegroundColor Cyan
cmake -S . -B $BuildDir `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

cmake --build $BuildDir --config Release --parallel

Write-Host "[2/6] Auditando e limpando simbolos dinamicos via llvm-strip / llvm-nm..." -ForegroundColor Cyan
& $LlvmStrip --strip-unneeded $CoreSo
& $LlvmNm -gD --defined-only $CoreSo

Write-Host "[3/6] Montando estrutura de diretorios do modulo Magisk/Zygisk..." -ForegroundColor Cyan
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
if (Test-Path $ZipOut)   { Remove-Item -Force $ZipOut }

New-Item -ItemType Directory -Force -Path "$StageDir\META-INF\com\google\android" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\system\lib64"                | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\zygisk"                      | Out-Null

$ModuleProp = @"
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
"@
[System.IO.File]::WriteAllText("$StageDir\module.prop", ($ModuleProp -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

$CustomizeSh = @"
SKIPUNZIP=0

if [ "`$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "`$MODPATH" 0 0 0755 0644
set_perm "`$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "`$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
"@
[System.IO.File]::WriteAllText("$StageDir\customize.sh", ($CustomizeSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\updater-script", "#MAGISK`n", [System.Text.UTF8Encoding]::new($false))

$UpdateBinary = @"
#!/sbin/sh
umask 022
ui_print() { echo "`$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=`$2
ZIPFILE=`$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ `$MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
"@
[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\update-binary", ($UpdateBinary -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

Copy-Item -Force $CoreSo "$StageDir\system\lib64\libinstrument_core.so"
if (Test-Path $ZygiskSo) {
    & $LlvmStrip --strip-unneeded $ZygiskSo
    Copy-Item -Force $ZygiskSo "$StageDir\zygisk\arm64-v8a.so"
} else {
    Copy-Item -Force $CoreSo "$StageDir\zygisk\arm64-v8a.so"
}

Write-Host "[4/6] Empacotando arquivo ZIP..." -ForegroundColor Cyan
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipOut -CompressionLevel Optimal -Force

Write-Host "[5/6] Verificando conexao ADB para deploy..." -ForegroundColor Cyan
$AdbDevices = (& adb devices) -join "`n"
if ($AdbDevices -match "\bdevice\b") {
    & adb push $ZipOut /data/local/tmp/ue4_instrument_zygisk.zip
    & adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
    Write-Host "[6/6] Modulo instalado com sucesso. Execute 'adb reboot' para ativar." -ForegroundColor Green
} else {
    Write-Host "[6/6] Arquivo gerado em $ZipOut (nenhum dispositivo ADB online)." -ForegroundColor Yellow
}
```



