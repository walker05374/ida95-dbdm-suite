// entry.cpp
// ============================================================================
// Architecture : ELF64 AArch64 (arm64-v8a) | Unreal Engine 4.27 (libUE4.so)
// Target       : com.netease.dbdena (Build 292949 — Grade A Verified Offsets)
// Standard     : C++20 (-std=c++20 -O3 -fno-exceptions -fno-rtti)
// ============================================================================
#include "visibility.hpp"
#include "elf_resolver.hpp"
#include "in_process_memory.hpp"
#include "zygisk.hpp"
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

#if defined(__ANDROID__)
#include <android/log.h>
#define DIAG_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO,  "UE4Diag", fmt __VA_OPT__(,) __VA_ARGS__)
#define DIAG_LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN,  "UE4Diag", fmt __VA_OPT__(,) __VA_ARGS__)
#define DIAG_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "UE4Diag", fmt __VA_OPT__(,) __VA_ARGS__)
#else
#include <cstdio>
#define DIAG_LOGI(fmt, ...) std::printf("[UE4Diag][INFO] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#define DIAG_LOGW(fmt, ...) std::printf("[UE4Diag][WARN] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#define DIAG_LOGE(fmt, ...) std::printf("[UE4Diag][ERR]  " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#endif

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
    // Global RVAs verified via IDA Pro static analysis of libUE4_fixed.so (Build 292949)
    uintptr_t gworld_rva                = 0x10D69998; // UWorldProxy (.bss) | GOT 0x0F668440
    uintptr_t gnamepool_rva             = 0x10B7C100; // FNamePool (.bss)   | FName::ToString @ 0x0A59F82C
    uintptr_t gnamepool_init_bool_rva   = 0x10BCF0C0; // bNamePoolInitialized (.bss) @ 0x0A59F818
    uintptr_t guobjectarray_rva         = 0x10BF7920; // FUObjectArray (.bss) | ObjObjects @ 0x10BF7930
    uintptr_t gengine_rva               = 0x10D65798; // UEngine* (.bss)    | GOT 0x0F6682A8

    // UWorld
    uintptr_t persistent_level          = 0x0038;     // ULevel* (LDR X0, [X0, #0x38] @ 0x0C78A6C0)
    uintptr_t owning_game_instance      = 0x0410;     // UGameInstance* (LDR X0, [X0, #0x410] @ 0x0C1BFDFC)

    // ULevel
    uintptr_t actor_array               = 0x00A0;     // TArray<AActor*>: Data (+0xA0), Count (+0xA8), Max (+0xAC)

    // UGameInstance -> ULocalPlayer -> APlayerController -> APawn / APlayerState
    uintptr_t local_players_array       = 0x0040;     // TArray<ULocalPlayer*> (FClassParams @ 0x0C85FE90)
    uintptr_t player_controller         = 0x0038;     // UPlayer::PlayerController (FClassParams @ 0x0C99FE40)
    uintptr_t acknowledged_pawn         = 0x02B8;     // APlayerController::AcknowledgedPawn (FClassParams @ 0x0F3B84D8)
    uintptr_t player_camera_manager     = 0x02D0;     // APlayerController::PlayerCameraManager (@ 0x0C1C019C)
    uintptr_t controller_player_state   = 0x0238;     // AController::PlayerState
    uintptr_t player_state              = 0x0248;     // APawn::PlayerState

    // UObjectBase / AActor / ACharacter (WITH_CASE_PRESERVING_NAME = 1 -> 12-byte FName)
    uintptr_t fname_comparison_index    = 0x0018;     // UObjectBase::NamePrivate.ComparisonIndex (+0x18)
    uintptr_t fname_index               = 0x001C;     // UObjectBase::NamePrivate.DisplayIndex (+0x1C)
    uintptr_t root_component            = 0x0140;     // AActor::RootComponent (FClassParams @ 0x0C7ABD84)
    uintptr_t character_movement        = 0x0290;     // ACharacter::CharacterMovement / Mesh

    // USceneComponent
    uintptr_t relative_location         = 0x0134;     // FVector (12 bytes)
    uintptr_t component_velocity        = 0x0140;     // FVector (12 bytes)

    // UCharacterMovementComponent
    uintptr_t max_walk_speed            = 0x018C;     // float
    uintptr_t max_acceleration          = 0x0198;     // float

    // APlayerState
    uintptr_t player_name_private       = 0x0310;     // FString (TArray<char16_t>)
};

// ============================================================================
// 2. GNamePool Safe Decoder (UE 4.27 FNamePool Block Architecture - Rev. 7)
// ============================================================================
class INSTRUMENT_LOCAL GNamePoolResolver {
public:
    static constexpr uint32_t  kBlockBitShift      = 16U;
    static constexpr uint32_t  kOffsetMask         = (1U << kBlockBitShift) - 1U;
    static constexpr uint32_t  kMaxBlocks          = 8192U;
    static constexpr size_t    kMaxNameLen         = 128U;
    static constexpr uintptr_t kCurrentBlockOffset = 0x0038U; // FNameEntryAllocator::CurrentBlock (+0x38)
    static constexpr uintptr_t kByteCursorOffset   = 0x003CU; // FNameEntryAllocator::CurrentByteCursor (+0x3C)
    static constexpr uintptr_t kBlocksOffset       = 0x0040U; // FNameEntryAllocator::Blocks (+0x40)
    static constexpr uint32_t  kStride             = 4U;      // WITH_CASE_PRESERVING_NAME = 1 (Stride = 4)
    static constexpr uintptr_t kEntryHeaderOffset  = 0x0004U; // +0x00 ComparisonId (4B), +0x04 Header (2B)
    static constexpr uintptr_t kEntryStringOffset  = 0x0006U; // +0x06 AnsiName[Len]

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
        std::span<const uint8_t> payload{reinterpret_cast<const uint8_t*>(staging.data()), byte_len};
        const Status write_st = InProcessMemory::safe_write(fstr->data_ptr, payload);
        if (write_st != Status::Ok) {
            return write_st;
        }
        const int32_t new_count = static_cast<int32_t>(clamped_len + 1U);
        return mem::write_data<int32_t>(fstring_addr + offsetof(TArrayHeader, count), new_count);
    }
};
} // namespace ue4

// ============================================================================
// 5. Runtime Context, Offset Validation Logging & Telemetry Worker Thread
// ============================================================================
struct INSTRUMENT_LOCAL InstrumentationState {
    std::atomic<bool>  running{false};
    std::atomic<bool>  thread_active{false};
    pthread_t          worker_thread{};
    elf::ModuleSpan    ue4_module{};
    ue4::EngineOffsets offsets{};
    uintptr_t          last_logged_uworld{0};
    uintptr_t          last_logged_ulevel{0};
    bool               gnamepool_verified{false};
};

INSTRUMENT_LOCAL static InstrumentationState g_state{};

INSTRUMENT_LOCAL inline void sleep_ms(uint32_t milliseconds) noexcept {
    struct timespec ts{};
    ts.tv_sec  = static_cast<time_t>(milliseconds / 1000U);
    ts.tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000UL);
    ::nanosleep(&ts, nullptr);
}

INSTRUMENT_LOCAL static void log_resolved_engine_anchors() noexcept {
    const uintptr_t base = g_state.ue4_module.base_address;
    const size_t    size = g_state.ue4_module.mapped_size;
    const auto&     off  = g_state.offsets;

    const uintptr_t gworld_abs      = base + off.gworld_rva;
    const uintptr_t gnamepool_abs   = base + off.gnamepool_rva;
    const uintptr_t gname_init_abs  = base + off.gnamepool_init_bool_rva;
    const uintptr_t guobject_abs    = base + off.guobjectarray_rva;
    const uintptr_t gengine_abs     = base + off.gengine_rva;

    const uintptr_t uworld_val      = mem::read<uintptr_t>(gworld_abs).value_or(0ULL);
    const uintptr_t gengine_val     = mem::read<uintptr_t>(gengine_abs).value_or(0ULL);
    const uint8_t   namepool_init   = mem::read<uint8_t>(gname_init_abs).value_or(0U);
    const uint32_t  current_block   = mem::read<uint32_t>(gnamepool_abs + ue4::GNamePoolResolver::kCurrentBlockOffset).value_or(0U);

    DIAG_LOGI(
        "libUE4.so resolved | Base=0x%llX | MappedSize=0x%zx (%zu MB)",
        static_cast<unsigned long long>(base),
        size,
        size / (1024U * 1024U)
    );
    DIAG_LOGI(
        "Anchors | GWorld=0x%llX (RVA +0x%llX -> *0x%llX) | GNamePool=0x%llX (RVA +0x%llX, Init=%u, CurBlock=%u)",
        static_cast<unsigned long long>(gworld_abs),
        static_cast<unsigned long long>(off.gworld_rva),
        static_cast<unsigned long long>(uworld_val),
        static_cast<unsigned long long>(gnamepool_abs),
        static_cast<unsigned long long>(off.gnamepool_rva),
        static_cast<unsigned>(namepool_init),
        static_cast<unsigned>(current_block)
    );
    DIAG_LOGI(
        "Globals | GUObjectArray=0x%llX (+0x%llX) | GEngine=0x%llX (+0x%llX -> *0x%llX)",
        static_cast<unsigned long long>(guobject_abs),
        static_cast<unsigned long long>(off.guobjectarray_rva),
        static_cast<unsigned long long>(gengine_abs),
        static_cast<unsigned long long>(off.gengine_rva),
        static_cast<unsigned long long>(gengine_val)
    );
}

INSTRUMENT_LOCAL static void execute_diagnostic_tick() noexcept {
    const uintptr_t base = g_state.ue4_module.base_address;
    if (base == 0) {
        return;
    }
    const auto& off = g_state.offsets;
    const uintptr_t gnamepool_abs = base + off.gnamepool_rva;
    const ue4::GNamePoolResolver name_pool{gnamepool_abs};

    // Verify FName(0) == "None" once GNamePool is initialized by the engine
    if (!g_state.gnamepool_verified) {
        std::array<char, 64> entry0_buf{};
        if (name_pool.resolve_name(0U, entry0_buf)) {
            g_state.gnamepool_verified = true;
            DIAG_LOGI(
                "GNamePool verified @ 0x%llX | EName(0) resolved to \"%s\"",
                static_cast<unsigned long long>(gnamepool_abs),
                entry0_buf.data()
            );
        }
    }

    // 1. Resolve UWorld pointer safely
    const uintptr_t gworld_abs = base + off.gworld_rva;
    const auto uworld = mem::read<uintptr_t>(gworld_abs);
    if (!uworld.has_value() || *uworld == 0) {
        return;
    }

    const auto ulevel = mem::read<uintptr_t>(*uworld + off.persistent_level);
    const auto game_instance = mem::read<uintptr_t>(*uworld + off.owning_game_instance);

    // Log once per UWorld / PersistentLevel transition for effortless adb logcat confirmation
    if (*uworld != g_state.last_logged_uworld ||
        (ulevel.has_value() && *ulevel != g_state.last_logged_ulevel))
    {
        g_state.last_logged_uworld = *uworld;
        g_state.last_logged_ulevel = ulevel.value_or(0ULL);
        int32_t actor_count = 0;
        if (ulevel.has_value() && *ulevel != 0) {
            if (const auto arr = mem::read<ue4::TArrayHeader>(*ulevel + off.actor_array)) {
                actor_count = arr->count;
            }
        }
        DIAG_LOGI(
            "World state active | UWorld=0x%llX | PersistentLevel=0x%llX (Actors=%d) | OwningGameInstance=0x%llX",
            static_cast<unsigned long long>(*uworld),
            static_cast<unsigned long long>(g_state.last_logged_ulevel),
            static_cast<int>(actor_count),
            static_cast<unsigned long long>(game_instance.value_or(0ULL))
        );
    }

    // 2. Traverse UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> AcknowledgedPawn
    uintptr_t    local_pawn      = 0;
    uintptr_t    local_player_st = 0;
    ue4::FVector local_origin{};
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
                    if (local_player_st == 0) {
                        if (const auto ctrl_ps = mem::read<uintptr_t>(*pc + off.controller_player_state)) {
                            local_player_st = *ctrl_ps;
                        }
                    }
                }
            }
        }
    }

    // 3. Execute kinematic parameter tuning and LocalPlayerNameController
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

    // 4. Traverse UWorld -> PersistentLevel -> Actors array and filter spatially
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
    DIAG_LOGI("Telemetry worker started; polling dl_iterate_phdr for libUE4.so...");

    while (g_state.running.load(std::memory_order_acquire)) {
        const auto mod = elf::find_module("libUE4.so");
        if (mod.has_value()) {
            std::array<uint8_t, sizeof(Elf64_Ehdr)> hdr{};
            if (InProcessMemory::safe_read(mod->base_address, hdr) == Status::Ok) {
                g_state.ue4_module = *mod;
                log_resolved_engine_anchors();
                break;
            }
        }
        sleep_ms(250);
    }

    while (g_state.running.load(std::memory_order_acquire)) {
        execute_diagnostic_tick();
        sleep_ms(16);
    }

    DIAG_LOGI("Telemetry worker exiting cleanly.");
    return nullptr;
}

INSTRUMENT_LOCAL bool initialize_instrumentation() noexcept {
    bool expected = false;
    if (!g_state.running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    DIAG_LOGI(
        "Initializing UE4.27 AArch64 Instrumentation (Configured RVAs: GWorld=0x%llX, GNamePool=0x%llX)",
        static_cast<unsigned long long>(g_state.offsets.gworld_rva),
        static_cast<unsigned long long>(g_state.offsets.gnamepool_rva)
    );

    pthread_attr_t attr{};
    if (pthread_attr_init(&attr) != 0) {
        g_state.running.store(false, std::memory_order_release);
        DIAG_LOGE("pthread_attr_init failed.");
        return false;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    const int rc = pthread_create(&g_state.worker_thread, &attr, &telemetry_worker_entry, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        g_state.running.store(false, std::memory_order_release);
        DIAG_LOGE("pthread_create failed with error code %d.", rc);
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
// 6. Public C-ABI / JNI & Zygisk Lifecycle Exports
// ============================================================================
namespace runtime {

class INSTRUMENT_LOCAL DbdZygiskModule final : public zygisk::ModuleBase {
public:
    void onLoad(void* api, JNIEnv* env) override {
        api_ = static_cast<zygisk::Api*>(api);
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!args || !args->nice_name || !env_) {
            if (api_) {
                api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            }
            return;
        }

        const char* raw_pkg = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw_pkg) {
            if (api_) {
                api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            }
            return;
        }

        const std::string_view proc_name{raw_pkg};
        is_target_process_ = (proc_name == "com.netease.dbdena");
        env_->ReleaseStringUTFChars(args->nice_name, raw_pkg);

        if (!is_target_process_ && api_) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs* /*args*/) override {
        if (is_target_process_) {
            DIAG_LOGI("Zygisk postAppSpecialize matched com.netease.dbdena; starting worker.");
            (void)initialize_instrumentation();
        }
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs* /*args*/) override {
        if (api_) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

private:
    zygisk::Api* api_{nullptr};
    JNIEnv*      env_{nullptr};
    bool         is_target_process_{false};
};

} // namespace runtime

REGISTER_ZYGISK_MODULE(runtime::DbdZygiskModule)

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
