// ============================================================================
// File          : jni/telemetry/ui_timing_validator.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a) | Unreal Engine 4.27 (libUE4.so)
// Standard      : C++20 (-std=c++20 -O3 -fno-exceptions -fno-rtti -Wall -Wextra)
// Purpose       : O(1) Tear-Free UI Timing Window & Gauge Telemetry Validator
// ============================================================================

#pragma once

#include "in_process_memory.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace hud::timing {

// ============================================================================
// 1. Offsets & RVAs Verificados (libUE4.so Build 292949 — Reflection Table)
// ============================================================================
struct UITimingOffsets {
    // UObjectBase Header (64-bit UE4.27 com WITH_CASE_PRESERVING_NAME = 1)
    static constexpr std::uintptr_t kUObjectVTable          = 0x0000; // void**
    static constexpr std::uintptr_t kUObjectFlags           = 0x0008; // EObjectFlags (uint32_t)
    static constexpr std::uintptr_t kUObjectInternalIndex   = 0x000C; // int32_t
    static constexpr std::uintptr_t kUObjectClassPrivate    = 0x0010; // UClass*

    // Flags de destruição de ciclo de vida do UObject (GC / PendingKill)
    static constexpr std::uint32_t  kRF_BeginDestroyed      = (1U << 15U); // 0x00008000
    static constexpr std::uint32_t  kRF_FinishDestroyed     = (1U << 16U); // 0x00010000
    static constexpr std::uint32_t  kRF_PendingKill         = (1U << 29U); // 0x20000000
    static constexpr std::uint32_t  kDeadObjectMask         =
        kRF_BeginDestroyed | kRF_FinishDestroyed | kRF_PendingKill;

    // Cadeia de resolução: ADBDPlayer -> UPlayerInteractionHandler -> USkillCheck (Build 292949 Verified)
    static constexpr std::uintptr_t kPlayerInteractionHandler = 0x0A90; // UPlayerInteractionHandler* ADBDPlayer::_interactionHandler (FPropertyParams @ 0x0EBD9188, ADBDPlayer::GetPlayerInteractionHandler @ 0x06FCD35C)
    static constexpr std::uintptr_t kHandlerCurrentInteraction= 0x0200; // UInteractionDefinition* UPlayerInteractionHandler::_currentInteraction (FPropertyParams @ 0x0EC8EFC8, FClassParams @ 0x0EC8F748)
    static constexpr std::uintptr_t kSkillCheckComponent      = 0x0290; // USkillCheck* UPlayerInteractionHandler::_skillCheck (FPropertyParams @ 0x0EC8F120, UPlayerInteractionHandler::GetSkillCheck @ 0x07B4E234)

    // USkillCheck (FClassParams @ 0x0ECB5158, StaticClass @ 0x07B89BE4, SizeOf = 0x0420 bytes)
    static constexpr std::uintptr_t kActivatedFlag            = 0x028C; // bool _activated (FPropertyParams @ 0x0ECB4E50, SetBitFunc @ 0x07B8B20C: STRB W8, [X0, #0x28C])
    static constexpr std::uintptr_t kDisplayedFlag            = 0x028D; // bool _displayed (FPropertyParams @ 0x0ECB4E78, SetBitFunc @ 0x07B8B218: STRB W8, [X0, #0x28D], USkillCheck::IsDisplayed @ 0x06E16FA8)
    static constexpr std::uintptr_t kWarningGivenFlag         = 0x028E; // bool _warningGiven (FPropertyParams @ 0x0ECB4EA0, SetBitFunc @ 0x07B8B224: STRB W8, [X0, #0x28E])
    static constexpr std::uintptr_t kStateFlags               = 0x028C; // Base byte of (_activated @ +0x028C, _displayed @ +0x028D, _warningGiven @ +0x028E)
    static constexpr std::uintptr_t kCurrentProgress          = 0x0290; // float _currentProgress (FPropertyParams @ 0x0ECB4EC8, OnSkillCheckInput @ 0x06E18F6C: LDR S0, [X20, #0x290])
    static constexpr std::uintptr_t kCurrentUIProgress        = 0x0294; // float _currentUIProgress (FPropertyParams @ 0x0ECB4EF0)
    static constexpr std::uintptr_t kXOffset                  = 0x0298; // int32_t xOffset (FPropertyParams @ 0x0ECB4F18)
    static constexpr std::uintptr_t kYOffset                  = 0x029C; // int32_t yOffset (FPropertyParams @ 0x0ECB4F40)
    static constexpr std::uintptr_t kGenerationCount          = 0x02A0; // int32_t _skillCheckCount (FPropertyParams @ 0x0ECB4F68)
    static constexpr std::uintptr_t kOwnerPawn                = 0x02B8; // ADBDPlayer* _owner (FPropertyParams @ 0x0ECB4F90)
    static constexpr std::uintptr_t kCurrentInteraction       = 0x02C0; // UChargeableInteractionDefinition* _currentInteraction (FPropertyParams @ 0x0ECB4FC0)
    static constexpr std::uintptr_t kCustomType               = 0x02C8; // uint8_t ESkillCheckCustomType _currentSkillCheckType (FPropertyParams @ 0x0ECB5020, OnSkillCheckInput @ 0x06E18FE8: LDRB W21, [X20, #0x2C8])
    static constexpr std::uintptr_t kLiveSuccessZoneStart     = 0x02CC; // float live SuccessZoneStart (OnSkillCheckInput @ 0x06E18F44: LDR S1, [X20, #0x2CC])
    static constexpr std::uintptr_t kLiveSuccessZoneEnd       = 0x02D0; // float live SuccessZoneEnd (OnSkillCheckInput @ 0x06E19000: LDR S1, [X20, #0x2D0])
    static constexpr std::uintptr_t kLiveBonusZoneLength      = 0x02D4; // float live BonusZoneLength (OnSkillCheckInput @ 0x06E18F54: LDR S1, [X20, #0x2D4])
    static constexpr std::uintptr_t kLiveBonusZoneStart       = 0x02D8; // float live BonusZoneStart (OnSkillCheckInput @ 0x06E19018: LDR S2, [X20, #0x2D8])
    static constexpr std::uintptr_t kLiveIsDisplayed          = 0x02F4; // uint8_t live bIsDisplayed (OnSkillCheckInput @ 0x06E18F38: LDRB W8, [X20, #0x2F4])
    static constexpr std::uintptr_t kDefinitionCache          = 0x02F8; // FSkillCheckDefinition _currentSkillCheckDefinitionCache (FPropertyParams @ 0x0ECB5050, 0x2C bytes)
    static constexpr std::uintptr_t kInternalStateEnum        = 0x0404; // uint32_t internal state enum (OnSkillCheckInput @ 0x06E19054: STR W8, [X19, #0x404])
    static constexpr std::size_t    kMinStructSpan            = 0x0420; // USkillCheck SizeOf = 0x0420 (FClassParams @ 0x0ECB5158)

    // Sub-offsets de FSkillCheckDefinition (base = +0x02F8, FPropertyParams @ 0x0ECB5050)
    static constexpr std::uintptr_t kDefSuccessZoneStart      = 0x02F8 + 0x00; // float SuccessZoneStart
    static constexpr std::uintptr_t kDefSuccessZoneEnd        = 0x02F8 + 0x04; // float SuccessZoneEnd
    static constexpr std::uintptr_t kDefBonusZoneLength       = 0x02F8 + 0x08; // float BonusZoneLength
    static constexpr std::uintptr_t kDefBonusZoneStart        = 0x02F8 + 0x0C; // float BonusZoneStart
    static constexpr std::uintptr_t kDefProgressRate          = 0x02F8 + 0x10; // float ProgressRate (unidades normalizadas/s)
    static constexpr std::uintptr_t kDefStartingTickerPos     = 0x02F8 + 0x14; // float StartingTickerPosition
    static constexpr std::uintptr_t kDefWarningSoundDelay     = 0x02F8 + 0x1C; // float WarningSoundDelay
    static constexpr std::uintptr_t kDefBoolFlags             = 0x02F8 + 0x28; // uint8_t (IsInsane, IsContinuous, etc.)

    // RVAs de Despacho Nativo em libUE4.so (Build 292949 Disassembly Proof)
    static constexpr std::uintptr_t kStaticClassRVA           = 0x07B89BE4; // USkillCheck::StaticClass()
    static constexpr std::uintptr_t kActivateCustomRVA        = 0x06E17384; // USkillCheck::Local_ActivateCustomSkillCheck (Z_Construct @ 0x07B8ABDC, ExecFunc @ 0x07B8A248 -> BL 0x06E17384)
    static constexpr std::uintptr_t kActivateStandardRVA      = 0x06E18208; // USkillCheck::Local_ActivateSkillCheck (Z_Construct @ 0x07B8AC28, ExecFunc @ 0x07B8A33C -> BL 0x06E18208)
    static constexpr std::uintptr_t kSuccessTriggerRVA        = 0x06E16E2C; // USkillCheck::Local_OnSkillCheckSuccessTrigger (Z_Construct @ 0x07B8ACFC, ExecFunc @ 0x07B895E8 -> BL 0x06E16E2C)
    static constexpr std::uintptr_t kOnSkillCheckInputRVA     = 0x06E18CF0; // USkillCheck::OnSkillCheckInput (Z_Construct @ 0x07B8AFA8, ExecFunc @ 0x07B8A234 -> B 0x06E18CF0)
};

// ============================================================================
// 2. Layouts Binários POD (Compatíveis com ABI ARM64 da UE4.27)
// ============================================================================
#pragma pack(push, 1)
struct FSkillCheckDefinitionPOD {
    float        success_zone_start{0.0f};       // +0x00
    float        success_zone_end{0.0f};         // +0x04
    float        bonus_zone_length{0.0f};        // +0x08
    float        bonus_zone_start{0.0f};         // +0x0C
    float        progress_rate{0.0f};            // +0x10
    float        starting_ticker_pos{0.0f};      // +0x14
    std::int32_t reserved_pad_18{0};             // +0x18
    float        warning_sound_delay{0.0f};      // +0x1C
    std::int32_t reserved_pad_20{0};             // +0x20
    std::int32_t reserved_pad_24{0};             // +0x24
    std::uint8_t is_insane{0};                   // +0x28
    std::uint8_t is_continuous{0};               // +0x29
    std::uint8_t is_one_shot{0};                 // +0x2A
    std::uint8_t pad_2b{0};                      // +0x2B
};
static_assert(sizeof(FSkillCheckDefinitionPOD) == 0x2C, "FSkillCheckDefinitionPOD size mismatch");

// Bloco de parâmetros de UFunction para Local_OnSkillCheckSuccessTrigger (@ 0x07B8AD94)
struct FNativeSuccessTriggerParams {
    std::uint8_t bonus{1};                       // +0x00: true = Bonus/Sweet-Spot Zone
    std::uint8_t insane{0};                      // +0x01: bool insane
    std::uint8_t skill_check_custom_type{0};     // +0x02: ESkillCheckCustomType
    std::uint8_t failed_counted_as_good{0};      // +0x03: bool failedCountedAsGood
};
static_assert(sizeof(FNativeSuccessTriggerParams) == 0x04, "FNativeSuccessTriggerParams must be 4 bytes");
#pragma pack(pop)

enum class TimingZoneClass : std::uint8_t {
    Inactive       = 0,
    PreWindow      = 1,
    BonusSweetSpot = 2, // Zona ótima (Great / Bonus)
    StandardGood   = 3, // Zona regular (Good)
    PostWindowMiss = 4
};

enum class ValidationRejectReason : std::uint8_t {
    None                 = 0,
    ValidatorDisabled    = 1,
    InvalidPointerOrGC   = 2,
    TornSnapshotRead     = 3,
    InactiveWidget       = 4,
    OwnershipMismatch    = 5,
    AlreadyTriggeredGen  = 6,
    NonFiniteMathState   = 7,
    OutsideTargetWindow  = 8
};

struct alignas(64) UITimingSnapshot {
    std::uintptr_t           component_addr{0};
    std::uintptr_t           owner_pawn_addr{0};
    std::int32_t             generation_id{-1};
    float                    raw_progress{0.0f};
    float                    ui_progress{0.0f};
    float                    predicted_progress{0.0f};
    FSkillCheckDefinitionPOD definition{};
    std::uint8_t             custom_type{0};
    std::uint8_t             state_flags{0};
    bool                     is_activated{false};
    bool                     is_displayed{false};
    TimingZoneClass          current_zone{TimingZoneClass::Inactive};
    ValidationRejectReason   reject_reason{ValidationRejectReason::None};
    bool                     should_trigger_now{false};
    float                    lead_compensation_units{0.0f};
    std::uint64_t            timestamp_ns{0};
};

// ============================================================================
// 3. Relógio Monotônico e Preditor EMA de Delta Time (Tick <-> Render GLES3)
// ============================================================================
struct FrameLatencyCompensator {
    float         ema_tick_dt_sec{0.016667f};    // ~60 Hz baseline
    float         ema_render_dt_sec{0.008333f};  // ~120 Hz / meia fase de swap
    float         pipeline_jitter_sec{0.0020f};  // Margem de despacho de input (2.0ms)
    std::uint64_t last_tick_ns{0};
    std::uint64_t last_render_ns{0};

    [[nodiscard]] static std::uint64_t now_monotonic_ns() noexcept {
#if defined(__linux__) || defined(__ANDROID__)
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL) +
               static_cast<std::uint64_t>(ts.tv_nsec);
#else
        return 0ULL;
#endif
    }

    // Atualiza o EMA do tick da thread de telemetria em O(1)
    void record_telemetry_tick(std::uint64_t now_ns) noexcept {
        if (last_tick_ns != 0 && now_ns > last_tick_ns) {
            const float dt = static_cast<float>(now_ns - last_tick_ns) * 1e-9f;
            if (dt >= 0.0005f && dt <= 0.250f) {
                ema_tick_dt_sec = (0.85f * ema_tick_dt_sec) + (0.15f * dt);
            }
        }
        last_tick_ns = now_ns;
    }

    // Chamado no hook de eglSwapBuffers / ImGui Render para medir a defasagem real até o frame
    void record_render_present(std::uint64_t now_ns) noexcept {
        if (last_render_ns != 0 && now_ns > last_render_ns) {
            const float dt = static_cast<float>(now_ns - last_render_ns) * 1e-9f;
            if (dt >= 0.0005f && dt <= 0.250f) {
                ema_render_dt_sec = (0.85f * ema_render_dt_sec) + (0.15f * dt);
            }
        }
        last_render_ns = now_ns;
    }

    // Calcula o horizonte temporal de predição (em segundos) entre a leitura atual e a efetivação no frame
    [[nodiscard]] constexpr float compute_lookahead_seconds(std::uint64_t sample_ns) const noexcept {
        float phase_lag_sec = 0.5f * ema_render_dt_sec;
        if (last_render_ns != 0 && sample_ns > last_render_ns) {
            const float elapsed_since_present = static_cast<float>(sample_ns - last_render_ns) * 1e-9f;
            if (elapsed_since_present >= 0.0f && elapsed_since_present < ema_render_dt_sec) {
                phase_lag_sec = ema_render_dt_sec - elapsed_since_present;
            }
        }
        const float total_lookahead = (0.5f * ema_tick_dt_sec) + phase_lag_sec + pipeline_jitter_sec;
        return std::clamp(total_lookahead, 0.0f, 0.050f); // Teto rígido de 50ms
    }
};

// ============================================================================
// 4. Controlador e Validador O(1) de Precisão Temporal (UI Timing Validator)
// ============================================================================
class alignas(64) UITimingValidatorController {
public:
    static constexpr std::size_t kHistoryRingSize = 64U; // Potência de 2 para máscara O(1)

    using NativeSuccessDispatchFn = void(*)(
        std::uintptr_t                     skillcheck_instance,
        std::uintptr_t                     rva_offset,
        const FNativeSuccessTriggerParams& params) noexcept;

    // ------------------------------------------------------------------------
    // Validação Canônica AArch64 + Verificação de Ciclo de Vida UObject (GC-Safe)
    // ------------------------------------------------------------------------
    [[nodiscard]] static constexpr bool is_canonical_aarch64_ptr(std::uintptr_t addr) noexcept {
        const std::uintptr_t untagged = addr & 0x0000FFFFFFFFFFFFULL;
        return (untagged >= 0x00010000ULL && untagged <= 0x00007FFFFFFFFFFFULL) &&
               ((untagged & 0x07ULL) == 0ULL); // Alinhado em 8 bytes
    }

    [[nodiscard]] static bool is_live_uobject(std::uintptr_t obj_addr, std::size_t min_span) noexcept {
        if (!is_canonical_aarch64_ptr(obj_addr)) [[unlikely]] {
            return false;
        }
        if (!runtime::memory::InProcessMemory::IsValidPtr(obj_addr, min_span)) [[unlikely]] {
            return false;
        }

        struct UObjectHeaderProbe {
            std::uintptr_t vtable;
            std::uint32_t  object_flags;
            std::int32_t   internal_index;
            std::uintptr_t class_private;
        };

        UObjectHeaderProbe hdr{};
        std::span<std::uint8_t> dst{reinterpret_cast<std::uint8_t*>(&hdr), sizeof(hdr)};
        if (runtime::memory::InProcessMemory::safe_read(obj_addr, dst) != runtime::memory::Status::Ok) [[unlikely]] {
            return false;
        }

        if (!is_canonical_aarch64_ptr(hdr.vtable) || !is_canonical_aarch64_ptr(hdr.class_private)) [[unlikely]] {
            return false;
        }
        if ((hdr.object_flags & UITimingOffsets::kDeadObjectMask) != 0U) [[unlikely]] {
            return false;
        }
        return (hdr.internal_index >= 0 && hdr.internal_index < 0x400000);
    }

    // ------------------------------------------------------------------------
    // Leitura Atômica Tear-Free (Protocolo Seqlock de Dupla Verificação em Memória)
    // Garante que se o widget fechar ou mudar de geração no meio da leitura, a amostra é descartada.
    // ------------------------------------------------------------------------
    [[nodiscard]] static bool read_tear_free_snapshot(
        std::uintptr_t    skillcheck_addr,
        UITimingSnapshot& out_snap) noexcept
    {
        if (!is_live_uobject(skillcheck_addr, UITimingOffsets::kMinStructSpan)) [[unlikely]] {
            out_snap.reject_reason = ValidationRejectReason::InvalidPointerOrGC;
            return false;
        }

        // Passo 1: Lê guardas de pré-condição (flags de estado + contador de geração + ObjectFlags)
        std::uint8_t  act_pre   = 0;
        std::uint8_t  disp_pre  = 0;
        std::int32_t  gen_pre   = -1;
        std::uint32_t obj_flags = 0;

        if (!read_scalar(skillcheck_addr + UITimingOffsets::kActivatedFlag, act_pre) ||
            !read_scalar(skillcheck_addr + UITimingOffsets::kDisplayedFlag, disp_pre) ||
            !read_scalar(skillcheck_addr + UITimingOffsets::kGenerationCount, gen_pre) ||
            !read_scalar(skillcheck_addr + UITimingOffsets::kUObjectFlags, obj_flags)) [[unlikely]] {
            out_snap.reject_reason = ValidationRejectReason::InvalidPointerOrGC;
            return false;
        }

        if ((obj_flags & UITimingOffsets::kDeadObjectMask) != 0U) [[unlikely]] {
            out_snap.reject_reason = ValidationRejectReason::InvalidPointerOrGC;
            return false;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        // Passo 2: Lê o bloco contíguo principal [0x0290 .. 0x0324] (0x94 bytes) em uma única syscall/cópia
        constexpr std::size_t kCoreBlockStart = UITimingOffsets::kCurrentProgress; // 0x0290
        constexpr std::size_t kCoreBlockEnd   = UITimingOffsets::kDefinitionCache + sizeof(FSkillCheckDefinitionPOD); // 0x0324
        constexpr std::size_t kCoreBlockSize  = kCoreBlockEnd - kCoreBlockStart;   // 0x94 bytes

        alignas(8) std::array<std::uint8_t, kCoreBlockSize> core_buf{};
        if (runtime::memory::InProcessMemory::safe_read(
                skillcheck_addr + kCoreBlockStart,
                std::span<std::uint8_t>{core_buf.data(), core_buf.size()}) != runtime::memory::Status::Ok) [[unlikely]] {
            out_snap.reject_reason = ValidationRejectReason::InvalidPointerOrGC;
            return false;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        // Passo 3: Re-lê guardas de pós-condição para detectar fechamento/mutação concorrente pela GameThread
        std::uint8_t  act_post       = 0;
        std::uint8_t  disp_post      = 0;
        std::int32_t  gen_post       = -2;
        std::uint32_t obj_flags_post = 0;

        if (!read_scalar(skillcheck_addr + UITimingOffsets::kActivatedFlag, act_post) ||
            !read_scalar(skillcheck_addr + UITimingOffsets::kDisplayedFlag, disp_post) ||
            !read_scalar(skillcheck_addr + UITimingOffsets::kGenerationCount, gen_post) ||
            !read_scalar(skillcheck_addr + UITimingOffsets::kUObjectFlags, obj_flags_post)) [[unlikely]] {
            out_snap.reject_reason = ValidationRejectReason::TornSnapshotRead;
            return false;
        }

        if ((obj_flags_post & UITimingOffsets::kDeadObjectMask) != 0U ||
            act_pre != act_post ||
            disp_pre != disp_post ||
            gen_pre != gen_post ||
            gen_pre < 0) [[unlikely]] {
            out_snap.reject_reason = ValidationRejectReason::TornSnapshotRead;
            return false;
        }

        // Passo 4: Extrai os campos tipados do buffer local imutável
        auto extract_at = [&core_buf]<typename T>(std::size_t abs_offset) noexcept -> T {
            T val{};
            std::memcpy(&val, core_buf.data() + (abs_offset - kCoreBlockStart), sizeof(T));
            return val;
        };

        const std::uint8_t live_disp = extract_at.template operator()<std::uint8_t>(UITimingOffsets::kLiveIsDisplayed);
        const std::uint8_t combined_flags =
            ((act_post != 0U) ? 0x01U : 0U) |
            (((disp_post != 0U) || (live_disp != 0U)) ? 0x02U : 0U);

        out_snap.component_addr  = skillcheck_addr;
        out_snap.raw_progress    = extract_at.template operator()<float>(UITimingOffsets::kCurrentProgress);
        out_snap.ui_progress     = extract_at.template operator()<float>(UITimingOffsets::kCurrentUIProgress);
        out_snap.generation_id   = gen_post;
        out_snap.owner_pawn_addr = extract_at.template operator()<std::uintptr_t>(UITimingOffsets::kOwnerPawn);
        out_snap.custom_type     = extract_at.template operator()<std::uint8_t>(UITimingOffsets::kCustomType);
        out_snap.definition      = extract_at.template operator()<FSkillCheckDefinitionPOD>(UITimingOffsets::kDefinitionCache);
        out_snap.state_flags     = combined_flags;
        out_snap.is_activated    = (combined_flags & 0x01U) != 0U;
        out_snap.is_displayed    = (combined_flags & 0x02U) != 0U;

        return true;
    }

    // ------------------------------------------------------------------------
    // Matemática de Predição de Frame e Classificação de Sweet-Spot em O(1)
    // Suporta Ponteiros Horários (ProgressRate > 0) e Anti-Horários/Reversos (ProgressRate < 0)
    // ------------------------------------------------------------------------
    [[nodiscard]] UITimingSnapshot evaluate_and_validate_tick(
        std::uintptr_t        skillcheck_addr,
        std::uintptr_t        expected_local_pawn,
        bool                  validator_enabled,
        bool                  allow_first_frame_bonus_dispatch = false,
        NativeSuccessDispatchFn dispatch_fn                    = nullptr) noexcept
    {
        UITimingSnapshot snap{};
        snap.timestamp_ns = latency_.now_monotonic_ns();
        latency_.record_telemetry_tick(snap.timestamp_ns);

        if (!validator_enabled) {
            // Reset imediato de trava em fallback manual (Guard 3)
            last_triggered_component_.store(0ULL, std::memory_order_relaxed);
            last_triggered_gen_.store(-1, std::memory_order_relaxed);
            snap.reject_reason = ValidationRejectReason::ValidatorDisabled;
            return snap;
        }

        if (!read_tear_free_snapshot(skillcheck_addr, snap)) {
            return snap;
        }

        // Guard de Ativação: O componente precisa estar ativo e visível na UI
        if (!snap.is_activated && !snap.is_displayed) {
            snap.current_zone  = TimingZoneClass::Inactive;
            snap.reject_reason = ValidationRejectReason::InactiveWidget;
            return snap;
        }

        // Guard de Propriedade Local (_owner == local_survivor_pawn)
        if (!is_canonical_aarch64_ptr(snap.owner_pawn_addr) ||
            (expected_local_pawn != 0 && snap.owner_pawn_addr != expected_local_pawn)) {
            snap.reject_reason = ValidationRejectReason::OwnershipMismatch;
            return snap;
        }

        // Guard de Execução Única O(1) por par (component_addr, _skillCheckCount)
        if (has_already_triggered(snap.component_addr, snap.generation_id)) {
            snap.reject_reason = ValidationRejectReason::AlreadyTriggeredGen;
            return snap;
        }

        // Validação de Sanidade Numérica (Fail-Closed para NaN/Inf)
        if (!std::isfinite(snap.raw_progress) ||
            !std::isfinite(snap.ui_progress) ||
            !std::isfinite(snap.definition.success_zone_start) ||
            !std::isfinite(snap.definition.success_zone_end) ||
            !std::isfinite(snap.definition.bonus_zone_start) ||
            !std::isfinite(snap.definition.bonus_zone_length) ||
            !std::isfinite(snap.definition.progress_rate)) [[unlikely]] {
            snap.reject_reason = ValidationRejectReason::NonFiniteMathState;
            return snap;
        }

        // Compensação Preditiva de Latência (Tick -> GLES3 Render Present)
        // Usa a maior leitura coerente entre _currentProgress e _currentUIProgress na direção do movimento
        const float rate      = snap.definition.progress_rate;
        const float lookahead = latency_.compute_lookahead_seconds(snap.timestamp_ns);
        const float lead_disp = rate * lookahead;

        const float base_progress = (rate >= 0.0f)
            ? std::max(snap.raw_progress, snap.ui_progress)
            : std::min(snap.raw_progress, snap.ui_progress);

        const float predicted = std::clamp(base_progress + lead_disp, 0.0f, 1.0f);
        snap.predicted_progress      = predicted;
        snap.lead_compensation_units = lead_disp;

        // Normalização dos limites da janela ótima (Bonus / Great) e regular (Success / Good)
        const float bonus_lo = std::clamp(
            std::min(snap.definition.bonus_zone_start,
                     snap.definition.bonus_zone_start + snap.definition.bonus_zone_length),
            0.0f, 1.0f);
        const float bonus_hi = std::clamp(
            std::max(snap.definition.bonus_zone_start,
                     snap.definition.bonus_zone_start + snap.definition.bonus_zone_length),
            0.0f, 1.0f);

        const float succ_lo = std::clamp(
            std::min(snap.definition.success_zone_start, snap.definition.success_zone_end),
            0.0f, 1.0f);
        const float succ_hi = std::clamp(
            std::max(snap.definition.success_zone_start, snap.definition.success_zone_end),
            0.0f, 1.0f);

        // Sub-janela central do Sweet-Spot (evita disparo no limite exato de borda onde float rounding falha)
        const float bonus_span   = bonus_hi - bonus_lo;
        const float edge_guard   = std::min(bonus_span * 0.10f, 0.0035f);
        const float sweet_min    = bonus_lo + edge_guard;
        const float sweet_max    = bonus_hi - edge_guard;

        // Verifica interseção por varredura de segmento [base_progress, predicted] para nunca pular
        // uma zona estreita em quedas bruscas de FPS (ex.: 30 FPS -> delta de ~33ms)
        const float seg_min = std::min(base_progress, predicted);
        const float seg_max = std::max(base_progress, predicted);

        const bool in_sweet_spot =
            (bonus_span > 1e-4f) && (seg_max >= sweet_min) && (seg_min <= sweet_max);
        const bool in_good_zone  =
            (succ_hi > succ_lo)  && (seg_max >= succ_lo)   && (seg_min <= succ_hi);

        if (in_sweet_spot) {
            snap.current_zone = TimingZoneClass::BonusSweetSpot;
        } else if (in_good_zone) {
            snap.current_zone = TimingZoneClass::StandardGood;
        } else if ((rate >= 0.0f && predicted < succ_lo) || (rate < 0.0f && predicted > succ_hi)) {
            snap.current_zone = TimingZoneClass::PreWindow;
        } else {
            snap.current_zone = TimingZoneClass::PostWindowMiss;
        }

        // Condição de disparo:
        // Modo A (Sweet-Spot / Gauge QA): Dispara quando o intervalo predito cruza a sub-janela ótima (BonusSweetSpot)
        // Modo B (First-Frame Direct Resolution): Dispara já no Frame 1 de ativação se allow_first_frame_bonus_dispatch = true
        if (in_sweet_spot || allow_first_frame_bonus_dispatch) {
            mark_triggered(snap.component_addr, snap.generation_id);
            snap.should_trigger_now = true;
            snap.current_zone       = TimingZoneClass::BonusSweetSpot;
            snap.reject_reason      = ValidationRejectReason::None;

            if (dispatch_fn != nullptr) {
                const FNativeSuccessTriggerParams params{
                    .bonus                  = 1U,
                    .insane                 = static_cast<std::uint8_t>(snap.definition.is_insane != 0 ? 1U : 0U),
                    .skill_check_custom_type= snap.custom_type,
                    .failed_counted_as_good = 0U
                };
                dispatch_fn(snap.component_addr, UITimingOffsets::kSuccessTriggerRVA, params);
            }
        } else {
            snap.should_trigger_now = false;
            snap.reject_reason      = ValidationRejectReason::OutsideTargetWindow;
        }

        publish_snapshot(snap);
        return snap;
    }

    // Notifica a apresentação de um frame no GLES3 (eglSwapBuffers) para calibrar a fase de renderização
    void notify_gles3_frame_presented() noexcept {
        latency_.record_render_present(FrameLatencyCompensator::now_monotonic_ns());
    }

    // Leitura Lock-Free O(1) do último estado para renderização no overlay ImGui
    [[nodiscard]] const UITimingSnapshot& read_latest_hud_snapshot() const noexcept {
        const std::uint32_t idx = published_slot_.load(std::memory_order_acquire) & 1U;
        return hud_snapshots_[idx];
    }

    // Reinicializa estado e limpa o anel de execução única (chamado no boot do módulo ou fallback manual)
    void reset_state() noexcept {
        last_triggered_component_.store(0ULL, std::memory_order_release);
        last_triggered_gen_.store(-1, std::memory_order_release);
        ring_cursor_ = 0;
        for (auto& entry : seen_ring_) {
            entry = {0ULL, -1};
        }
    }

private:
    template <typename T>
    [[nodiscard]] static bool read_scalar(std::uintptr_t addr, T& out_val) noexcept {
        std::span<std::uint8_t> slice{reinterpret_cast<std::uint8_t*>(&out_val), sizeof(T)};
        return runtime::memory::InProcessMemory::safe_read(addr, slice) == runtime::memory::Status::Ok;
    }

    [[nodiscard]] bool has_already_triggered(std::uintptr_t comp_addr, std::int32_t gen_id) const noexcept {
        if (last_triggered_component_.load(std::memory_order_relaxed) == comp_addr &&
            last_triggered_gen_.load(std::memory_order_relaxed) == gen_id) {
            return true;
        }
        for (const auto& entry : seen_ring_) {
            if (entry.first == comp_addr && entry.second == gen_id) {
                return true;
            }
        }
        return false;
    }

    void mark_triggered(std::uintptr_t comp_addr, std::int32_t gen_id) noexcept {
        last_triggered_component_.store(comp_addr, std::memory_order_relaxed);
        last_triggered_gen_.store(gen_id, std::memory_order_relaxed);
        const std::size_t slot = (ring_cursor_++) & (kHistoryRingSize - 1U);
        seen_ring_[slot] = {comp_addr, gen_id};
    }

    void publish_snapshot(const UITimingSnapshot& snap) noexcept {
        const std::uint32_t next_slot = (published_slot_.load(std::memory_order_relaxed) ^ 1U) & 1U;
        hud_snapshots_[next_slot] = snap;
        published_slot_.store(next_slot, std::memory_order_release);
    }

    FrameLatencyCompensator                                            latency_{};
    std::atomic<std::uintptr_t>                                        last_triggered_component_{0};
    std::atomic<std::int32_t>                                          last_triggered_gen_{-1};
    std::size_t                                                        ring_cursor_{0};
    std::array<std::pair<std::uintptr_t, std::int32_t>, kHistoryRingSize> seen_ring_{};
    std::atomic<std::uint32_t>                                         published_slot_{0};
    std::array<UITimingSnapshot, 2>                                    hud_snapshots_{};
};

// Instância global thread-safe do controlador O(1) de janela de tempo
[[nodiscard]] inline UITimingValidatorController& get_timing_validator() noexcept {
    static UITimingValidatorController s_instance{};
    return s_instance;
}

} // namespace hud::timing

