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
#include "imgui_gles3_hud.hpp"
#include "telemetry_adapter.hpp"
#include "egl_plt_bridge.hpp"
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
    uintptr_t uobject_flags             = 0x0008;     // UObjectBase::ObjectFlags (int32, +0x08)
    uintptr_t uobject_internal_index    = 0x000C;     // UObjectBase::InternalIndex (int32, +0x0C)
    uintptr_t uobject_class_private     = 0x0010;     // UObjectBase::ClassPrivate (UClass*, +0x10)
    uintptr_t fname_comparison_index    = 0x0018;     // UObjectBase::NamePrivate.ComparisonIndex (+0x18)
    uintptr_t fname_index               = 0x001C;     // UObjectBase::NamePrivate.DisplayIndex (+0x1C)
    uintptr_t custom_time_dilation      = 0x0098;     // AActor::CustomTimeDilation (float, +0x98)
    uintptr_t root_component            = 0x0140;     // AActor::RootComponent (FClassParams @ 0x0C7ABD84)
    uintptr_t character_movement        = 0x0290;     // ACharacter::CharacterMovement / Mesh

    // APlayerCameraManager -> CameraCachePrivate (+0x1AF0) -> POV (+0x1B00)
    uintptr_t camera_cache_private      = 0x1AF0;     // FCameraCacheEntry (FClassParams @ 0x0F3B0910)
    uintptr_t pov_location              = 0x1AF0 + 0x0010; // FMinimalViewInfo::Location (+0x1B00, FVector)
    uintptr_t pov_rotation              = 0x1AF0 + 0x001C; // FMinimalViewInfo::Rotation (+0x1B0C, FRotator)
    uintptr_t pov_fov                   = 0x1AF0 + 0x0028; // FMinimalViewInfo::FOV (+0x1B18, float)

    // USceneComponent
    uintptr_t relative_location         = 0x0134;     // FVector (12 bytes)
    uintptr_t component_velocity        = 0x0140;     // FVector (12 bytes)
    uintptr_t scene_comp_flags          = 0x0164;     // uint16 bitfield (bVisible=0x0040, bHiddenInGame=0x1000)

    // FUObjectArray / FChunkedFixedUObjectArray (for live lifecycle / PendingKill verification)
    uintptr_t guobject_objects_offset   = 0x0010;     // ObjObjects.Objects (FUObjectItem**)
    uintptr_t guobject_max_elems_offset = 0x0020;     // ObjObjects.MaxElements (int32)
    uintptr_t guobject_num_elems_offset = 0x0024;     // ObjObjects.NumElements (int32)
    uintptr_t guobject_num_chunks_offset= 0x002C;     // ObjObjects.NumChunks (int32)

    // UCharacterMovementComponent
    uintptr_t jump_z_velocity           = 0x0158;     // float (+0x158)
    uintptr_t max_walk_speed            = 0x018C;     // float (+0x18C)
    uintptr_t max_acceleration          = 0x0198;     // float (+0x198)

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
    static constexpr size_t   kMaxActorsPerTick      = 2048U;
    static constexpr size_t   kMaxSnapshots          = 64U;
    static constexpr uint32_t kFlagBeginDestroyed    = 0x00020000U; // RF_BeginDestroyed
    static constexpr uint32_t kFlagFinishDestroyed   = 0x00040000U; // RF_FinishDestroyed
    static constexpr int32_t  kInternalFlagPendingKill = (1 << 29);  // EInternalObjectFlags::PendingKill
    static constexpr int32_t  kInternalFlagUnreachable = (1 << 28);  // EInternalObjectFlags::Unreachable
    static constexpr int32_t  kInternalFlagGarbage     = (1 << 21);  // EInternalObjectFlags::Garbage

    struct UObjectHeaderSnapshot {
        uintptr_t vtable{0};
        uint32_t  object_flags{0};
        int32_t   internal_index{-1};
        uintptr_t class_private{0};
        uint32_t  fname_comparison_index{0};
        uint32_t  fname_display_index{0};
    };

    // Validates that a UObject* is live, mapped in canonical user-space, not RF_BeginDestroyed /
    // RF_FinishDestroyed, and (when GUObjectArray is accessible) matches its FUObjectItem slot
    // without PendingKill / Unreachable / Garbage flags.
    [[nodiscard]] static bool validate_live_uobject(
        uintptr_t obj_addr,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        uint32_t* out_fname_display_idx = nullptr
    ) noexcept {
        if (!InProcessMemory::IsValidPtr(obj_addr, sizeof(UObjectHeaderSnapshot))) {
            return false;
        }

        const auto hdr = mem::read<UObjectHeaderSnapshot>(obj_addr);
        if (!hdr.has_value()) {
            return false;
        }

        // VTable and ClassPrivate must be valid user-space pointers
        if (!InProcessMemory::IsValidPtr(hdr->vtable, sizeof(uintptr_t)) ||
            !InProcessMemory::IsValidPtr(hdr->class_private, sizeof(uintptr_t))) {
            return false;
        }

        // Reject objects undergoing UE4 destruction (RF_BeginDestroyed | RF_FinishDestroyed)
        if ((hdr->object_flags & (kFlagBeginDestroyed | kFlagFinishDestroyed)) != 0U) {
            return false;
        }

        // InternalIndex must be within valid GUObjectArray bounds
        if (hdr->internal_index <= 0 || hdr->internal_index >= 0x400000) {
            return false;
        }

        if (hdr->fname_display_index == 0U || hdr->fname_display_index >= (8192U << 16U)) {
            return false;
        }

        // Cross-check against GUObjectArray (FUObjectItem: +0x00 Object*, +0x08 Flags)
        if (InProcessMemory::IsValidPtr(guobjectarray_abs, 0x30U)) {
            const auto num_elems  = mem::read<int32_t>(guobjectarray_abs + off.guobject_num_elems_offset);
            const auto num_chunks = mem::read<int32_t>(guobjectarray_abs + off.guobject_num_chunks_offset);
            const auto objects_pp = mem::read<uintptr_t>(guobjectarray_abs + off.guobject_objects_offset);

            if (num_elems.has_value() && num_chunks.has_value() && objects_pp.has_value() &&
                *num_elems > 0 && *num_chunks > 0 && *num_chunks <= 512 &&
                InProcessMemory::IsValidPtr(*objects_pp, sizeof(uintptr_t)))
            {
                const int32_t idx = hdr->internal_index;
                if (idx >= *num_elems) {
                    return false;
                }
                const int32_t chunk_idx = idx >> 16;
                const int32_t within_idx = idx & 0xFFFF;
                if (chunk_idx < 0 || chunk_idx >= *num_chunks) {
                    return false;
                }
                const auto chunk_base = mem::read<uintptr_t>(
                    *objects_pp + static_cast<uintptr_t>(chunk_idx) * sizeof(uintptr_t));
                if (!chunk_base.has_value() || !InProcessMemory::IsValidPtr(*chunk_base, 0x18U)) {
                    return false;
                }
                const uintptr_t item_addr = *chunk_base + static_cast<uintptr_t>(within_idx) * 0x18ULL;
                const auto item_obj   = mem::read<uintptr_t>(item_addr + 0x00U);
                const auto item_flags = mem::read<int32_t>(item_addr + 0x08U);
                if (!item_obj.has_value() || !item_flags.has_value()) {
                    return false;
                }
                // If the slot no longer points to this object, it was destroyed/reallocated mid-frame
                if (InProcessMemory::untag_address(*item_obj) != InProcessMemory::untag_address(obj_addr)) {
                    return false;
                }
                // Check EInternalObjectFlags::PendingKill | Unreachable | Garbage
                if ((*item_flags & (kInternalFlagPendingKill | kInternalFlagUnreachable | kInternalFlagGarbage)) != 0) {
                    return false;
                }
            }
        }

        if (out_fname_display_idx != nullptr) {
            *out_fname_display_idx = hdr->fname_display_index;
        }
        return true;
    }

    [[nodiscard]] static std::optional<FVector> read_actor_location(
        uintptr_t actor,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!InProcessMemory::IsValidPtr(actor, 0x150U)) {
            return std::nullopt;
        }
        const auto root_comp = mem::read<uintptr_t>(actor + off.root_component);
        if (!root_comp.has_value() || !InProcessMemory::IsValidPtr(*root_comp, 0x170U)) {
            return std::nullopt;
        }
        // Verify the USceneComponent itself is still a live UObject (not destroyed when an item depletes or pawn dies)
        if (!validate_live_uobject(*root_comp, guobjectarray_abs, off, nullptr)) {
            return std::nullopt;
        }
        const auto loc = mem::read<FVector>(*root_comp + off.relative_location);
        if (!loc.has_value()) {
            return std::nullopt;
        }
        if (!std::isfinite(loc->x) || !std::isfinite(loc->y) || !std::isfinite(loc->z)) {
            return std::nullopt;
        }
        // Reject uninitialized/corrupted transform coordinates
        if (std::abs(loc->x) > 1000000.0f || std::abs(loc->y) > 1000000.0f || std::abs(loc->z) > 1000000.0f) {
            return std::nullopt;
        }
        return loc;
    }

    static Status apply_kinematic_limits(
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        float target_walk_speed,
        float target_accel,
        float target_jump_z = 0.0f,
        float target_time_scale = 0.0f
    ) noexcept {
        if (!validate_live_uobject(local_pawn, guobjectarray_abs, off, nullptr)) {
            return Status::InvalidArgument;
        }
        if (target_time_scale >= 0.1f && target_time_scale <= 5.0f) {
            const Status st = mem::write_data<float>(local_pawn + off.custom_time_dilation, target_time_scale);
            if (st != Status::Ok) {
                return st;
            }
        }
        const auto move_comp = mem::read<uintptr_t>(local_pawn + off.character_movement);
        if (!move_comp.has_value() || !validate_live_uobject(*move_comp, guobjectarray_abs, off, nullptr)) {
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
        if (target_jump_z > 0.0f && target_jump_z <= 5000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.jump_z_velocity, target_jump_z);
            if (st != Status::Ok) {
                return st;
            }
        }
        return Status::Ok;
    }

    [[nodiscard]] static bool is_priority_gameplay_actor(std::string_view raw_name) noexcept {
        auto icontains = [](std::string_view hay, std::string_view needle) noexcept -> bool {
            if (needle.size() > hay.size()) {
                return false;
            }
            auto to_lower = [](unsigned char c) constexpr noexcept -> unsigned char {
                return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
            };
            for (size_t i = 0; i <= hay.size() - needle.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < needle.size(); ++j) {
                    if (to_lower(static_cast<unsigned char>(hay[i + j])) !=
                        to_lower(static_cast<unsigned char>(needle[j]))) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return true;
                }
            }
            return false;
        };

        // Immediately ignore handheld items, charges, add-ons, and consumables that deplete mid-game
        if (icontains(raw_name, "Collectable") ||
            icontains(raw_name, "Medkit")      ||
            icontains(raw_name, "Flashlight")  ||
            icontains(raw_name, "Toolbox")     ||
            icontains(raw_name, "Key")         ||
            icontains(raw_name, "MapItem")     ||
            icontains(raw_name, "Firecracker") ||
            icontains(raw_name, "Addon")       ||
            icontains(raw_name, "Item")        ||
            icontains(raw_name, "Chargeable")  ||
            icontains(raw_name, "Inventory")) {
            return false;
        }

        return icontains(raw_name, "Slasher") ||
               icontains(raw_name, "Killer") ||
               icontains(raw_name, "Camper") ||
               icontains(raw_name, "Survivor") ||
               icontains(raw_name, "Generator") ||
               icontains(raw_name, "Hatch") ||
               icontains(raw_name, "Chest") ||
               icontains(raw_name, "Searchable") ||
               icontains(raw_name, "Totem");
    }

    static size_t collect_nearby_entities(
        uintptr_t ulevel_addr,
        uintptr_t guobjectarray_abs,
        uintptr_t ignore_actor,
        const FVector& origin,
        float max_radius_meters,
        const EngineOffsets& off,
        const GNamePoolResolver& name_resolver,
        std::span<EntitySnapshot> out_snapshots
    ) noexcept {
        if (!InProcessMemory::IsValidPtr(ulevel_addr, 0xB0U) ||
            !InProcessMemory::IsValidPtr(ignore_actor, 0x150U) ||
            out_snapshots.empty()) {
            return 0;
        }
        const auto actor_arr = mem::read<TArrayHeader>(ulevel_addr + off.actor_array);
        if (!actor_arr.has_value() ||
            !InProcessMemory::IsValidPtr(actor_arr->data_ptr, sizeof(uintptr_t)) ||
            actor_arr->count < 40 ||
            actor_arr->count > 16384 ||
            actor_arr->max < actor_arr->count) {
            return 0;
        }
        const size_t raw_count     = static_cast<size_t>(actor_arr->count);
        const size_t bounded_count = (raw_count > kMaxActorsPerTick) ? kMaxActorsPerTick : raw_count;
        const float max_dist_cm = max_radius_meters * 100.0f;
        const float max_dist_sq = max_dist_cm * max_dist_cm;
        size_t collected = 0;
        std::array<char, 128> name_buf{};

        // Strictly collect only live, validated priority gameplay actors during an active match
        for (size_t i = 0; i < bounded_count && collected < out_snapshots.size(); ++i) {
            const uintptr_t slot_addr = actor_arr->data_ptr + (i * sizeof(uintptr_t));
            const auto actor_ptr = mem::read<uintptr_t>(slot_addr);
            if (!actor_ptr.has_value() ||
                *actor_ptr == ignore_actor ||
                !InProcessMemory::IsValidPtr(*actor_ptr, 0x150U)) {
                continue;
            }

            uint32_t fname_idx = 0U;
            if (!validate_live_uobject(*actor_ptr, guobjectarray_abs, off, &fname_idx) || fname_idx == 0U) {
                continue;
            }

            if (!name_resolver.resolve_name(fname_idx, name_buf)) {
                continue;
            }
            if (!is_priority_gameplay_actor(name_buf.data())) {
                continue;
            }
            const auto pos = read_actor_location(*actor_ptr, guobjectarray_abs, off);
            if (!pos.has_value()) {
                continue;
            }
            const float dist_sq = origin.dist_squared(*pos);
            if (std::isfinite(dist_sq) && dist_sq <= max_dist_sq) {
                out_snapshots[collected++] = EntitySnapshot{
                    .actor_address   = *actor_ptr,
                    .fname_id        = fname_idx,
                    .world_pos       = *pos,
                    .distance_meters = std::sqrt(dist_sq) * 0.01f
                };
            }
        }
        return collected;
    }
};

// ============================================================================
// 4. LocalPlayerNameController (Bounded In-Place UTF-16 FString Reader & Override)
// ============================================================================
class INSTRUMENT_LOCAL LocalPlayerNameController {
public:
    static bool read_local_name(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::span<char> out_utf8
    ) noexcept {
        if (player_state_addr == 0 || out_utf8.size() < 2U) {
            return false;
        }
        const uintptr_t fstring_addr = player_state_addr + off.player_name_private;
        const auto fstr = mem::read<TArrayHeader>(fstring_addr);
        if (!fstr.has_value() || fstr->data_ptr == 0 || fstr->count <= 1) {
            return false;
        }
        const size_t char_count = static_cast<size_t>(fstr->count) - 1U;
        std::array<char16_t, 64> u16_buf{};
        const size_t clamped = std::min({char_count, u16_buf.size() - 1U, out_utf8.size() - 1U});
        std::span<uint8_t> raw_slice{reinterpret_cast<uint8_t*>(u16_buf.data()), clamped * sizeof(char16_t)};
        if (InProcessMemory::safe_read(fstr->data_ptr, raw_slice) != Status::Ok) {
            return false;
        }
        for (size_t i = 0; i < clamped; ++i) {
            const char16_t ch = u16_buf[i];
            out_utf8[i] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<char>(ch) : '?';
        }
        out_utf8[clamped] = '\0';
        return clamped > 0;
    }

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

    static Status override_local_name_utf8(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::string_view new_name_utf8
    ) noexcept {
        if (player_state_addr == 0 || new_name_utf8.empty()) {
            return Status::InvalidArgument;
        }
        std::array<char16_t, 64> u16_buf{};
        const size_t len = std::min(new_name_utf8.size(), u16_buf.size() - 1U);
        for (size_t i = 0; i < len; ++i) {
            u16_buf[i] = static_cast<char16_t>(static_cast<unsigned char>(new_name_utf8[i]));
        }
        u16_buf[len] = u'\0';
        return override_local_name(player_state_addr, off, std::u16string_view{u16_buf.data(), len});
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
    const uintptr_t gnamepool_abs     = base + off.gnamepool_rva;
    const uintptr_t guobjectarray_abs = base + off.guobjectarray_rva;
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

    // 1. Resolve UWorld pointer safely; if transitioning or invalid, clear HUD match state
    const uintptr_t gworld_abs = base + off.gworld_rva;
    const auto uworld = mem::read<uintptr_t>(gworld_abs);
    if (!uworld.has_value() || !InProcessMemory::IsValidPtr(*uworld, 0x420U)) {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.match_active = false;
        hud_data.node_count   = 0;
        pthread_mutex_unlock(&hud_data.mutex);
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
        if (ulevel.has_value() && InProcessMemory::IsValidPtr(*ulevel, 0xB0U)) {
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

    // 2. Traverse UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> AcknowledgedPawn / PlayerCameraManager
    uintptr_t       local_pawn          = 0;
    uintptr_t       local_player_st     = 0;
    ue4::FVector    local_origin{};
    hud::CameraView cam_view{};
    float           cur_walk_speed      = 600.0f;
    float           cur_max_accel       = 2048.0f;
    float           cur_jump_z          = 420.0f;
    float           cur_time_scale      = 1.0f;

    if (game_instance.has_value() && InProcessMemory::IsValidPtr(*game_instance, 0x50U)) {
        const auto lp_array = mem::read<ue4::TArrayHeader>(*game_instance + off.local_players_array);
        if (lp_array.has_value() &&
            InProcessMemory::IsValidPtr(lp_array->data_ptr, sizeof(uintptr_t)) &&
            lp_array->count > 0 && lp_array->count <= 16)
        {
            const auto first_lp = mem::read<uintptr_t>(lp_array->data_ptr);
            if (first_lp.has_value() && InProcessMemory::IsValidPtr(*first_lp, 0x48U)) {
                const auto pc = mem::read<uintptr_t>(*first_lp + off.player_controller);
                if (pc.has_value() &&
                    ue4::SpatialAndKinematicPipeline::validate_live_uobject(*pc, guobjectarray_abs, off, nullptr))
                {
                    // Read PlayerCameraManager (+0x2D0) -> CameraCachePrivate (+0x1AF0) -> POV (+0x10 / +0x1C / +0x28)
                    if (const auto cam_mgr = mem::read<uintptr_t>(*pc + off.player_camera_manager);
                        cam_mgr.has_value() &&
                        ue4::SpatialAndKinematicPipeline::validate_live_uobject(*cam_mgr, guobjectarray_abs, off, nullptr))
                    {
                        if (const auto pov_loc = mem::read<ue4::FVector>(*cam_mgr + off.pov_location)) {
                            if (std::isfinite(pov_loc->x) && std::isfinite(pov_loc->y) && std::isfinite(pov_loc->z)) {
                                cam_view.location = {pov_loc->x, pov_loc->y, pov_loc->z};
                            }
                        }
                        if (const auto pov_rot = mem::read<hud::Rotator>(*cam_mgr + off.pov_rotation)) {
                            if (std::isfinite(pov_rot->pitch) && std::isfinite(pov_rot->yaw) && std::isfinite(pov_rot->roll)) {
                                cam_view.rotation = *pov_rot;
                            }
                        }
                        if (const auto pov_fov = mem::read<float>(*cam_mgr + off.pov_fov)) {
                            if (std::isfinite(*pov_fov)) {
                                cam_view.fov = *pov_fov;
                            }
                        }
                    }

                    const auto pawn = mem::read<uintptr_t>(*pc + off.acknowledged_pawn);
                    if (pawn.has_value() &&
                        ue4::SpatialAndKinematicPipeline::validate_live_uobject(*pawn, guobjectarray_abs, off, nullptr))
                    {
                        if (const auto loc = ue4::SpatialAndKinematicPipeline::read_actor_location(*pawn, guobjectarray_abs, off)) {
                            local_pawn   = *pawn;
                            local_origin = *loc;
                        }
                        if (local_pawn != 0) {
                            if (const auto td = mem::read<float>(local_pawn + off.custom_time_dilation)) {
                                if (std::isfinite(*td)) {
                                    cur_time_scale = *td;
                                }
                            }
                            if (const auto move_comp = mem::read<uintptr_t>(local_pawn + off.character_movement);
                                move_comp.has_value() &&
                                ue4::SpatialAndKinematicPipeline::validate_live_uobject(*move_comp, guobjectarray_abs, off, nullptr))
                            {
                                if (const auto ws = mem::read<float>(*move_comp + off.max_walk_speed)) {
                                    if (std::isfinite(*ws)) cur_walk_speed = *ws;
                                }
                                if (const auto ma = mem::read<float>(*move_comp + off.max_acceleration)) {
                                    if (std::isfinite(*ma)) cur_max_accel = *ma;
                                }
                                if (const auto jz = mem::read<float>(*move_comp + off.jump_z_velocity)) {
                                    if (std::isfinite(*jz)) cur_jump_z = *jz;
                                }
                            }
                            if (const auto ps = mem::read<uintptr_t>(local_pawn + off.player_state);
                                ps.has_value() &&
                                ue4::SpatialAndKinematicPipeline::validate_live_uobject(*ps, guobjectarray_abs, off, nullptr))
                            {
                                local_player_st = *ps;
                            }
                        }
                    }
                    if (local_player_st == 0) {
                        if (const auto ctrl_ps = mem::read<uintptr_t>(*pc + off.controller_player_state);
                            ctrl_ps.has_value() &&
                            ue4::SpatialAndKinematicPipeline::validate_live_uobject(*ctrl_ps, guobjectarray_abs, off, nullptr))
                        {
                            local_player_st = *ctrl_ps;
                        }
                    }
                }
            }
        }
    }

    std::array<char, 64> current_player_name{};
    if (local_player_st != 0) {
        (void)ue4::LocalPlayerNameController::read_local_name(
            local_player_st,
            off,
            current_player_name
        );
    }

    // 3. Traverse UWorld -> PersistentLevel -> Actors array and convert to RawEntitySample[64]
    std::array<ue4::EntitySnapshot, ue4::SpatialAndKinematicPipeline::kMaxSnapshots> snapshots{};
    std::array<std::array<char, 64>, ue4::SpatialAndKinematicPipeline::kMaxSnapshots> resolved_names{};
    std::array<hud::adapter::RawEntitySample, ue4::SpatialAndKinematicPipeline::kMaxSnapshots> raw_samples{};
    size_t collected_count = 0;

    if (ulevel.has_value() &&
        local_pawn != 0 &&
        ue4::SpatialAndKinematicPipeline::validate_live_uobject(*ulevel, guobjectarray_abs, off, nullptr))
    {
        collected_count = ue4::SpatialAndKinematicPipeline::collect_nearby_entities(
            *ulevel,
            guobjectarray_abs,
            local_pawn,
            local_origin,
            1000.0f,
            off,
            name_pool,
            snapshots
        );

        for (size_t i = 0; i < collected_count; ++i) {
            if (!name_pool.resolve_name(snapshots[i].fname_id, resolved_names[i])) {
                resolved_names[i][0] = '\0';
            }
            raw_samples[i] = hud::adapter::RawEntitySample{
                .x      = snapshots[i].world_pos.x,
                .y      = snapshots[i].world_pos.y,
                .z      = snapshots[i].world_pos.z,
                .dist_m = snapshots[i].distance_meters,
                .name   = resolved_names[i].data()
            };
        }
    }

    // 4. Synchronize camera, entities, player name, and kinematics with Dear ImGui HUD
    bool  should_apply_kinematics  = false;
    float target_walk_speed        = cur_walk_speed;
    float target_accel             = cur_max_accel;
    float target_jump_z            = cur_jump_z;
    float target_time_scale        = cur_time_scale;
    bool  should_apply_player_name = false;
    std::array<char, 64> target_player_name{};

    hud::adapter::sync_telemetry_to_hud(
        cam_view.location.x,
        cam_view.location.y,
        cam_view.location.z,
        cam_view.rotation.pitch,
        cam_view.rotation.yaw,
        cam_view.rotation.roll,
        cam_view.fov,
        raw_samples.data(),
        collected_count,
        cur_walk_speed,
        cur_max_accel,
        cur_jump_z,
        cur_time_scale,
        &should_apply_kinematics,
        &target_walk_speed,
        &target_accel,
        &target_jump_z,
        &target_time_scale,
        current_player_name.data(),
        &should_apply_player_name,
        target_player_name.data(),
        target_player_name.size()
    );

    if (should_apply_player_name && local_player_st != 0 && target_player_name[0] != '\0') {
        if (ue4::SpatialAndKinematicPipeline::validate_live_uobject(local_player_st, guobjectarray_abs, off, nullptr)) {
            (void)ue4::LocalPlayerNameController::override_local_name_utf8(
                local_player_st,
                off,
                target_player_name.data()
            );
        }
    }

    if (should_apply_kinematics && local_pawn != 0) {
        (void)ue4::SpatialAndKinematicPipeline::apply_kinematic_limits(
            local_pawn,
            guobjectarray_abs,
            off,
            target_walk_speed,
            target_accel,
            target_jump_z,
            target_time_scale
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
                const uintptr_t lib_base = g_state.ue4_module.base_address;
                log_resolved_engine_anchors();
                if (egl_bridge::install_egl_bridge(lib_base)) {
                    DIAG_LOGI("EGL PLT/GOT bridge installed on libUE4.so @ 0x%llX",
                              static_cast<unsigned long long>(lib_base));
                } else {
                    DIAG_LOGW("EGL PLT/GOT bridge did not find eglSwapBuffers slot in libUE4.so");
                }
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
