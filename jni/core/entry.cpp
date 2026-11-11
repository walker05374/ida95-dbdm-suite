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
#include "ipc_shared_state.hpp"
#include "scene_collector_helpers.hpp"
#include "telemetry_adapter.hpp"
#include "diagnostic_tick_bridge.hpp"
#include <dirent.h>
#include <jni.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
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
    uintptr_t controller_pawn           = 0x0278;     // AController::Pawn
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
    uintptr_t skeletal_mesh             = 0x0288;     // ACharacter::Mesh (USkeletalMeshComponent*, +0x288)
    uintptr_t character_movement        = 0x0290;     // ACharacter::CharacterMovement (UCharacterMovementComponent*, +0x290)

    // APlayerCameraManager -> CameraCachePrivate (+0x1AE0 / +0x1AF0) -> POV (+0x10 inside FCameraCacheEntry)
    uintptr_t camera_cache_private      = 0x1AF0;     // FCameraCacheEntry (FClassParams @ 0x0F3B0910)
    uintptr_t camera_cache_alt          = 0x1AE0;     // Alternate FCameraCacheEntry base
    uintptr_t pov_location              = 0x1AF0 + 0x0010; // FMinimalViewInfo::Location (+0x1B00, FVector)
    uintptr_t pov_rotation              = 0x1AF0 + 0x001C; // FMinimalViewInfo::Rotation (+0x1B0C, FRotator)
    uintptr_t pov_fov                   = 0x1AF0 + 0x0028; // FMinimalViewInfo::FOV (+0x1B18, float)

    // USceneComponent
    uintptr_t relative_location         = 0x0134;     // FVector (12 bytes)
    uintptr_t relative_rotation         = 0x0140;     // FRotator (12 bytes: Pitch, Yaw, Roll)
    uintptr_t component_velocity        = 0x014C;     // FVector (12 bytes)
    uintptr_t scene_comp_flags          = 0x0164;     // uint16 bitfield (bVisible=0x0040, bHiddenInGame=0x1000)

    // FUObjectArray / FChunkedFixedUObjectArray (for live lifecycle / PendingKill verification)
    uintptr_t guobject_objects_offset   = 0x0010;     // ObjObjects.Objects (FUObjectItem**)
    uintptr_t guobject_max_elems_offset = 0x0020;     // ObjObjects.MaxElements (int32)
    uintptr_t guobject_num_elems_offset = 0x0024;     // ObjObjects.NumElements (int32)
    uintptr_t guobject_num_chunks_offset= 0x002C;     // ObjObjects.NumChunks (int32)

    // UCharacterMovementComponent
    uintptr_t jump_z_velocity           = 0x0158;     // float (+0x158)
    uintptr_t movement_mode             = 0x0174;     // uint8 EMovementMode (+0x174; MOVE_Walking=1, MOVE_Flying=5)
    uintptr_t movement_dirty_flags      = 0x0178;     // uint8 bitfield (bNetworkUpdateReceived / bJustTeleported / bCheatFlying, +0x178)
    uintptr_t max_walk_speed            = 0x018C;     // float (+0x18C)
    uintptr_t max_walk_speed_crouched   = 0x0190;     // float (+0x190)
    uintptr_t max_fly_speed             = 0x0194;     // float (+0x194)
    uintptr_t max_acceleration          = 0x0198;     // float (+0x198)
    uintptr_t max_custom_movement_speed = 0x019C;     // float (+0x19C)

    // APlayerCameraManager FOV & AActor Collision
    uintptr_t actor_collision_flags     = 0x0058;     // uint8 bitfield (bActorEnableCollision)
    uintptr_t camera_default_fov        = 0x0248;     // float APlayerCameraManager::DefaultFOV (+0x248)
    uintptr_t camera_locked_fov         = 0x0250;     // float APlayerCameraManager::LockedFOV (+0x250)

    // UChargeableComponent / Generator Progress & USkillCheck
    uintptr_t chargeable_current_charge = 0x0338;     // float (+0x338)
    uintptr_t player_interaction_handler= 0x08D0;     // UPlayerInteractionHandler* (+0x8D0)
    uintptr_t skillcheck_component      = 0x02E8;     // USkillCheck* (+0x2E8)
    uintptr_t skillcheck_success_start  = 0x0164;     // float USkillCheck::SuccessZoneStart (+0x164)
    uintptr_t skillcheck_success_length = 0x0168;     // float USkillCheck::SuccessZoneLength (+0x168)
    uintptr_t skillcheck_bonus_length   = 0x016C;     // float USkillCheck::BonusZoneLength (+0x16C)

    // ACamperPlayer / ADBDPlayerState Gameplay State & Role
    uintptr_t camper_immobilize_state   = 0x1158;     // uint8 EImmobilizeState (+0x1158)
    uintptr_t camper_trap_immunity_flag = 0x115C;     // uint8 Trap Immunity / Hazard Bypass (+0x115C)
    uintptr_t player_name_private       = 0x0310;     // FString (TArray<char16_t>)
    uintptr_t player_state_game_role    = 0x039A;     // uint8 EPlayerRole (+0x39A: 1=Camper, 2=Slasher, 3=Observer)
    uintptr_t player_state_camper_state = 0x03B0;     // uint8 EGameState (+0x3B0: 2=VE_Escaped)
};

// ============================================================================
// 2. GNamePool Safe Decoder (UE 4.27 FNamePool Block Architecture - Rev. 7)
// ============================================================================
class INSTRUMENT_LOCAL GNamePoolResolver {
public:
    static constexpr uint32_t  kBlockBitShift            = 16U;
    static constexpr uint32_t  kOffsetMask               = (1U << kBlockBitShift) - 1U;
    static constexpr uint32_t  kMaxBlocks                = 8192U;
    static constexpr size_t    kMaxNameLen               = 128U;
    static constexpr uintptr_t kCurrentBlockOffset       = 0x0038U; // FNameEntryAllocator::CurrentBlock (+0x38)
    static constexpr uintptr_t kByteCursorOffset         = 0x003CU; // FNameEntryAllocator::CurrentByteCursor (+0x3C)
    static constexpr uintptr_t kBlocksOffset             = 0x0040U; // FNameEntryAllocator::Blocks (+0x40)
    static constexpr uint32_t  kDefaultBlockBytes        = 65536U;  // Standard 64 KB FNamePool block (Stride = 2)
    static constexpr uint32_t  kCasePreservingBlockBytes = 262144U; // WITH_CASE_PRESERVING_NAME = 1 (Stride = 4)

    explicit constexpr GNamePoolResolver(uintptr_t pool_base) noexcept
        : pool_base_(pool_base) {}

    [[nodiscard]] bool resolve_name(uint32_t comparison_index, std::span<char> out_buffer) const noexcept {
        if (pool_base_ == 0 || out_buffer.size() < 2U) {
            return false;
        }
        const uint32_t block_idx = comparison_index >> kBlockBitShift;
        if (block_idx >= kMaxBlocks) {
            return false;
        }

        // Detect once from Block[0] (EName(0) == "None") whether the pool uses standard 64 KB blocks
        // (stride=2, header=+0x00, string=+0x02) or case-preserving 256 KB blocks (stride=4, header=+0x04, string=+0x06)
        static uint32_t  s_detected_stride     = 0U;
        static uint32_t  s_detected_block_size = kDefaultBlockBytes;
        static uintptr_t s_detected_hdr_off    = 0x0000U;
        static uintptr_t s_detected_str_off    = 0x0002U;

        if (s_detected_stride == 0U) {
            if (const auto blk0 = mem::read<uintptr_t>(pool_base_ + kBlocksOffset); blk0.has_value() && *blk0 != 0) {
                char probe4[4]{};
                std::span<uint8_t> s4{reinterpret_cast<uint8_t*>(probe4), sizeof(probe4)};
                if (InProcessMemory::safe_read(*blk0 + 0x0006U, s4) == Status::Ok &&
                    std::memcmp(probe4, "None", 4) == 0) {
                    s_detected_stride     = 4U;
                    s_detected_block_size = kCasePreservingBlockBytes;
                    s_detected_hdr_off    = 0x0004U;
                    s_detected_str_off    = 0x0006U;
                } else if (InProcessMemory::safe_read(*blk0 + 0x0002U, s4) == Status::Ok &&
                           std::memcmp(probe4, "None", 4) == 0) {
                    s_detected_stride     = 2U;
                    s_detected_block_size = kDefaultBlockBytes;
                    s_detected_hdr_off    = 0x0000U;
                    s_detected_str_off    = 0x0002U;
                }
            }
        }

        const uint32_t  stride     = (s_detected_stride != 0U) ? s_detected_stride : 2U;
        const uint32_t  block_size = (s_detected_stride != 0U) ? s_detected_block_size : kDefaultBlockBytes;
        const uintptr_t hdr_off    = (s_detected_stride != 0U) ? s_detected_hdr_off : 0x0000U;
        const uintptr_t str_off    = (s_detected_stride != 0U) ? s_detected_str_off : 0x0002U;

        const uint32_t byte_off = (comparison_index & kOffsetMask) * stride;
        if (byte_off + str_off >= block_size) {
            return false;
        }

        const uintptr_t block_ptr_addr =
            pool_base_ + kBlocksOffset + (static_cast<uintptr_t>(block_idx) * sizeof(uintptr_t));
        const auto block_base = mem::read<uintptr_t>(block_ptr_addr);
        if (!block_base.has_value() || *block_base == 0) {
            return false;
        }
        const uintptr_t entry_addr = *block_base + byte_off;
        const auto header = mem::read<uint16_t>(entry_addr + hdr_off);
        if (!header.has_value()) {
            return false;
        }
        const bool   is_wide = (*header & 1U) != 0;
        const size_t len     = static_cast<size_t>(*header >> 1U);
        if (len == 0 || len >= kMaxNameLen || len >= out_buffer.size()) {
            return false;
        }
        const size_t entry_bytes = is_wide ? (len * sizeof(char16_t)) : len;
        if (byte_off + str_off + entry_bytes > block_size) {
            return false;
        }
        if (!is_wide) {
            std::span<uint8_t> str_slice{reinterpret_cast<uint8_t*>(out_buffer.data()), len};
            if (InProcessMemory::safe_read(entry_addr + str_off, str_slice) != Status::Ok) {
                return false;
            }
            out_buffer[len] = '\0';
            return true;
        }

        std::array<char16_t, kMaxNameLen> wide_buf{};
        std::span<uint8_t> wide_slice{reinterpret_cast<uint8_t*>(wide_buf.data()), entry_bytes};
        if (InProcessMemory::safe_read(entry_addr + str_off, wide_slice) != Status::Ok) {
            return false;
        }
        size_t out_idx = 0;
        for (size_t i = 0; i < len && out_idx + 1U < out_buffer.size(); ++i) {
            const uint16_t ch = static_cast<uint16_t>(wide_buf[i]);
            if (ch == 0U) {
                break;
            }
            if (ch < 0x80U) {
                out_buffer[out_idx++] = static_cast<char>(ch);
            } else if (ch < 0x800U && out_idx + 2U < out_buffer.size()) {
                out_buffer[out_idx++] = static_cast<char>(0xC0U | (ch >> 6U));
                out_buffer[out_idx++] = static_cast<char>(0x80U | (ch & 0x3FU));
            } else if (out_idx + 3U < out_buffer.size()) {
                out_buffer[out_idx++] = static_cast<char>(0xE0U | (ch >> 12U));
                out_buffer[out_idx++] = static_cast<char>(0x80U | ((ch >> 6U) & 0x3FU));
                out_buffer[out_idx++] = static_cast<char>(0x80U | (ch & 0x3FU));
            } else {
                break;
            }
        }
        out_buffer[out_idx] = '\0';
        return out_idx > 0;
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
    int32_t   priority_rank{99};
    FVector   world_pos{};
    float     yaw_deg{0.0f};
    float     distance_meters{0.0f};
    float     progress_pct{-1.0f};
    bool      has_skeleton{false};
    hud::Vec3 bone_joints[hud::kSkeletonJointCount]{};
    char      category_label[32]{};
};

class INSTRUMENT_LOCAL SpatialAndKinematicPipeline {
public:
    static constexpr size_t   kMaxActorsPerTick      = 4096U;
    static constexpr size_t   kMaxSnapshots          = 256U;
    static constexpr uint32_t kFlagBeginDestroyed    = 0x00008000U; // RF_BeginDestroyed (1 << 15 in UE4.27)
    static constexpr uint32_t kFlagFinishDestroyed   = 0x00010000U; // RF_FinishDestroyed (1 << 16 in UE4.27)

    struct UObjectHeaderSnapshot {
        uintptr_t vtable{0};
        uint32_t  object_flags{0};
        int32_t   internal_index{-1};
        uintptr_t class_private{0};
        uint32_t  fname_comparison_index{0};
        uint32_t  fname_display_index{0};
    };

    // Validates that a UObject* is live, mapped in canonical user-space (verified via process_vm_readv),
    // has valid VTable and ClassPrivate pointers, and is not RF_BeginDestroyed / RF_FinishDestroyed.
    [[nodiscard]] static bool validate_live_uobject(
        uintptr_t obj_addr,
        uintptr_t /*guobjectarray_abs*/,
        const EngineOffsets& /*off*/,
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
        if (hdr->internal_index < 0 || hdr->internal_index >= 0x400000) {
            return false;
        }

        const uint32_t fname_idx =
            (hdr->fname_display_index != 0U && hdr->fname_display_index < (8192U << 16U))
                ? hdr->fname_display_index
                : hdr->fname_comparison_index;

        if (fname_idx >= (8192U << 16U)) {
            return false;
        }

        if (out_fname_display_idx != nullptr) {
            *out_fname_display_idx = fname_idx;
        }
        return true;
    }

    // Strict canonical 48-bit user-space + struct span + live UObject validation before any offset read/write
    [[nodiscard]] static bool validate_canonical_uobject(
        uintptr_t obj_addr,
        size_t min_struct_bytes,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (obj_addr == 0) {
            return false;
        }
        const uintptr_t canonical = obj_addr & 0x0000FFFFFFFFFFFFULL;
        if (canonical < 0x00010000ULL || canonical > 0x00007FFFFFFFFFFFULL) {
            return false;
        }
        if (!InProcessMemory::IsValidPtr(obj_addr, std::max<size_t>(min_struct_bytes, sizeof(UObjectHeaderSnapshot)))) {
            return false;
        }
        return validate_live_uobject(obj_addr, guobjectarray_abs, off, nullptr);
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
        // If RelativeLocation is (0,0,0) because the component is attached to a parent spawner/tile,
        // probe USceneComponent::ComponentToWorld (FTransform at +0x1C0/+0x1D0/+0x1E0) for world-space Translation.
        if (std::abs(loc->x) < 1.0f && std::abs(loc->y) < 1.0f && std::abs(loc->z) < 1.0f) {
            struct FTransformProbe {
                float qx, qy, qz, qw;
                float tx, ty, tz, tpad;
                float sx, sy, sz, spad;
            };
            static constexpr uintptr_t kCtwOffsets[] = {0x1C0U, 0x1D0U, 0x1E0U, 0x1B0U, 0x1F0U};
            for (uintptr_t ctw_off : kCtwOffsets) {
                if (const auto ctw = mem::read<FTransformProbe>(*root_comp + ctw_off)) {
                    const float qnorm = ctw->qx * ctw->qx + ctw->qy * ctw->qy + ctw->qz * ctw->qz + ctw->qw * ctw->qw;
                    if (std::isfinite(qnorm) && qnorm >= 0.95f && qnorm <= 1.05f &&
                        std::isfinite(ctw->tx) && std::isfinite(ctw->ty) && std::isfinite(ctw->tz) &&
                        std::isfinite(ctw->sx) && std::abs(ctw->sx) >= 0.05f && std::abs(ctw->sx) <= 100.0f &&
                        (std::abs(ctw->tx) >= 10.0f || std::abs(ctw->ty) >= 10.0f || std::abs(ctw->tz) >= 10.0f) &&
                        std::abs(ctw->tx) <= 500000.0f && std::abs(ctw->ty) <= 500000.0f && std::abs(ctw->tz) <= 500000.0f)
                    {
                        return FVector{ctw->tx, ctw->ty, ctw->tz};
                    }
                }
            }
        }
        return loc;
    }

    // Reads character rotation yaw from RootComponent (+0x140 -> RelativeRotation.Yaw at +0x144).
    static void read_character_yaw(
        uintptr_t actor,
        const EngineOffsets& off,
        float& out_yaw_deg
    ) noexcept {
        out_yaw_deg = 0.0f;

        if (!InProcessMemory::IsValidPtr(actor, 0x2A0U)) {
            return;
        }

        if (const auto root_comp = mem::read<uintptr_t>(actor + off.root_component);
            root_comp.has_value() && InProcessMemory::IsValidPtr(*root_comp, 0x170U)) {
            if (const auto yaw_val = mem::read<float>(*root_comp + off.relative_rotation + 0x04U);
                yaw_val.has_value() && std::isfinite(*yaw_val) && std::abs(*yaw_val) <= 3600.0f) {
                out_yaw_deg = *yaw_val;
            }
        }
    }

    // Dynamically resolves UPlayerController -> AcknowledgedPawn -> UCharacterMovementComponent each frame
    [[nodiscard]] static hud::CharacterMovementChain resolve_local_movement_chain(
        uintptr_t local_pc,
        uintptr_t fallback_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        hud::CharacterMovementChain chain{};
        if (validate_canonical_uobject(local_pc, 0x2E0U, guobjectarray_abs, off)) {
            chain.player_controller = local_pc;
            uintptr_t ack_pawn = mem::read<uintptr_t>(local_pc + off.acknowledged_pawn).value_or(0ULL);
            if (!validate_canonical_uobject(ack_pawn, 0x2A0U, guobjectarray_abs, off)) {
                ack_pawn = mem::read<uintptr_t>(local_pc + off.controller_pawn).value_or(0ULL);
            }
            if (validate_canonical_uobject(ack_pawn, 0x2A0U, guobjectarray_abs, off)) {
                chain.acknowledged_pawn = ack_pawn;
            }
        }
        if (chain.acknowledged_pawn == 0 && validate_canonical_uobject(fallback_pawn, 0x2A0U, guobjectarray_abs, off)) {
            chain.acknowledged_pawn = fallback_pawn;
        }
        if (chain.acknowledged_pawn != 0) {
            const auto mc = mem::read<uintptr_t>(chain.acknowledged_pawn + off.character_movement);
            if (mc.has_value() && validate_canonical_uobject(*mc, 0x1B0U, guobjectarray_abs, off)) {
                chain.movement_component = *mc;
                chain.is_valid           = true;
            }
        }
        return chain;
    }

    static Status apply_kinematic_limits(
        uintptr_t local_pc,
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        float target_walk_speed,
        float target_accel,
        float target_jump_z = 0.0f,
        float target_time_scale = 0.0f,
        bool set_dirty_flag = true
    ) noexcept {
        const hud::CharacterMovementChain chain = resolve_local_movement_chain(
            local_pc, local_pawn, guobjectarray_abs, off);
        if (!chain.is_valid || chain.acknowledged_pawn == 0 || chain.movement_component == 0) {
            return Status::UnmappedOrFault;
        }

        const uintptr_t pawn_addr = chain.acknowledged_pawn;
        const uintptr_t move_comp = chain.movement_component;

        if (std::isfinite(target_time_scale) && target_time_scale >= 0.5f && target_time_scale <= 2.5f) {
            (void)mem::write_data<float>(pawn_addr + off.custom_time_dilation, target_time_scale);
        }
        if (std::isfinite(target_walk_speed) && target_walk_speed >= 50.0f && target_walk_speed <= 5000.0f) {
            const Status st = mem::write_data<float>(move_comp + off.max_walk_speed, target_walk_speed);
            if (st != Status::Ok) {
                return st;
            }
            (void)mem::write_data<float>(move_comp + off.max_walk_speed_crouched, target_walk_speed);
            (void)mem::write_data<float>(move_comp + off.max_fly_speed, target_walk_speed);
            (void)mem::write_data<float>(move_comp + off.max_custom_movement_speed, target_walk_speed);
        }
        if (std::isfinite(target_accel) && target_accel >= 50.0f && target_accel <= 6144.0f) {
            const Status st = mem::write_data<float>(move_comp + off.max_acceleration, target_accel);
            if (st != Status::Ok) {
                return st;
            }
        }
        if (std::isfinite(target_jump_z) && target_jump_z >= 50.0f && target_jump_z <= 5000.0f) {
            const Status st = mem::write_data<float>(move_comp + off.jump_z_velocity, target_jump_z);
            if (st != Status::Ok) {
                return st;
            }
        }

        // Set continuous movement dirty / network-reconciliation flag on UCharacterMovementComponent
        // so server reconciliation does not revert local movement tweaks
        if (set_dirty_flag) {
            if (const auto cur_flags = mem::read<uint8_t>(move_comp + off.movement_dirty_flags)) {
                const uint8_t dirty_byte = hud::compute_movement_dirty_flags(*cur_flags, true, false);
                (void)mem::write_data<uint8_t>(move_comp + off.movement_dirty_flags, dirty_byte);
            }
        }

        // Read-back verification of written floats
        static float s_last_logged_walk  = -1.0f;
        static float s_last_logged_accel = -1.0f;
        static float s_last_logged_jump  = -1.0f;
        static float s_last_logged_time  = -1.0f;
        static uint32_t s_kin_log_tick   = 0;

        const float rb_walk  = mem::read<float>(move_comp + off.max_walk_speed).value_or(-1.0f);
        const float rb_accel = mem::read<float>(move_comp + off.max_acceleration).value_or(-1.0f);
        const float rb_jump  = mem::read<float>(move_comp + off.jump_z_velocity).value_or(-1.0f);
        const float rb_time  = mem::read<float>(pawn_addr + off.custom_time_dilation).value_or(-1.0f);

        const bool changed =
            (std::abs(rb_walk - s_last_logged_walk) > 0.5f) ||
            (std::abs(rb_accel - s_last_logged_accel) > 0.5f) ||
            (std::abs(rb_jump - s_last_logged_jump) > 0.5f) ||
            (std::abs(rb_time - s_last_logged_time) > 0.02f);

        if (changed || (++s_kin_log_tick % 120U) == 1U) {
            s_last_logged_walk  = rb_walk;
            s_last_logged_accel = rb_accel;
            s_last_logged_jump  = rb_jump;
            s_last_logged_time  = rb_time;
            DIAG_LOGI(
                "[KINEMATICS] Applied & verified on PC=0x%llX Pawn=0x%llX MoveComp=0x%llX | WalkSpeed=%.1f (readback=%.1f) | MaxAccel=%.1f (readback=%.1f) | JumpZ=%.1f (readback=%.1f) | TimeScale=%.2f (readback=%.2f)",
                static_cast<unsigned long long>(chain.player_controller),
                static_cast<unsigned long long>(pawn_addr),
                static_cast<unsigned long long>(move_comp),
                static_cast<double>(target_walk_speed),
                static_cast<double>(rb_walk),
                static_cast<double>(target_accel),
                static_cast<double>(rb_accel),
                static_cast<double>(target_jump_z),
                static_cast<double>(rb_jump),
                static_cast<double>(target_time_scale),
                static_cast<double>(rb_time)
            );
        }
        return Status::Ok;
    }

    // 2. Field of View (FOV) Changer: Force-writes clamped FOV [80.0f, 120.0f] to DefaultFOV (+0x248), LockedFOV (+0x250), and CameraCachePrivate.POV.FOV
    static Status apply_fov_override(
        uintptr_t cam_mgr,
        uintptr_t matched_pov_off,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        float target_fov
    ) noexcept {
        if (!validate_canonical_uobject(cam_mgr, 0x1B30U, guobjectarray_abs, off) ||
            !std::isfinite(target_fov) ||
            target_fov < 80.0f ||
            target_fov > 120.0f) {
            return Status::InvalidArgument;
        }
        const float clamped_fov = std::clamp(target_fov, 80.0f, 120.0f);
        (void)mem::write_data<float>(cam_mgr + off.camera_default_fov, clamped_fov);
        (void)mem::write_data<float>(cam_mgr + off.camera_locked_fov,  clamped_fov);
        const uintptr_t pov_fov_off = (matched_pov_off >= 0x1AF0U && matched_pov_off <= 0x1B00U)
            ? (matched_pov_off + 0x18U)
            : off.pov_fov;
        return mem::write_data<float>(cam_mgr + pov_fov_off, clamped_fov);
    }

    // 3a. Auto Skillcheck: Resolves UPlayerInteractionHandler -> USkillCheck and locks SuccessZoneStart to 0.0f and SuccessZoneLength to 1.0f
    static bool apply_auto_skillcheck(
        uintptr_t local_pawn,
        uintptr_t local_pc,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        float zone_start  = 0.0f;
        float zone_length = 1.0f;
        float bonus_len   = 1.0f;
        hud::compute_skillcheck_zone_override(zone_start, zone_length, bonus_len);

        const hud::CharacterMovementChain chain = resolve_local_movement_chain(
            local_pc, local_pawn, guobjectarray_abs, off);
        const std::array<uintptr_t, 3> owners{chain.acknowledged_pawn, local_pawn, local_pc};
        static constexpr std::array<uintptr_t, 5> kInteractionHandlerOffsets = {
            0x08D0U, 0x08C8U, 0x08D8U, 0x08E0U, 0x0910U
        };
        static constexpr std::array<uintptr_t, 4> kSkillCheckOffsets = {
            0x02E8U, 0x02E0U, 0x02F0U, 0x02F8U
        };

        for (uintptr_t owner : owners) {
            if (!validate_canonical_uobject(owner, 0x920U, guobjectarray_abs, off)) {
                continue;
            }
            for (uintptr_t ih_off : kInteractionHandlerOffsets) {
                const auto ih = mem::read<uintptr_t>(owner + ih_off);
                if (!ih.has_value() || !validate_canonical_uobject(*ih, 0x310U, guobjectarray_abs, off)) {
                    continue;
                }
                for (uintptr_t sc_off : kSkillCheckOffsets) {
                    const auto sc = mem::read<uintptr_t>(*ih + sc_off);
                    if (!sc.has_value() || !validate_canonical_uobject(*sc, 0x180U, guobjectarray_abs, off)) {
                        continue;
                    }
                    const float prev_start = mem::read<float>(*sc + off.skillcheck_success_start).value_or(zone_start);
                    const float prev_len   = mem::read<float>(*sc + off.skillcheck_success_length).value_or(zone_length);
                    const bool  newly_overridden =
                        (std::abs(prev_start - zone_start) > 1e-3f) ||
                        (std::abs(prev_len - zone_length) > 1e-3f);
                    (void)mem::write_data<float>(*sc + off.skillcheck_success_start,  zone_start);
                    (void)mem::write_data<float>(*sc + off.skillcheck_success_length, zone_length);
                    (void)mem::write_data<float>(*sc + off.skillcheck_bonus_length,   bonus_len);
                    return newly_overridden;
                }
            }
        }
        return false;
    }

    // 3b. Trap Immunity: Clears Trapped immobilize state on local pawn and disables collision overlap on nearby BearTrap actors
    static void apply_trap_immunity(
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!validate_canonical_uobject(local_pawn, 0x1170U, guobjectarray_abs, off)) {
            return;
        }
        if (const auto imm = mem::read<uint8_t>(local_pawn + off.camper_immobilize_state)) {
            if (*imm == static_cast<uint8_t>(hud::ImmobilizeState::Trapped)) {
                (void)mem::write_data<uint8_t>(
                    local_pawn + off.camper_immobilize_state,
                    static_cast<uint8_t>(hud::ImmobilizeState::None));
            }
        }
        if (const auto cur_flag = mem::read<uint8_t>(local_pawn + off.camper_trap_immunity_flag)) {
            const uint8_t updated = static_cast<uint8_t>(*cur_flag | 0x01U);
            if (updated != *cur_flag) {
                (void)mem::write_data<uint8_t>(local_pawn + off.camper_trap_immunity_flag, updated);
            }
        }
    }

    static void disable_trap_actor_collision(
        uintptr_t trap_actor,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!validate_canonical_uobject(trap_actor, 0x0070U, guobjectarray_abs, off)) {
            return;
        }
        if (const auto flags = mem::read<uint8_t>(trap_actor + off.actor_collision_flags)) {
            const uint8_t cleared = hud::compute_actor_collision_byte(*flags, false);
            if (cleared != *flags) {
                (void)mem::write_data<uint8_t>(trap_actor + off.actor_collision_flags, cleared);
            }
        }
    }

    // 4a. Fly Cheat: Resolves UPlayerController -> AcknowledgedPawn -> UCharacterMovementComponent
    //     and overrides MovementMode (+0x174) to MOVE_Flying (5) or restores MOVE_Walking (1)
    static Status apply_fly_mode(
        uintptr_t local_pc,
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        bool fly_enabled,
        float fly_speed
    ) noexcept {
        const hud::CharacterMovementChain chain = resolve_local_movement_chain(
            local_pc, local_pawn, guobjectarray_abs, off);
        if (!chain.is_valid || chain.acknowledged_pawn == 0 || chain.movement_component == 0) {
            return Status::UnmappedOrFault;
        }
        const uintptr_t pawn_addr = chain.acknowledged_pawn;
        const uintptr_t move_comp = chain.movement_component;

        const uint8_t target_mode = hud::resolve_movement_mode_byte(fly_enabled);
        if (fly_enabled && std::isfinite(fly_speed) && fly_speed >= 50.0f && fly_speed <= 5000.0f) {
            (void)mem::write_data<float>(move_comp + off.max_fly_speed, fly_speed);
        }
        if (const auto cur_flags = mem::read<uint8_t>(move_comp + off.movement_dirty_flags)) {
            const uint8_t dirty_byte = hud::compute_movement_dirty_flags(*cur_flags, fly_enabled, fly_enabled);
            (void)mem::write_data<uint8_t>(move_comp + off.movement_dirty_flags, dirty_byte);
        }
        if (const auto col_flags = mem::read<uint8_t>(pawn_addr + off.actor_collision_flags)) {
            const uint8_t col_byte = hud::compute_actor_collision_byte(*col_flags, !fly_enabled);
            if (col_byte != *col_flags) {
                (void)mem::write_data<uint8_t>(pawn_addr + off.actor_collision_flags, col_byte);
            }
        }
        return mem::write_data<uint8_t>(move_comp + off.movement_mode, target_mode);
    }

    // 4b. Force Escape: Sets VE_Escaped state on ADBDPlayerState and teleports local pawn to exit gate / hatch bounds
    static Status apply_force_escape(
        uintptr_t local_pawn,
        uintptr_t local_player_st,
        const FVector* escape_portal_pos,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (validate_canonical_uobject(local_player_st, 0x3C0U, guobjectarray_abs, off)) {
            constexpr uint8_t kGameStateEscaped = 2U; // EGameState::VE_Escaped
            (void)mem::write_data<uint8_t>(local_player_st + off.player_state_camper_state, kGameStateEscaped);
        }
        if (!validate_canonical_uobject(local_pawn, 0x1170U, guobjectarray_abs, off)) {
            return Status::InvalidArgument;
        }
        (void)mem::write_data<uint8_t>(
            local_pawn + off.camper_immobilize_state,
            static_cast<uint8_t>(hud::ImmobilizeState::None));

        if (escape_portal_pos != nullptr &&
            std::isfinite(escape_portal_pos->x) &&
            std::isfinite(escape_portal_pos->y) &&
            std::isfinite(escape_portal_pos->z)) {
            const auto root_comp = mem::read<uintptr_t>(local_pawn + off.root_component);
            if (root_comp.has_value() && validate_canonical_uobject(*root_comp, 0x150U, guobjectarray_abs, off)) {
                const FVector dest{
                    escape_portal_pos->x,
                    escape_portal_pos->y,
                    escape_portal_pos->z + 95.0f
                };
                return mem::write_data<FVector>(*root_comp + off.relative_location, dest);
            }
        }
        return Status::Ok;
    }

    // 5a. Set Immobilize State (none, hooked, trapped, hiding, dead, caged)
    static Status apply_immobilize_state(
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        hud::ImmobilizeState state
    ) noexcept {
        if (!validate_canonical_uobject(local_pawn, 0x1170U, guobjectarray_abs, off)) {
            return Status::InvalidArgument;
        }
        if (const auto cur_state = mem::read<uint8_t>(local_pawn + off.camper_immobilize_state)) {
            if (!hud::is_immobilize_transition_safe(*cur_state, state)) {
                return Status::InvalidArgument;
            }
        }
        const uint8_t state_byte = hud::resolve_immobilize_state_byte(state);
        return mem::write_data<uint8_t>(local_pawn + off.camper_immobilize_state, state_byte);
    }

    // 5b. Set Player Role (camper, slasher, observer)
    static Status apply_player_role(
        uintptr_t local_player_st,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        hud::PlayerRole role
    ) noexcept {
        if (!validate_canonical_uobject(local_player_st, 0x3C0U, guobjectarray_abs, off)) {
            return Status::InvalidArgument;
        }
        const uint8_t role_byte = hud::resolve_player_role_byte(role);
        return mem::write_data<uint8_t>(local_player_st + off.player_state_game_role, role_byte);
    }

    // Walks UObject::ClassPrivate (+0x10) and UStruct::SuperStruct (+0x48) up to 6 levels,
    // writing the leaf UClass name into out_leaf_class and the full inheritance chain
    // (e.g. "BP_Chest01_C->Searchable->Interactable->Actor") into out_super_chain.
    // Uses a 128-slot direct-mapped UClass* cache so each UClass hierarchy is walked via
    // process_vm_readv only once rather than on every actor every tick.
    struct UClassHierarchyCacheEntry {
        uintptr_t             cls_ptr{0};
        std::array<char, 96>  leaf_name{};
        std::array<char, 224> super_chain{};
    };

    static void resolve_uclass_hierarchy(
        uintptr_t actor_ptr,
        const GNamePoolResolver& name_resolver,
        std::span<char> out_leaf_class,
        std::span<char> out_super_chain
    ) noexcept {
        if (!out_leaf_class.empty()) {
            out_leaf_class[0] = '\0';
        }
        if (!out_super_chain.empty()) {
            out_super_chain[0] = '\0';
        }
        const auto cls_ptr_opt = mem::read<uintptr_t>(actor_ptr + 0x10U);
        if (!cls_ptr_opt.has_value() || !InProcessMemory::IsValidPtr(*cls_ptr_opt, 0x50U)) {
            return;
        }

        const uintptr_t leaf_cls = *cls_ptr_opt;
        static std::array<UClassHierarchyCacheEntry, 128> s_cache{};
        const size_t slot = static_cast<size_t>((leaf_cls >> 4U) ^ (leaf_cls >> 13U)) & (s_cache.size() - 1U);
        UClassHierarchyCacheEntry& cache_entry = s_cache[slot];

        if (cache_entry.cls_ptr == leaf_cls && cache_entry.leaf_name[0] != '\0') {
            if (!out_leaf_class.empty()) {
                std::strncpy(out_leaf_class.data(), cache_entry.leaf_name.data(), out_leaf_class.size() - 1U);
                out_leaf_class[out_leaf_class.size() - 1U] = '\0';
            }
            if (!out_super_chain.empty()) {
                std::strncpy(out_super_chain.data(), cache_entry.super_chain.data(), out_super_chain.size() - 1U);
                out_super_chain[out_super_chain.size() - 1U] = '\0';
            }
            return;
        }

        uintptr_t cur_cls = leaf_cls;
        size_t chain_pos = 0;
        std::array<char, 96> node_name{};
        std::array<char, 96> built_leaf{};
        std::array<char, 224> built_chain{};

        for (size_t depth = 0; depth < 6U && cur_cls != 0; ++depth) {
            if (!InProcessMemory::IsValidPtr(cur_cls, 0x50U)) {
                break;
            }
            const auto cls_fn = mem::read<uint32_t>(cur_cls + 0x18U);
            if (!cls_fn.has_value() || *cls_fn == 0U || !name_resolver.resolve_name(*cls_fn, node_name)) {
                break;
            }
            if (depth == 0U) {
                std::strncpy(built_leaf.data(), node_name.data(), built_leaf.size() - 1U);
                built_leaf[built_leaf.size() - 1U] = '\0';
            }
            if (chain_pos + 4U < built_chain.size()) {
                if (chain_pos > 0U) {
                    built_chain[chain_pos++] = '-';
                    built_chain[chain_pos++] = '>';
                }
                const size_t nlen = std::strlen(node_name.data());
                const size_t avail = built_chain.size() - 1U - chain_pos;
                const size_t copy_n = std::min(nlen, avail);
                std::memcpy(built_chain.data() + chain_pos, node_name.data(), copy_n);
                chain_pos += copy_n;
                built_chain[chain_pos] = '\0';
            }
            if (std::strcmp(node_name.data(), "Actor") == 0 ||
                std::strcmp(node_name.data(), "Object") == 0) {
                break;
            }
            const auto super_opt = mem::read<uintptr_t>(cur_cls + 0x48U);
            if (!super_opt.has_value() || *super_opt == cur_cls) {
                break;
            }
            cur_cls = *super_opt;
        }

        if (built_leaf[0] != '\0') {
            cache_entry.cls_ptr     = leaf_cls;
            cache_entry.leaf_name   = built_leaf;
            cache_entry.super_chain = built_chain;
        }

        if (!out_leaf_class.empty()) {
            std::strncpy(out_leaf_class.data(), built_leaf.data(), out_leaf_class.size() - 1U);
            out_leaf_class[out_leaf_class.size() - 1U] = '\0';
        }
        if (!out_super_chain.empty()) {
            std::strncpy(out_super_chain.data(), built_chain.data(), out_super_chain.size() - 1U);
            out_super_chain[out_super_chain.size() - 1U] = '\0';
        }
    }

    // Classifies an actor using a strict 3-tier resolver:
    //   Tier 1: Strict UE4 UClass / UStruct::SuperStruct hierarchy type check.
    //   Tier 2: Strict actor-ID / blueprint name check on Actor->NamePrivate.
    //   Tier 3: No cross-category fallback; logs unresolved interactive actors to the debug log and returns nullptr.
    // Returns canonical category ("Killer", "Survivor", "Generator", "Hatch", "ExitGate", "Totem", "Chest", "Hook", "Pallet")
    // and sets *out_priority (1 = highest priority .. 9 = lowest), or returns nullptr if not a valid target match entity.
    [[nodiscard]] static const char* classify_actor_category(
        std::string_view actor_name,
        std::string_view class_name,
        std::string_view super_chain = {},
        int32_t* out_priority = nullptr,
        bool* out_is_unresolved_candidate = nullptr
    ) noexcept {
        if (out_is_unresolved_candidate != nullptr) {
            *out_is_unresolved_candidate = false;
        }

        auto icontains = [](std::string_view hay, std::string_view needle) noexcept -> bool {
            if (needle.empty() || needle.size() > hay.size()) {
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

        const std::string_view type_chain = super_chain.empty() ? class_name : super_chain;

        auto leaf_or_actor_contains = [&](std::string_view needle) noexcept -> bool {
            return icontains(class_name, needle) || icontains(actor_name, needle);
        };

        auto type_contains = [&](std::string_view needle) noexcept -> bool {
            return icontains(type_chain, needle) || icontains(class_name, needle);
        };

        // 1. Strict exclusion of manager actors, ghost generators, sub-entities, templates, windows, hiding closets, and handheld items
        if (leaf_or_actor_contains("Default__")        ||
            leaf_or_actor_contains("Archetype")        ||
            leaf_or_actor_contains("GEN_VARIABLE")     ||
            leaf_or_actor_contains("LevelGenerator")   ||
            leaf_or_actor_contains("ProceduralLevel")  ||
            leaf_or_actor_contains("GeneratorEntity")  ||
            leaf_or_actor_contains("GeneratorDriven")  ||
            leaf_or_actor_contains("Portal_Generator") ||
            leaf_or_actor_contains("Portal_MeatHook")  ||
            leaf_or_actor_contains("BigThreeEvent")    ||
            leaf_or_actor_contains("BP_Entity_C")      ||
            leaf_or_actor_contains("SacrificeEnd")     ||
            leaf_or_actor_contains("PlayerStart")      ||
            leaf_or_actor_contains("GameRoleFront")    ||
            leaf_or_actor_contains("SlasherMale_REF")  ||
            leaf_or_actor_contains("BP_Menu_")         ||
            leaf_or_actor_contains("Lobby")            ||
            leaf_or_actor_contains("Looby")            ||
            leaf_or_actor_contains("pos_")             ||
            leaf_or_actor_contains("AIController")     ||
            leaf_or_actor_contains("PlayerController") ||
            leaf_or_actor_contains("PlayerState")      ||
            leaf_or_actor_contains("GameState")        ||
            leaf_or_actor_contains("GameMode")         ||
            leaf_or_actor_contains("Camera")           ||
            leaf_or_actor_contains("KillCam")          ||
            leaf_or_actor_contains("ReactionCam")      ||
            leaf_or_actor_contains("StruggleCam")      ||
            leaf_or_actor_contains("SacrificeCam")     ||
            leaf_or_actor_contains("Status")           ||
            leaf_or_actor_contains("Effect")           ||
            leaf_or_actor_contains("Decal")            ||
            leaf_or_actor_contains("Heartbeat")        ||
            leaf_or_actor_contains("Lighting")         ||
            leaf_or_actor_contains("PostProcess")      ||
            leaf_or_actor_contains("Emitter")          ||
            leaf_or_actor_contains("Particle")         ||
            leaf_or_actor_contains("Audio")            ||
            leaf_or_actor_contains("Sound")            ||
            leaf_or_actor_contains("NavMesh")          ||
            leaf_or_actor_contains("NavData")          ||
            leaf_or_actor_contains("Volume")           ||
            leaf_or_actor_contains("ReflectionCapture")||
            leaf_or_actor_contains("NightmareHusk")    ||
            leaf_or_actor_contains("DreamInducer")     ||
            leaf_or_actor_contains("BlastMine")        ||
            leaf_or_actor_contains("Collectable")      ||
            leaf_or_actor_contains("Medkit")           ||
            leaf_or_actor_contains("Flashlight")       ||
            leaf_or_actor_contains("Toolbox")          ||
            leaf_or_actor_contains("Firecracker")      ||
            leaf_or_actor_contains("DullKey")          ||
            leaf_or_actor_contains("SkeletonKey")      ||
            leaf_or_actor_contains("BrokenKey")        ||
            leaf_or_actor_contains("MapItem")          ||
            leaf_or_actor_contains("Addon")            ||
            leaf_or_actor_contains("BP_Item_")         ||
            leaf_or_actor_contains("_ACC")             ||
            leaf_or_actor_contains("Torso")            ||
            leaf_or_actor_contains("Hair")             ||
            leaf_or_actor_contains("Necklace")         ||
            leaf_or_actor_contains("Bags")             ||
            leaf_or_actor_contains("SkeletalMeshActor")||
            leaf_or_actor_contains("StaticMeshActor")  ||
            leaf_or_actor_contains("ClipManager")      ||
            leaf_or_actor_contains("Outline")          ||
            leaf_or_actor_contains("Blocker")          ||
            leaf_or_actor_contains("WindowStandard")      ||
            leaf_or_actor_contains("BP_Window")           ||
            leaf_or_actor_contains("Closet")              ||
            leaf_or_actor_contains("BP_Locker")           ||
            leaf_or_actor_contains("Breakable")           ||
            leaf_or_actor_contains("LevelCrow")           ||
            leaf_or_actor_contains("PlayerInteractable")  ||
            leaf_or_actor_contains("CamperInteractable")  ||
            leaf_or_actor_contains("SlasherInteractable") ||
            leaf_or_actor_contains("BP_IND_ExitGate")) {
            return nullptr;
        }

        if (icontains(class_name, "Window") || icontains(actor_name, "Window")) {
            return nullptr;
        }

        // Reject if the leaf UClass itself is an unspawned Spawner (except HexSpawner/Totem) or Component
        if (icontains(class_name, "Component") ||
            (icontains(class_name, "Spawner") &&
             !leaf_or_actor_contains("Totem") &&
             !leaf_or_actor_contains("HexSpawner") &&
             !leaf_or_actor_contains("Shrine"))) {
            return nullptr;
        }

        auto set_prio = [&](int32_t p, const char* lbl) noexcept -> const char* {
            if (out_priority) *out_priority = p;
            return lbl;
        };

        const bool is_sub_weapon_or_trap =
            leaf_or_actor_contains("Trap")       ||
            leaf_or_actor_contains("Projectile") ||
            leaf_or_actor_contains("Power")      ||
            leaf_or_actor_contains("Stalker")    ||
            leaf_or_actor_contains("Weapon")     ||
            leaf_or_actor_contains("Mori");

        // ====================================================================
        // TIER 1: Strict UE4 UClass / UStruct::SuperStruct Type Check
        // ====================================================================
        if (!type_chain.empty()) {
            // Priority 1: Killer (ASlasherPlayer)
            if (!is_sub_weapon_or_trap &&
                (type_contains("SlasherPlayer") ||
                 type_contains("Slasher_Character") ||
                 type_contains("Killer_Character"))) {
                return set_prio(1, "Killer");
            }

            // Priority 2: Survivor (ACamperPlayer)
            if (type_contains("CamperPlayer") ||
                type_contains("Camper_Character") ||
                type_contains("Survivor_Character")) {
                return set_prio(2, "Survivor");
            }

            // Priority 3: Generator (AGenerator)
            if (type_contains("->Generator->")     ||
                type_contains("GeneratorStandard") ||
                type_contains("GeneratorNoPole")   ||
                type_contains("GeneratorShort")    ||
                type_contains("GeneratorIndoor")   ||
                type_contains("BP_Generator")      ||
                icontains(class_name, "Generator")) {
                return set_prio(3, "Generator");
            }

            // Priority 4: Hatch (AHatch)
            if (type_contains("->Hatch->") ||
                type_contains("BP_Hatch")  ||
                icontains(class_name, "Hatch")) {
                return set_prio(4, "Hatch");
            }

            // Priority 5: Exit Gate (AEscapeDoor)
            if (type_contains("->EscapeDoor->") ||
                type_contains("BP_EscapeBase")  ||
                icontains(class_name, "EscapeDoor")) {
                return set_prio(5, "ExitGate");
            }

            // Priority 6: Totem (ATotem)
            if (type_contains("->Totem->")  ||
                type_contains("TotemBase")  ||
                type_contains("BP_Totem")   ||
                type_contains("HexSpawner") ||
                type_contains("HexTotem")   ||
                type_contains("BoonTotem")  ||
                icontains(class_name, "Totem")) {
                if (leaf_or_actor_contains("Cleansed") ||
                    leaf_or_actor_contains("Destroyed") ||
                    leaf_or_actor_contains("Broken")) {
                    return nullptr;
                }
                return set_prio(6, "Totem");
            }

            // Priority 7: Chest (ASearchable / BP_Chest)
            if (type_contains("->Searchable->") ||
                type_contains("BP_Chest")       ||
                icontains(class_name, "Chest")  ||
                icontains(class_name, "Searchable")) {
                return set_prio(7, "Chest");
            }

            // Priority 8: Hook (AMeatHook / AMeatLocker / ASmallMeatLocker)
            if (type_contains("->MeatHook->")    ||
                type_contains("SmallMeatLocker") ||
                type_contains("MeatLocker")      ||
                type_contains("MeatHook")        ||
                type_contains("BP_Hook")) {
                return set_prio(8, "Hook");
            }

            // Priority 9: Pallet (APallet / Bookshelf)
            if (type_contains("->Pallet->") ||
                type_contains("BP_Pallet")  ||
                type_contains("Bookshelf")  ||
                icontains(class_name, "Pallet")) {
                return set_prio(9, "Pallet");
            }

            // When UClass hierarchy (type_chain) is resolved, do NOT fall back to actor_name
            // substring guessing (which could misclassify e.g. SearchableSpawner MagicFountains as Chests).
            if (out_is_unresolved_candidate != nullptr) {
                if (type_contains("Interactable") || type_contains("DBDPlayer")) {
                    *out_is_unresolved_candidate = true;
                }
            }
            return nullptr;
        }

        // ====================================================================
        // TIER 2: Strict Actor-ID / Instance Name Match (No cross-category fallback)
        // ====================================================================
        if (!is_sub_weapon_or_trap &&
            (icontains(actor_name, "Slasher_Character") ||
             icontains(actor_name, "BP_Slasher")        ||
             icontains(actor_name, "SlasherPlayer")     ||
             icontains(actor_name, "Killer_Character"))) {
            return set_prio(1, "Killer");
        }

        if (icontains(actor_name, "BP_Camper")        ||
            icontains(actor_name, "CamperMale")       ||
            icontains(actor_name, "CamperFemale")     ||
            icontains(actor_name, "Camper_Character") ||
            icontains(actor_name, "Survivor_Character")) {
            return set_prio(2, "Survivor");
        }

        if (icontains(actor_name, "GeneratorStandard") ||
            icontains(actor_name, "GeneratorNoPole")   ||
            icontains(actor_name, "GeneratorShort")    ||
            icontains(actor_name, "GeneratorIndoor")   ||
            icontains(actor_name, "BP_Generator")) {
            return set_prio(3, "Generator");
        }

        if (icontains(actor_name, "BP_Hatch") ||
            icontains(actor_name, "Hatch01")  ||
            icontains(actor_name, "Hatch_")) {
            return set_prio(4, "Hatch");
        }

        if (icontains(actor_name, "EscapeDoor") ||
            icontains(actor_name, "ExitGate")) {
            return set_prio(5, "ExitGate");
        }

        if (icontains(actor_name, "BP_Totem")   ||
            icontains(actor_name, "TotemBase")  ||
            icontains(actor_name, "HexSpawner") ||
            icontains(actor_name, "HexTotem")   ||
            icontains(actor_name, "BoonTotem")) {
            if (icontains(actor_name, "Cleansed") ||
                icontains(actor_name, "Destroyed") ||
                icontains(actor_name, "Broken")) {
                return nullptr;
            }
            return set_prio(6, "Totem");
        }

        if (icontains(actor_name, "BP_Chest") ||
            icontains(actor_name, "Chest01")  ||
            icontains(actor_name, "Chest_")   ||
            icontains(actor_name, "Searchable")) {
            return set_prio(7, "Chest");
        }

        if (icontains(actor_name, "SmallMeatLocker") ||
            icontains(actor_name, "MeatLocker")      ||
            icontains(actor_name, "MeatHook")        ||
            icontains(actor_name, "BP_Hook")) {
            return set_prio(8, "Hook");
        }

        if (icontains(actor_name, "Bookshelf") ||
            icontains(actor_name, "BP_Pallet") ||
            icontains(actor_name, "Pallet01")  ||
            icontains(actor_name, "Pallet_")) {
            return set_prio(9, "Pallet");
        }

        // ====================================================================
        // TIER 3: Unresolved Fallback — Never guess or collapse into another category
        // ====================================================================
        if (out_is_unresolved_candidate != nullptr) {
            if (type_contains("Interactable") || type_contains("DBDPlayer")) {
                *out_is_unresolved_candidate = true;
            }
        }

        return nullptr;
    }

    // Reads active generator repair progress float (0.0f to 100.0f) directly from the
    // interaction/component offset (+0x0338) or its attached UChargeableComponent instance.
    [[nodiscard]] static float read_generator_progress_pct(
        uintptr_t actor_ptr,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!InProcessMemory::IsValidPtr(actor_ptr, off.chargeable_current_charge + sizeof(uintptr_t))) {
            return 0.0f;
        }

        // 1. Check if the interaction/chargeable offset on AGenerator holds a live 8-byte aligned UChargeableComponent* pointer
        if (const auto maybe_comp = mem::read<uintptr_t>(actor_ptr + off.chargeable_current_charge);
            maybe_comp.has_value())
        {
            const uintptr_t untagged_comp = *maybe_comp & 0x00FFFFFFFFFFFFFFULL;
            if ((untagged_comp & 0x7ULL) == 0ULL &&
                untagged_comp >= 0x0000000100000000ULL &&
                untagged_comp <= 0x00007FFFFFFFFFFFULL &&
                validate_canonical_uobject(*maybe_comp, off.chargeable_current_charge + sizeof(float) * 2U, guobjectarray_abs, off))
            {
                if (const auto comp_chg = mem::read<float>(*maybe_comp + off.chargeable_current_charge);
                    comp_chg.has_value() && std::isfinite(*comp_chg) && *comp_chg >= 0.0f && *comp_chg <= 100.0f)
                {
                    float max_chg = 0.0f;
                    if (const auto m = mem::read<float>(*maybe_comp + off.chargeable_current_charge + sizeof(float));
                        m.has_value() && std::isfinite(*m) && *m >= 80.0f && *m <= 100.0f) {
                        max_chg = *m;
                    }
                    return hud::adapter::normalize_generator_progress_pct(*comp_chg, max_chg);
                }
            }
        }

        // 2. Read the active repair progress float (0.0f to 100.0f) directly from actor_ptr + off.chargeable_current_charge (+0x0338)
        if (const auto raw_chg = mem::read<float>(actor_ptr + off.chargeable_current_charge);
            raw_chg.has_value() && std::isfinite(*raw_chg) && *raw_chg >= 0.0f && *raw_chg <= 100.0f)
        {
            float max_chg = 0.0f;
            if (const auto m = mem::read<float>(actor_ptr + off.chargeable_current_charge + sizeof(float));
                m.has_value() && std::isfinite(*m) && *m >= 80.0f && *m <= 100.0f) {
                max_chg = *m;
            }
            return hud::adapter::normalize_generator_progress_pct(*raw_chg, max_chg);
        }

        return 0.0f;
    }

    // Reads Shrine (Totem) state & interaction flags. Returns >= 0.0f while the totem is active/standing
    // so it is preserved in the telemetry array, and returns -1.0f (< 0.0f) once cleansed/hidden.
    [[nodiscard]] static float read_totem_state_progress(
        uintptr_t actor_ptr,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        const char* actor_name,
        const char* class_name
    ) noexcept {
        if (!InProcessMemory::IsValidPtr(actor_ptr, 0x150U)) {
            return -1.0f;
        }
        if (!hud::adapter::is_totem_active(0.0f, actor_name) ||
            !hud::adapter::is_totem_active(0.0f, class_name)) {
            return -1.0f;
        }

        // Verify RootComponent visibility flags: if bHiddenInGame (0x1000) is set and bVisible (0x0040) is cleared,
        // the totem has been cleansed/deactivated.
        if (const auto root_comp = mem::read<uintptr_t>(actor_ptr + off.root_component);
            root_comp.has_value() && InProcessMemory::IsValidPtr(*root_comp, off.scene_comp_flags + sizeof(uint16_t))) {
            if (!validate_live_uobject(*root_comp, guobjectarray_abs, off, nullptr)) {
                return -1.0f;
            }
            if (const auto vis_flags = mem::read<uint16_t>(*root_comp + off.scene_comp_flags);
                vis_flags.has_value() && ((*vis_flags & 0x1000U) != 0U) && ((*vis_flags & 0x0040U) == 0U)) {
                return -1.0f;
            }
        }

        // Check interaction/cleanse progress at chargeable offset (+0x0338) if mapped
        if (InProcessMemory::IsValidPtr(actor_ptr, off.chargeable_current_charge + sizeof(uintptr_t))) {
            const auto raw_u64 = mem::read<uintptr_t>(actor_ptr + off.chargeable_current_charge);
            const bool is_ptr  = raw_u64.has_value() &&
                validate_canonical_uobject(*raw_u64, sizeof(UObjectHeaderSnapshot), guobjectarray_abs, off);
            if (!is_ptr) {
                if (const auto raw_chg = mem::read<float>(actor_ptr + off.chargeable_current_charge);
                    raw_chg.has_value() && std::isfinite(*raw_chg)) {
                    if (*raw_chg < 0.0f) {
                        return -1.0f;
                    }
                    if (*raw_chg <= 100.0f) {
                        const float pct = hud::adapter::normalize_generator_progress_pct(*raw_chg);
                        if (pct >= hud::adapter::kGeneratorCompleteThresholdPct) {
                            return -1.0f;
                        }
                        return pct;
                    }
                }
            }
        }

        return 0.0f;
    }

    static size_t collect_nearby_entities_multi_level(
        std::span<const uintptr_t> ulevels,
        uintptr_t guobjectarray_abs,
        uintptr_t ignore_actor,
        const FVector& origin,
        float max_radius_meters,
        const EngineOffsets& off,
        const GNamePoolResolver& name_resolver,
        std::span<EntitySnapshot> out_snapshots
    ) noexcept {
        if (ulevels.empty() || out_snapshots.empty()) {
            return 0;
        }
        const float max_dist_cm = max_radius_meters * 100.0f;
        const float max_dist_sq = max_dist_cm * max_dist_cm;
        size_t collected = 0;
        std::array<char, 128> name_buf{};
        std::array<char, 128> class_buf{};
        std::array<char, 256> super_chain_buf{};
        static std::array<uint32_t, 64> s_logged_unresolved_fnames{};
        static size_t s_logged_unresolved_count = 0;

        for (uintptr_t ulevel_addr : ulevels) {
            if (!InProcessMemory::IsValidPtr(ulevel_addr, 0xB0U)) {
                continue;
            }
            const auto actor_arr = mem::read<TArrayHeader>(ulevel_addr + off.actor_array);
            if (!actor_arr.has_value() ||
                !InProcessMemory::IsValidPtr(actor_arr->data_ptr, sizeof(uintptr_t)) ||
                actor_arr->count <= 0 ||
                actor_arr->count > 16384 ||
                actor_arr->max < actor_arr->count) {
                continue;
            }
            const size_t raw_count     = static_cast<size_t>(actor_arr->count);
            const size_t bounded_count = (raw_count > kMaxActorsPerTick) ? kMaxActorsPerTick : raw_count;

            for (size_t i = 0; i < bounded_count; ++i) {
                const uintptr_t slot_addr = actor_arr->data_ptr + (i * sizeof(uintptr_t));
                const auto actor_ptr = mem::read<uintptr_t>(slot_addr);
                if (!actor_ptr.has_value() ||
                    (*actor_ptr == ignore_actor && ignore_actor != 0) ||
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

                resolve_uclass_hierarchy(*actor_ptr, name_resolver, class_buf, super_chain_buf);

                int32_t prio = 99;
                bool is_unresolved = false;
                const char* cat_label = classify_actor_category(
                    name_buf.data(),
                    class_buf.data(),
                    super_chain_buf.data(),
                    &prio,
                    &is_unresolved);
                if (cat_label == nullptr) {
                    if (is_unresolved && class_buf[0] != '\0') {
                        bool already_logged = false;
                        for (size_t u = 0; u < s_logged_unresolved_count; ++u) {
                            if (s_logged_unresolved_fnames[u] == fname_idx) {
                                already_logged = true;
                                break;
                            }
                        }
                        if (!already_logged && s_logged_unresolved_count < s_logged_unresolved_fnames.size()) {
                            s_logged_unresolved_fnames[s_logged_unresolved_count++] = fname_idx;
                            DIAG_LOGW(
                                "[CATEGORY-UNRESOLVED] Actor=0x%llX FName=\"%s\" Class=\"%s\" SuperChain=\"%s\"",
                                static_cast<unsigned long long>(*actor_ptr),
                                name_buf.data(),
                                class_buf.data(),
                                super_chain_buf.data()
                            );
                        }
                    }
                    continue;
                }

                static std::array<uint32_t, 64> s_logged_matched_fnames{};
                static size_t s_logged_matched_count = 0;
                {
                    bool match_logged = false;
                    for (size_t m = 0; m < s_logged_matched_count; ++m) {
                        if (s_logged_matched_fnames[m] == fname_idx) {
                            match_logged = true;
                            break;
                        }
                    }
                    if (!match_logged && s_logged_matched_count < s_logged_matched_fnames.size()) {
                        s_logged_matched_fnames[s_logged_matched_count++] = fname_idx;
                        DIAG_LOGI(
                            "[CATEGORY-MATCH] Cat=%s (prio=%d) | FName=\"%s\" | Class=\"%s\" | SuperChain=\"%s\"",
                            cat_label,
                            static_cast<int>(prio),
                            name_buf.data(),
                            class_buf.data(),
                            super_chain_buf.data()
                        );
                    }
                }

                const auto pos = read_actor_location(*actor_ptr, guobjectarray_abs, off);
                if (!pos.has_value() || !hud::adapter::is_valid_world_coordinate(pos->x, pos->y, pos->z)) {
                    continue;
                }

                const float dist_sq = origin.dist_squared(*pos);
                if (!std::isfinite(dist_sq) || dist_sq > max_dist_sq) {
                    continue;
                }
                const float dist_m = std::sqrt(dist_sq) * 0.01f;

                // Never draw a Killer/Survivor marker on the local player themselves (< 1.5m from local pawn/camera)
                if ((prio == 1 || prio == 2) && dist_m < 1.5f) {
                    continue;
                }

                float gen_pct = -1.0f;
                if (prio == 3) {
                    gen_pct = read_generator_progress_pct(*actor_ptr, guobjectarray_abs, off);
                    // Automatic filter/hide condition when generator repair progress reaches or exceeds 99.5%
                    if (hud::adapter::should_hide_generator(gen_pct)) {
                        continue;
                    }
                } else if (prio == 6) {
                    // Preserve Shrine (Totem) nodes in the telemetry array with progress_pct >= 0.0f
                    // until interaction flags confirm cleansing (progress_pct < 0.0f)
                    gen_pct = read_totem_state_progress(
                        *actor_ptr,
                        guobjectarray_abs,
                        off,
                        name_buf.data(),
                        class_buf.data());
                    if (!hud::adapter::is_totem_active(gen_pct, cat_label)) {
                        continue;
                    }
                } else if (prio == 9) {
                    gen_pct = 0.0f;
                }

                EntitySnapshot snap{};
                snap.actor_address   = *actor_ptr;
                snap.fname_id        = fname_idx;
                snap.priority_rank   = prio;
                snap.world_pos       = *pos;
                snap.distance_meters = dist_m;
                snap.progress_pct    = gen_pct;
                snap.has_skeleton    = false;
                if (prio == 1 || prio == 2) {
                    read_character_yaw(*actor_ptr, off, snap.yaw_deg);
                }
                std::strncpy(snap.category_label, cat_label, sizeof(snap.category_label) - 1);
                snap.category_label[sizeof(snap.category_label) - 1] = '\0';

                if (collected < out_snapshots.size()) {
                    out_snapshots[collected++] = snap;
                } else {
                    // Replace lowest-priority / farthest entry if this actor has higher priority
                    size_t worst_idx = 0;
                    for (size_t k = 1; k < out_snapshots.size(); ++k) {
                        if (out_snapshots[k].priority_rank > out_snapshots[worst_idx].priority_rank ||
                            (out_snapshots[k].priority_rank == out_snapshots[worst_idx].priority_rank &&
                             out_snapshots[k].distance_meters > out_snapshots[worst_idx].distance_meters)) {
                            worst_idx = k;
                        }
                    }
                    if (snap.priority_rank < out_snapshots[worst_idx].priority_rank ||
                        (snap.priority_rank == out_snapshots[worst_idx].priority_rank &&
                         snap.distance_meters < out_snapshots[worst_idx].distance_meters)) {
                        out_snapshots[worst_idx] = snap;
                    }
                }
            }
        }

        // Sort collected entities by priority_rank ascending, then distance_meters ascending
        std::sort(
            out_snapshots.begin(),
            out_snapshots.begin() + static_cast<std::ptrdiff_t>(collected),
            [](const EntitySnapshot& a, const EntitySnapshot& b) noexcept {
                if (a.priority_rank != b.priority_rank) {
                    return a.priority_rank < b.priority_rank;
                }
                return a.distance_meters < b.distance_meters;
            }
        );

        return collected;
    }
};

// ============================================================================
// 4. LocalPlayerNameController (Bounded In-Place UTF-16 FString / FText Reader & Override)
// ============================================================================
class INSTRUMENT_LOCAL LocalPlayerNameController {
public:
    static constexpr std::array<uintptr_t, 3> kCandidateNameOffsets = {
        0x0310U, // UE4.27 NetEase ADBDPlayerState::PlayerNamePrivate (primary)
        0x0308U, // Alternate UE4.27 APlayerState::PlayerNamePrivate layout
        0x0300U  // Base UE4.27 APlayerState::PlayerNamePrivate layout
    };

    [[nodiscard]] static bool is_valid_utf16_fstring(
        const TArrayHeader& fstr,
        std::span<char16_t> out_u16,
        size_t* out_char_len = nullptr
    ) noexcept {
        if (fstr.data_ptr == 0 || fstr.count <= 1 || fstr.count > 64 ||
            fstr.max < fstr.count || fstr.max > 256 || out_u16.size() < 2U) {
            return false;
        }
        const size_t char_count = static_cast<size_t>(fstr.count) - 1U;
        const size_t clamped    = std::min(char_count, out_u16.size() - 1U);
        std::span<uint8_t> raw_slice{
            reinterpret_cast<uint8_t*>(out_u16.data()),
            (clamped + 1U) * sizeof(char16_t)
        };
        if (InProcessMemory::safe_read(fstr.data_ptr, raw_slice) != Status::Ok) {
            return false;
        }
        for (size_t i = 0; i < clamped; ++i) {
            const char16_t ch = out_u16[i];
            if (ch < 0x0020U || (ch >= 0xD800U && ch <= 0xDFFFU) || ch == 0xFFFFU) {
                return false;
            }
        }
        out_u16[clamped] = u'\0';
        if (out_char_len != nullptr) {
            *out_char_len = clamped;
        }
        return clamped > 0;
    }

    static bool read_local_name(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::span<char> out_utf8,
        uintptr_t* out_matched_offset = nullptr
    ) noexcept {
        if (player_state_addr == 0 || out_utf8.size() < 2U) {
            return false;
        }

        const std::array<uintptr_t, 3> offsets = {
            off.player_name_private,
            0x0308U,
            0x0300U
        };

        std::array<char16_t, 64> u16_buf{};
        for (uintptr_t cand_off : offsets) {
            const auto fstr = mem::read<TArrayHeader>(player_state_addr + cand_off);
            if (!fstr.has_value()) {
                continue;
            }
            size_t char_len = 0;
            if (!is_valid_utf16_fstring(*fstr, u16_buf, &char_len)) {
                continue;
            }
            const size_t clamped = std::min(char_len, out_utf8.size() - 1U);
            for (size_t i = 0; i < clamped; ++i) {
                const char16_t ch = u16_buf[i];
                out_utf8[i] = (ch >= 0x20U && ch <= 0x7EU) ? static_cast<char>(ch) : '?';
            }
            out_utf8[clamped] = '\0';
            if (out_matched_offset != nullptr) {
                *out_matched_offset = cand_off;
            }
            return clamped > 0;
        }
        return false;
    }

    // Writes a UTF-16 string to an existing UE4 FString (TArray<char16_t>) without overflowing ArrayMax.
    // If new_name fits in ArrayMax (len + 1 <= ArrayMax), writes in-place and updates ArrayNum = len + 1.
    // If ArrayMax is smaller and running in-process, allocates a persistent char16_t buffer;
    // when running out-of-process via process_vm_writev, clamps safely to ArrayMax - 1.
    static Status write_fstring_u16_safe(
        uintptr_t fstring_addr,
        const TArrayHeader& fstr,
        std::u16string_view new_name
    ) noexcept {
        if (fstring_addr == 0 || new_name.empty() || fstr.data_ptr == 0 || fstr.max <= 1) {
            return Status::InvalidArgument;
        }

        uint8_t probe = 0;
        if (InProcessMemory::safe_read(fstr.data_ptr, std::span<uint8_t>{&probe, 1}) != Status::Ok) {
            return Status::UnmappedOrFault;
        }

        std::array<char16_t, 64> staging{};
        const size_t max_staging_chars = staging.size() - 1U;
        const size_t requested_len     = std::min(new_name.size(), max_staging_chars);
        const size_t avail_in_place    = static_cast<size_t>(fstr.max) - 1U;

        if (requested_len <= avail_in_place) {
            std::memcpy(staging.data(), new_name.data(), requested_len * sizeof(char16_t));
            staging[requested_len] = u'\0';
            const size_t byte_len = (requested_len + 1U) * sizeof(char16_t);
            std::span<const uint8_t> payload{reinterpret_cast<const uint8_t*>(staging.data()), byte_len};
            const Status write_st = InProcessMemory::safe_write(fstr.data_ptr, payload);
            if (write_st != Status::Ok) {
                return write_st;
            }
            const int32_t new_count = static_cast<int32_t>(requested_len + 1U);
            return mem::write_data<int32_t>(fstring_addr + offsetof(TArrayHeader, count), new_count);
        }

        // If ArrayMax is smaller than requested_len + 1 and we are executing in-process inside libUE4.so's address space,
        // use a persistent aligned char16_t buffer so the full string is stored without overflowing ArrayMax.
        if (InProcessMemory::effective_pid() == ::getpid()) {
            alignas(16) static char16_t s_persistent_name_buf[64]{};
            std::memcpy(s_persistent_name_buf, new_name.data(), requested_len * sizeof(char16_t));
            s_persistent_name_buf[requested_len] = u'\0';

            TArrayHeader updated_hdr{};
            updated_hdr.data_ptr = reinterpret_cast<uintptr_t>(s_persistent_name_buf);
            updated_hdr.count    = static_cast<int32_t>(requested_len + 1U);
            updated_hdr.max      = static_cast<int32_t>(std::size(s_persistent_name_buf));
            return mem::write_data<TArrayHeader>(fstring_addr, updated_hdr);
        }

        // Out-of-process fallback: strictly clamp to ArrayMax - 1 in-place
        const size_t clamped_len = std::min(requested_len, avail_in_place);
        if (clamped_len == 0) {
            return Status::UnmappedOrFault;
        }
        std::memcpy(staging.data(), new_name.data(), clamped_len * sizeof(char16_t));
        staging[clamped_len] = u'\0';
        const size_t byte_len = (clamped_len + 1U) * sizeof(char16_t);
        std::span<const uint8_t> payload{reinterpret_cast<const uint8_t*>(staging.data()), byte_len};
        const Status write_st = InProcessMemory::safe_write(fstr.data_ptr, payload);
        if (write_st != Status::Ok) {
            return write_st;
        }
        const int32_t new_count = static_cast<int32_t>(clamped_len + 1U);
        return mem::write_data<int32_t>(fstring_addr + offsetof(TArrayHeader, count), new_count);
    }

    static Status override_local_name(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::u16string_view new_name
    ) noexcept {
        if (player_state_addr == 0 || new_name.empty()) {
            return Status::InvalidArgument;
        }

        // Track previous/original name on this APlayerState so we can also locate and update
        // cached UI FString / FText buffers (e.g. OldNamePrivate, NetEase platform name, FText history)
        static uintptr_t                s_tracked_ps   = 0;
        static std::array<char16_t, 64> s_orig_u16{};
        static size_t                   s_orig_u16_len = 0;

        std::array<char16_t, 64> prev_u16{};
        size_t                   prev_u16_len = 0;

        const std::array<uintptr_t, 3> primary_offsets = {
            off.player_name_private,
            0x0308U,
            0x0300U
        };

        Status primary_status = Status::UnmappedOrFault;
        uintptr_t matched_primary_off = off.player_name_private;

        for (uintptr_t cand_off : primary_offsets) {
            const uintptr_t fstring_addr = player_state_addr + cand_off;
            const auto fstr = mem::read<TArrayHeader>(fstring_addr);
            if (!fstr.has_value()) {
                continue;
            }
            std::array<char16_t, 64> cur_u16{};
            size_t cur_len = 0;
            if (!is_valid_utf16_fstring(*fstr, cur_u16, &cur_len)) {
                continue;
            }
            if (prev_u16_len == 0) {
                prev_u16     = cur_u16;
                prev_u16_len = cur_len;
                if (s_tracked_ps != player_state_addr || s_orig_u16_len == 0) {
                    s_tracked_ps   = player_state_addr;
                    s_orig_u16     = cur_u16;
                    s_orig_u16_len = cur_len;
                }
            }
            const Status st = write_fstring_u16_safe(fstring_addr, *fstr, new_name);
            if (st == Status::Ok && primary_status != Status::Ok) {
                primary_status      = Status::Ok;
                matched_primary_off = cand_off;
            }
        }

        if (primary_status != Status::Ok) {
            return primary_status;
        }

        const std::u16string_view prev_view{prev_u16.data(), prev_u16_len};
        const std::u16string_view orig_view{s_orig_u16.data(), s_orig_u16_len};

        auto matches_cached_name = [&](std::u16string_view cand) noexcept -> bool {
            if (cand.empty()) {
                return false;
            }
            if (!prev_view.empty() && cand == prev_view) {
                return true;
            }
            if (!orig_view.empty() && cand == orig_view) {
                return true;
            }
            return false;
        };

        // Scan APlayerState (+0x0300 .. +0x0480) for secondary FString fields (e.g. OldNamePrivate at +0x328,
        // NetEase account/platform nickname FStrings) and cached UI FText instances (ITextData -> FTextHistory)
        for (uintptr_t scan_off = 0x0300U; scan_off <= 0x0480U; scan_off += sizeof(uintptr_t)) {
            if (scan_off == matched_primary_off) {
                continue;
            }
            const uintptr_t field_addr = player_state_addr + scan_off;
            if (const auto cand_fstr = mem::read<TArrayHeader>(field_addr)) {
                std::array<char16_t, 64> buf{};
                size_t len = 0;
                if (is_valid_utf16_fstring(*cand_fstr, BufferSpan(buf), &len)) {
                    const std::u16string_view view{buf.data(), len};
                    if (matches_cached_name(view) || scan_off == (matched_primary_off + 0x18U)) {
                        (void)write_fstring_u16_safe(field_addr, *cand_fstr, new_name);
                        continue;
                    }
                }
            }

            // Probe UE4 FText layout: { ITextData* Object (+0x00), FSharedReferencer (+0x08), uint32 Flags (+0x10) }
            const auto text_data_ptr = mem::read<uintptr_t>(field_addr);
            if (!text_data_ptr.has_value() || !InProcessMemory::IsValidPtr(*text_data_ptr, 0x40U)) {
                continue;
            }
            for (uintptr_t td_off = 0x08U; td_off <= 0x30U; td_off += sizeof(uintptr_t)) {
                const uintptr_t td_fstr_addr = *text_data_ptr + td_off;
                if (const auto td_fstr = mem::read<TArrayHeader>(td_fstr_addr)) {
                    std::array<char16_t, 64> buf{};
                    size_t len = 0;
                    if (is_valid_utf16_fstring(*td_fstr, BufferSpan(buf), &len)) {
                        const std::u16string_view view{buf.data(), len};
                        if (matches_cached_name(view)) {
                            (void)write_fstring_u16_safe(td_fstr_addr, *td_fstr, new_name);
                            // Bump FText Flags/Revision (+0x10) so Slate/UMG invalidates cached glyph runs
                            if (const auto flags = mem::read<uint32_t>(field_addr + 0x10U)) {
                                (void)mem::write_data<uint32_t>(field_addr + 0x10U, *flags | 0x1U);
                            }
                            break;
                        }
                    }
                }
            }
        }

        return Status::Ok;
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

private:
    template <size_t N>
    [[nodiscard]] static constexpr std::span<char16_t> BufferSpan(std::array<char16_t, N>& arr) noexcept {
        return std::span<char16_t>{arr.data(), arr.size()};
    }
};
} // namespace ue4

// ============================================================================
// 5. Runtime Context, Offset Validation Logging & Telemetry Worker Thread
// ============================================================================
struct INSTRUMENT_LOCAL InstrumentationState {
    std::atomic<bool>  running{false};
    std::atomic<bool>  thread_active{false};
    std::atomic<bool>  ipc_thread_active{false};
    std::atomic<int>   ipc_listen_fd{-1};
    pthread_t          worker_thread{};
    pthread_t          ipc_thread{};
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

// Synchronizes optional external Floating Mod Menu APK config (/data/local/tmp/dbdm_mod_config.cfg)
// into hud::SceneTelemetryData whenever the file modification timestamp or content changes.
INSTRUMENT_LOCAL static void poll_external_mod_menu_config() noexcept {
    static std::uint32_t s_tick_counter      = 0;
    static std::int64_t  s_last_cfg_mtime_ns = 0;

    if ((++s_tick_counter & 0x03U) != 0U) {
        return; // Poll every 4 ticks (~60ms)
    }

    static constexpr const char* kCfgPath = "/data/local/tmp/dbdm_mod_config.cfg";
    struct stat st{};
    if (::stat(kCfgPath, &st) != 0) {
        return;
    }

    const std::int64_t mtime_ns =
        static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000000000LL +
        static_cast<std::int64_t>(st.st_mtim.tv_nsec);
    if (mtime_ns == s_last_cfg_mtime_ns) {
        return;
    }
    s_last_cfg_mtime_ns = mtime_ns;

    std::FILE* fp = std::fopen(kCfgPath, "r");
    if (!fp) {
        return;
    }

    hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
    pthread_mutex_lock(&hud_data.mutex);
    hud::GameplayManipulationConfig& gp_cfg = hud::get_gameplay_config();

    char line[192]{};
    while (std::fgets(line, sizeof(line), fp)) {
        std::size_t len = std::strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        char* eq = std::strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if (std::strcmp(key, "hud_open") == 0) {
            hud_data.hud_open = (val[0] == '1');
        } else if (std::strcmp(key, "draw_markers") == 0) {
            hud_data.draw_2d_node_markers = (val[0] == '1');
        } else if (std::strcmp(key, "max_dist") == 0) {
            float v = 0.0f;
            if (std::sscanf(val, "%f", &v) == 1 && v >= 1.0f && v <= 1000.0f) {
                hud_data.max_render_distance_m = v;
            }
        } else if (std::strcmp(key, "label_filter") == 0 || std::strcmp(key, "gp_payload") == 0) {
            if (!hud::parse_gameplay_ipc_string(val, hud_data, gp_cfg)) {
                std::strncpy(hud_data.label_filter, val, hud::kMaxLabelLen - 1);
                hud_data.label_filter[hud::kMaxLabelLen - 1] = '\0';
            }
        } else if (std::strcmp(key, "override_name") == 0) {
            hud_data.apply_custom_player_name = (val[0] == '1');
        } else if (std::strcmp(key, "player_name") == 0) {
            std::strncpy(hud_data.custom_player_name, val, hud::kMaxLabelLen - 1);
            hud_data.custom_player_name[hud::kMaxLabelLen - 1] = '\0';
        } else if (std::strcmp(key, "apply_kinematics") == 0) {
            hud_data.apply_custom_kinematics = (val[0] == '1');
        } else if (std::strcmp(key, "walk_speed") == 0 || std::strcmp(key, "walk_speed_pct") == 0) {
            float v = 0.0f;
            if (std::sscanf(val, "%f", &v) == 1 && std::isfinite(v)) {
                hud_data.walk_speed_pct = std::clamp(v, 100.0f, 250.0f);
            }
        } else if (std::strcmp(key, "max_accel") == 0 || std::strcmp(key, "accel_pct") == 0) {
            float v = 0.0f;
            if (std::sscanf(val, "%f", &v) == 1 && std::isfinite(v)) {
                hud_data.accel_pct = std::clamp(v, 100.0f, 300.0f);
            }
        } else if (std::strcmp(key, "jump_vel") == 0 || std::strcmp(key, "jump_pct") == 0) {
            float v = 0.0f;
            if (std::sscanf(val, "%f", &v) == 1 && std::isfinite(v)) {
                hud_data.jump_pct = std::clamp(v, 100.0f, 250.0f);
            }
        } else if (std::strcmp(key, "time_scale") == 0 || std::strcmp(key, "time_scale_pct") == 0) {
            float v = 0.0f;
            if (std::sscanf(val, "%f", &v) == 1 && std::isfinite(v)) {
                hud_data.time_scale     = std::clamp(v, 0.5f, 2.0f);
                hud_data.time_scale_pct = hud_data.time_scale * 100.0f;
            }
        } else if (std::strcmp(key, "apply_fov") == 0) {
            gp_cfg.apply_fov_override = (val[0] == '1');
        } else if (std::strcmp(key, "fov") == 0) {
            float v = 90.0f;
            if (std::sscanf(val, "%f", &v) == 1 && std::isfinite(v)) {
                gp_cfg.target_fov = std::clamp(v, 80.0f, 120.0f);
            }
        } else if (std::strcmp(key, "auto_skillcheck") == 0) {
            gp_cfg.auto_skillcheck = (val[0] == '1');
        } else if (std::strcmp(key, "trap_immunity") == 0) {
            gp_cfg.trap_immunity = (val[0] == '1');
        } else if (std::strcmp(key, "fly_cheat") == 0) {
            gp_cfg.fly_cheat_enabled = (val[0] == '1');
        } else if (std::strcmp(key, "force_escape") == 0) {
            gp_cfg.force_escape = (val[0] == '1');
        } else if (std::strcmp(key, "set_immobilize") == 0) {
            if (val[0] == '\0' || std::strcmp(val, "off") == 0) {
                gp_cfg.apply_immobilize_state  = false;
                gp_cfg.target_immobilize_state = hud::ImmobilizeState::None;
            } else {
                gp_cfg.target_immobilize_state = hud::adapter::parse_immobilize_state(val);
                gp_cfg.apply_immobilize_state  = true;
            }
        } else if (std::strcmp(key, "set_player_role") == 0) {
            if (val[0] == '\0' || std::strcmp(val, "none") == 0 || std::strcmp(val, "off") == 0) {
                gp_cfg.apply_player_role  = false;
                gp_cfg.target_player_role = hud::PlayerRole::None;
            } else {
                gp_cfg.target_player_role = hud::adapter::parse_player_role(val);
                gp_cfg.apply_player_role  = (gp_cfg.target_player_role != hud::PlayerRole::None);
            }
        } else if (std::strcmp(key, "cat_mask") == 0) {
            unsigned int cmask = 0xFFU;
            if (std::sscanf(val, "%x", &cmask) == 1) {
                hud_data.show_antagonist    = (cmask & 0x01U) != 0U;
                hud_data.show_teammates     = (cmask & 0x02U) != 0U;
                hud_data.show_power_nodes   = (cmask & 0x04U) != 0U;
                hud_data.show_shrines       = (cmask & 0x08U) != 0U;
                hud_data.show_containers    = (cmask & 0x10U) != 0U;
                hud_data.show_barriers      = (cmask & 0x20U) != 0U;
                hud_data.show_portals       = (cmask & 0x40U) != 0U;
                hud_data.show_special_props = (cmask & 0x80U) != 0U;
            }
        }
    }

    DIAG_LOGI(
        "[IPC-CMD] Config synced from APK -> draw_markers=%d | max_dist=%.1f | filter=\"%s\" | override_name=%d (\"%s\") | apply_kinematics=%d (walk=%.1f%%, accel=%.1f%%, jump=%.1f%%, time=%.2f)",
        hud_data.draw_2d_node_markers ? 1 : 0,
        static_cast<double>(hud_data.max_render_distance_m),
        hud_data.label_filter,
        hud_data.apply_custom_player_name ? 1 : 0,
        hud_data.custom_player_name,
        hud_data.apply_custom_kinematics ? 1 : 0,
        static_cast<double>(hud_data.walk_speed_pct),
        static_cast<double>(hud_data.accel_pct),
        static_cast<double>(hud_data.jump_pct),
        static_cast<double>(hud_data.time_scale)
    );

    pthread_mutex_unlock(&hud_data.mutex);
    std::fclose(fp);
}

// Helper to probe CameraCachePrivate POV across candidate offsets (+0x1B00, +0x1AF0 + 0x10, +0x1AE0 + 0x10)
INSTRUMENT_LOCAL static bool read_camera_pov_flexible(
    uintptr_t        cam_mgr,
    const ue4::EngineOffsets& off,
    hud::CameraView& out_cam,
    uintptr_t*       out_matched_offset = nullptr
) noexcept {
    if (!InProcessMemory::IsValidPtr(cam_mgr, 0x1B30U)) {
        return false;
    }
    const std::array<uintptr_t, 3> candidates{
        off.pov_location,
        off.camera_cache_private + 0x10U,
        off.camera_cache_alt + 0x10U
    };
    for (const uintptr_t base_off : candidates) {
        const auto loc = mem::read<ue4::FVector>(cam_mgr + base_off);
        const auto rot = mem::read<hud::EulerAngles>(cam_mgr + base_off + 0x0CU);
        const auto fov = mem::read<float>(cam_mgr + base_off + 0x18U);
        if (!loc.has_value() || !rot.has_value() || !fov.has_value()) {
            continue;
        }
        if (!std::isfinite(loc->x) || !std::isfinite(loc->y) || !std::isfinite(loc->z) ||
            !std::isfinite(rot->pitch) || !std::isfinite(rot->yaw) || !std::isfinite(rot->roll) ||
            !std::isfinite(*fov)) {
            continue;
        }
        if (*fov >= 20.0f && *fov <= 165.0f &&
            (std::abs(loc->x) > 0.5f || std::abs(loc->y) > 0.5f || std::abs(loc->z) > 0.5f) &&
            std::abs(loc->x) < 1000000.0f && std::abs(loc->y) < 1000000.0f && std::abs(loc->z) < 1000000.0f) {
            out_cam.location = {loc->x, loc->y, loc->z};
            out_cam.rotation = *rot;
            out_cam.fov      = *fov;
            if (out_matched_offset != nullptr) {
                *out_matched_offset = base_off;
            }
            return true;
        }
    }
    return false;
}

INSTRUMENT_LOCAL static void publish_shared_snapshot() noexcept {
    ipc::TelemetrySnapshotPayload snap{};
    ipc::fill_snapshot_from_telemetry(snap);
    ipc::write_snapshot_file(snap);
}

INSTRUMENT_LOCAL static void execute_diagnostic_tick() noexcept {
    poll_external_mod_menu_config();

    hud::GameplayManipulationConfig tick_gp_cfg{};
    {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud::GameplayManipulationConfig& gp_cfg = hud::get_gameplay_config();
        if (std::strncmp(hud_data.label_filter, "@gp:", 4) == 0) {
            (void)hud::parse_gameplay_ipc_string(hud_data.label_filter, hud_data, gp_cfg);
        }
        tick_gp_cfg = gp_cfg;
        pthread_mutex_unlock(&hud_data.mutex);
    }

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

    // 1. Resolve UWorld & PersistentLevel safely (with 180-tick level teardown write-lock protection)
    static uint32_t s_teardown_write_lock_ticks = 0;

    const uintptr_t gworld_abs = base + off.gworld_rva;
    const auto uworld = mem::read<uintptr_t>(gworld_abs);
    if (!uworld.has_value() ||
        !InProcessMemory::IsValidPtr(*uworld, 0x420U) ||
        !ue4::SpatialAndKinematicPipeline::validate_live_uobject(*uworld, guobjectarray_abs, off, nullptr))
    {
        s_teardown_write_lock_ticks = 180U;
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.match_active            = false;
        hud_data.node_count              = 0;
        hud_data.uworld_ptr              = 0;
        hud_data.ulevel_ptr              = 0;
        hud_data.local_pawn              = 0;
        ++hud_data.tick_seq;
        pthread_mutex_unlock(&hud_data.mutex);
        publish_shared_snapshot();
        return;
    }

    const auto ulevel = mem::read<uintptr_t>(*uworld + off.persistent_level);
    const auto game_instance = mem::read<uintptr_t>(*uworld + off.owning_game_instance);

    int32_t level_actor_count = 0;
    bool    ulevel_valid      = false;
    if (ulevel.has_value() &&
        InProcessMemory::IsValidPtr(*ulevel, 0xB0U) &&
        ue4::SpatialAndKinematicPipeline::validate_live_uobject(*ulevel, guobjectarray_abs, off, nullptr))
    {
        if (const auto arr = mem::read<ue4::TArrayHeader>(*ulevel + off.actor_array)) {
            if (InProcessMemory::IsValidPtr(arr->data_ptr, sizeof(uintptr_t)) &&
                arr->count >= 10 && arr->count <= 16384 && arr->max >= arr->count)
            {
                level_actor_count = arr->count;
                ulevel_valid      = true;
            }
        }
    }

    if (!ulevel_valid || !game_instance.has_value() || !InProcessMemory::IsValidPtr(*game_instance, 0x50U)) {
        s_teardown_write_lock_ticks = 180U;
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.match_active            = false;
        hud_data.node_count              = 0;
        hud_data.uworld_ptr              = *uworld;
        hud_data.ulevel_ptr              = ulevel.value_or(0ULL);
        hud_data.local_pawn              = 0;
        ++hud_data.tick_seq;
        pthread_mutex_unlock(&hud_data.mutex);
        publish_shared_snapshot();
        return;
    }

    // Log once per UWorld / PersistentLevel transition for effortless adb logcat confirmation
    const bool level_changed =
        (*uworld != g_state.last_logged_uworld) ||
        (ulevel.has_value() && *ulevel != g_state.last_logged_ulevel);

    if (level_changed) {
        s_teardown_write_lock_ticks = 180U;
        g_state.last_logged_uworld = *uworld;
        g_state.last_logged_ulevel = ulevel.value_or(0ULL);
        DIAG_LOGI(
            "World state active | UWorld=0x%llX | PersistentLevel=0x%llX (Actors=%d) | OwningGameInstance=0x%llX",
            static_cast<unsigned long long>(*uworld),
            static_cast<unsigned long long>(g_state.last_logged_ulevel),
            static_cast<int>(level_actor_count),
            static_cast<unsigned long long>(game_instance.value_or(0ULL))
        );
    } else if (s_teardown_write_lock_ticks > 0U) {
        --s_teardown_write_lock_ticks;
    }

    // Discover all loaded ULevel* in UWorld (PersistentLevel + streaming sublevels via UWorld::Levels / StreamingLevels)
    std::array<uintptr_t, 64> active_ulevels{};
    size_t num_ulevels = 0;
    auto push_unique_ulevel = [&](uintptr_t cand_lvl) noexcept {
        if (!InProcessMemory::IsValidPtr(cand_lvl, 0xB0U)) {
            return;
        }
        const auto arr = mem::read<ue4::TArrayHeader>(cand_lvl + off.actor_array);
        if (!arr.has_value() || !InProcessMemory::IsValidPtr(arr->data_ptr, sizeof(uintptr_t)) ||
            arr->count <= 0 || arr->count > 8192 || arr->max < arr->count || arr->max > 16384) {
            return;
        }
        for (size_t k = 0; k < num_ulevels; ++k) {
            if (active_ulevels[k] == cand_lvl) {
                return;
            }
        }
        if (num_ulevels < active_ulevels.size()) {
            active_ulevels[num_ulevels++] = cand_lvl;
        }
    };

    push_unique_ulevel(*ulevel);

    for (uintptr_t cand_off = 0x80U; cand_off <= 0x380U; cand_off += 8U) {
        const auto larr = mem::read<ue4::TArrayHeader>(*uworld + cand_off);
        if (!larr.has_value() || !InProcessMemory::IsValidPtr(larr->data_ptr, sizeof(uintptr_t)) ||
            larr->count <= 0 || larr->count > 128 || larr->max < larr->count || larr->max > 512) {
            continue;
        }
        const auto first_elem = mem::read<uintptr_t>(larr->data_ptr);
        if (!first_elem.has_value() || !InProcessMemory::IsValidPtr(*first_elem, 0x50U)) {
            continue;
        }

        // Case A: TArray<ULevel*> where Levels[0] == PersistentLevel
        if (*first_elem == *ulevel) {
            for (int32_t li = 0; li < larr->count && num_ulevels < active_ulevels.size(); ++li) {
                if (const auto sub_lvl = mem::read<uintptr_t>(larr->data_ptr + static_cast<size_t>(li) * sizeof(uintptr_t))) {
                    push_unique_ulevel(*sub_lvl);
                }
            }
            if (level_changed) {
                DIAG_LOGI("[UWORLD-LEVELS] Found UWorld::Levels at +0x%llX | TotalLevels=%zu (ArrayCount=%d)",
                          static_cast<unsigned long long>(cand_off), num_ulevels, larr->count);
            }
            continue;
        }

        // Case B: TArray<ULevelStreaming*> where element Class contains "LevelStreaming"
        std::array<char, 64> elem_cls{};
        if (const auto cptr = mem::read<uintptr_t>(*first_elem + 0x10U);
            cptr.has_value() && InProcessMemory::IsValidPtr(*cptr, 0x28U)) {
            if (const auto cfn = mem::read<uint32_t>(*cptr + 0x18U)) {
                (void)name_pool.resolve_name(*cfn, elem_cls);
            }
        }
        if (hud::detail::icontains_str(elem_cls.data(), "LevelStreaming")) {
            for (int32_t li = 0; li < larr->count && num_ulevels < active_ulevels.size(); ++li) {
                const auto stream_obj = mem::read<uintptr_t>(larr->data_ptr + static_cast<size_t>(li) * sizeof(uintptr_t));
                if (!stream_obj.has_value() || !InProcessMemory::IsValidPtr(*stream_obj, 0x160U)) {
                    continue;
                }
                for (uintptr_t loff = 0x80U; loff <= 0x158U; loff += 8U) {
                    const auto loaded_lvl = mem::read<uintptr_t>(*stream_obj + loff);
                    if (!loaded_lvl.has_value() || !InProcessMemory::IsValidPtr(*loaded_lvl, 0xB0U)) {
                        continue;
                    }
                    std::array<char, 64> lcls{};
                    if (const auto lcptr = mem::read<uintptr_t>(*loaded_lvl + 0x10U);
                        lcptr.has_value() && InProcessMemory::IsValidPtr(*lcptr, 0x28U)) {
                        if (const auto lcfn = mem::read<uint32_t>(*lcptr + 0x18U)) {
                            (void)name_pool.resolve_name(*lcfn, lcls);
                        }
                    }
                    if (std::strcmp(lcls.data(), "Level") == 0) {
                        push_unique_ulevel(*loaded_lvl);
                        break;
                    }
                }
            }
        }
    }

    // Helper lambda to verify an APlayerState pointer has "PlayerState" in its FName/Class
    auto is_valid_player_state = [&](uintptr_t cand_ps) noexcept -> bool {
        if (!InProcessMemory::IsValidPtr(cand_ps, 0x330U)) {
            return false;
        }
        uint32_t ps_fn = 0U;
        if (!ue4::SpatialAndKinematicPipeline::validate_live_uobject(cand_ps, guobjectarray_abs, off, &ps_fn) || ps_fn == 0U) {
            return false;
        }
        std::array<char, 64> ps_name{};
        if (name_pool.resolve_name(ps_fn, ps_name) && hud::detail::icontains_str(ps_name.data(), "PlayerState")) {
            return true;
        }
        if (const auto cptr = mem::read<uintptr_t>(cand_ps + 0x10U);
            cptr.has_value() && InProcessMemory::IsValidPtr(*cptr, 0x28U)) {
            if (const auto cfn = mem::read<uint32_t>(*cptr + 0x18U)) {
                std::array<char, 64> c_name{};
                if (name_pool.resolve_name(*cfn, c_name) && hud::detail::icontains_str(c_name.data(), "PlayerState")) {
                    return true;
                }
            }
        }
        return false;
    };

    // 2. Traverse UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> AcknowledgedPawn / PlayerCameraManager
    uintptr_t       local_pc            = 0;
    uintptr_t       local_pawn          = 0;
    uintptr_t       local_player_st     = 0;
    uintptr_t       local_cam_mgr       = 0;
    uintptr_t       matched_pov_off     = 0;
    bool            has_valid_camera    = false;
    ue4::FVector    local_origin{};
    hud::CameraView cam_view{};
    float           cur_walk_speed      = 600.0f;
    float           cur_max_accel       = 2048.0f;
    float           cur_jump_z          = 420.0f;
    float           cur_time_scale      = 1.0f;

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
                local_pc = *pc;

                // Read PlayerCameraManager (+0x2D0) -> CameraCachePrivate (+0x1AE0 / +0x1AF0 / +0x1B00)
                if (const auto cam_mgr = mem::read<uintptr_t>(*pc + off.player_camera_manager);
                    cam_mgr.has_value() &&
                    ue4::SpatialAndKinematicPipeline::validate_live_uobject(*cam_mgr, guobjectarray_abs, off, nullptr))
                {
                    local_cam_mgr = *cam_mgr;
                    has_valid_camera = read_camera_pov_flexible(*cam_mgr, off, cam_view, &matched_pov_off);
                }

                // Check APlayerController::AcknowledgedPawn (+0x2B8), then fallback to AController::Pawn (+0x278)
                uintptr_t cand_pawn = mem::read<uintptr_t>(*pc + off.acknowledged_pawn).value_or(0ULL);
                if (!ue4::SpatialAndKinematicPipeline::validate_live_uobject(cand_pawn, guobjectarray_abs, off, nullptr)) {
                    cand_pawn = mem::read<uintptr_t>(*pc + off.controller_pawn).value_or(0ULL);
                }

                if (cand_pawn != 0 &&
                    ue4::SpatialAndKinematicPipeline::validate_live_uobject(cand_pawn, guobjectarray_abs, off, nullptr))
                {
                    local_pawn = cand_pawn;
                    if (const auto loc = ue4::SpatialAndKinematicPipeline::read_actor_location(cand_pawn, guobjectarray_abs, off)) {
                        local_origin = *loc;
                    }
                }

                // Resolve verified APlayerState from Pawn (+0x240 / +0x248) or Controller (+0x228 / +0x230 / +0x238)
                if (local_pawn != 0) {
                    constexpr std::array<uintptr_t, 3> kPawnPsOffsets = {0x240U, 0x248U, 0x250U};
                    for (uintptr_t ps_off : kPawnPsOffsets) {
                        if (const auto ps = mem::read<uintptr_t>(local_pawn + ps_off);
                            ps.has_value() && is_valid_player_state(*ps)) {
                            local_player_st = *ps;
                            break;
                        }
                    }
                }
                if (local_player_st == 0) {
                    constexpr std::array<uintptr_t, 3> kCtrlPsOffsets = {0x228U, 0x230U, 0x238U};
                    for (uintptr_t ps_off : kCtrlPsOffsets) {
                        if (const auto ctrl_ps = mem::read<uintptr_t>(*pc + ps_off);
                            ctrl_ps.has_value() && is_valid_player_state(*ctrl_ps)) {
                            local_player_st = *ctrl_ps;
                            break;
                        }
                    }
                }
            }
        }
    }

    // 2b. Scan active ULevel::Actors (+0xA0) across PersistentLevel and streaming sublevels for:
    //     - Fallback Pawn / PlayerState / CameraManager when in Lobby or Custom Match
    //     - Active Generator Chargeable Progress (+0x0338) & Skillcheck Handler (Item 4)
    //     - Diagnostic actor dump on level change
    uint32_t active_gen_count     = 0;
    float    nearest_gen_pct      = 0.0f;
    float    nearest_gen_dist_sq  = 1e18f;
    static uint32_t s_skillchecks_handled = 0;
    size_t   logged_actors        = 0;
    bool     is_lobby_level       = false;

    for (size_t lvl_idx = 0; lvl_idx < num_ulevels; ++lvl_idx) {
        const uintptr_t cur_lvl = active_ulevels[lvl_idx];
        const auto actor_arr = mem::read<ue4::TArrayHeader>(cur_lvl + off.actor_array);
        if (!actor_arr.has_value() || !InProcessMemory::IsValidPtr(actor_arr->data_ptr, sizeof(uintptr_t))) {
            continue;
        }
        const size_t scan_limit = std::min<size_t>(
            static_cast<size_t>(std::max<int32_t>(0, actor_arr->count)),
            ue4::SpatialAndKinematicPipeline::kMaxActorsPerTick);
        std::array<char, 128> scan_name{};
        std::array<char, 128> cls_name{};

        const ue4::FVector ref_origin = (local_pawn != 0)
            ? local_origin
            : ue4::FVector{cam_view.location.x, cam_view.location.y, cam_view.location.z};

        for (size_t i = 0; i < scan_limit; ++i) {
            const auto a_ptr = mem::read<uintptr_t>(actor_arr->data_ptr + i * sizeof(uintptr_t));
            if (!a_ptr.has_value() || !InProcessMemory::IsValidPtr(*a_ptr, 0x150U)) {
                continue;
            }
            uint32_t fn_id = 0U;
            if (!ue4::SpatialAndKinematicPipeline::validate_live_uobject(*a_ptr, guobjectarray_abs, off, &fn_id) || fn_id == 0U) {
                continue;
            }
            if (!name_pool.resolve_name(fn_id, scan_name)) {
                continue;
            }

            cls_name[0] = '\0';
            if (const auto cls_ptr = mem::read<uintptr_t>(*a_ptr + 0x10U);
                cls_ptr.has_value() && InProcessMemory::IsValidPtr(*cls_ptr, 0x28U)) {
                if (const auto cls_fn = mem::read<uint32_t>(*cls_ptr + 0x18U)) {
                    (void)name_pool.resolve_name(*cls_fn, cls_name);
                }
            }

            if (hud::detail::icontains_str(scan_name.data(), "OfflineLobby") ||
                hud::detail::icontains_str(scan_name.data(), "MatchLobby") ||
                hud::detail::icontains_str(scan_name.data(), "StartScreen") ||
                hud::detail::icontains_str(scan_name.data(), "DBDGame_Start") ||
                hud::detail::icontains_str(scan_name.data(), "RoleSelection")) {
                is_lobby_level = true;
            }

            if (level_changed && logged_actors < 300U) {
                const auto a_pos = ue4::SpatialAndKinematicPipeline::read_actor_location(*a_ptr, guobjectarray_abs, off);
                const auto rc_ptr = mem::read<uintptr_t>(*a_ptr + off.root_component).value_or(0ULL);
                DIAG_LOGI(
                    "[LEVEL-SCAN] Lvl[%zu] Actor[%zu]=0x%llX FName=\"%s\" Class=\"%s\" RC=0x%llX HasPos=%d Pos=(%.1f, %.1f, %.1f)",
                    lvl_idx,
                    i,
                    static_cast<unsigned long long>(*a_ptr),
                    scan_name.data(),
                    cls_name.data(),
                    static_cast<unsigned long long>(rc_ptr),
                    a_pos.has_value() ? 1 : 0,
                    static_cast<double>(a_pos ? a_pos->x : 0.0f),
                    static_cast<double>(a_pos ? a_pos->y : 0.0f),
                    static_cast<double>(a_pos ? a_pos->z : 0.0f)
                );
                ++logged_actors;
            }

            // Fallback PlayerState discovery in ULevel::Actors (e.g. DBDPlayerState / MenuPlayerState)
            if (local_player_st == 0 && is_valid_player_state(*a_ptr)) {
                local_player_st = *a_ptr;
            }

            // Fallback CameraManager discovery in ULevel::Actors
            if (!has_valid_camera &&
                (hud::detail::icontains_str(scan_name.data(), "CameraManager") ||
                 hud::detail::icontains_str(cls_name.data(), "CameraManager"))) {
                if (read_camera_pov_flexible(*a_ptr, off, cam_view, &matched_pov_off)) {
                    local_cam_mgr    = *a_ptr;
                    has_valid_camera = true;
                }
            }

            // Fallback Local Pawn discovery (e.g. Lobby or Match Camper / Slasher / Character with valid CharacterMovement)
            if (local_pawn == 0 &&
                (hud::detail::icontains_str(scan_name.data(), "Camper") ||
                 hud::detail::icontains_str(scan_name.data(), "Slasher") ||
                 hud::detail::icontains_str(scan_name.data(), "Survivor") ||
                 hud::detail::icontains_str(scan_name.data(), "Character") ||
                 hud::detail::icontains_str(cls_name.data(), "Camper") ||
                 hud::detail::icontains_str(cls_name.data(), "Slasher")))
            {
                if (const auto mc = mem::read<uintptr_t>(*a_ptr + off.character_movement);
                    mc.has_value() && ue4::SpatialAndKinematicPipeline::validate_live_uobject(*mc, guobjectarray_abs, off, nullptr))
                {
                    local_pawn = *a_ptr;
                    if (const auto loc = ue4::SpatialAndKinematicPipeline::read_actor_location(*a_ptr, guobjectarray_abs, off)) {
                        local_origin = *loc;
                    }
                    if (local_player_st == 0) {
                        constexpr std::array<uintptr_t, 3> kPawnPsOffsets = {0x240U, 0x248U, 0x250U};
                        for (uintptr_t ps_off : kPawnPsOffsets) {
                            if (const auto ps = mem::read<uintptr_t>(*a_ptr + ps_off);
                                ps.has_value() && is_valid_player_state(*ps)) {
                                local_player_st = *ps;
                                break;
                            }
                        }
                    }
                }
            }

            // Item 4: Generator Progress (+0x0338) & Skillcheck Handler (strictly real interactive Generators only!)
            const char* cat = ue4::SpatialAndKinematicPipeline::classify_actor_category(scan_name.data(), cls_name.data());
            if (cat != nullptr && std::strcmp(cat, "Generator") == 0) {
                if (const auto gpos = ue4::SpatialAndKinematicPipeline::read_actor_location(*a_ptr, guobjectarray_abs, off)) {
                    const float origin_sq = gpos->x * gpos->x + gpos->y * gpos->y + gpos->z * gpos->z;
                    const bool is_ghost_pos =
                        (std::fabs(gpos->x - 330.0f) < 5.0f &&
                         std::fabs(gpos->y - 180.0f) < 5.0f &&
                         std::fabs(gpos->z - 20.0f) < 5.0f);
                    if (origin_sq >= 100.0f && !is_ghost_pos) {
                        const float gen_pct = ue4::SpatialAndKinematicPipeline::read_generator_progress_pct(
                            *a_ptr,
                            guobjectarray_abs,
                            off);
                        if (!hud::adapter::should_hide_generator(gen_pct)) {
                            ++active_gen_count;
                            const float dsq = ref_origin.dist_squared(*gpos);
                            if (dsq < nearest_gen_dist_sq) {
                                nearest_gen_dist_sq = dsq;
                                nearest_gen_pct     = gen_pct;
                            }
                        }
                    }
                }
            }

            // Trap Immunity: disable overlap collision on nearby BearTrap / Trap actors when enabled
            if (tick_gp_cfg.trap_immunity &&
                (hud::detail::icontains_str(scan_name.data(), "BearTrap") ||
                 hud::detail::icontains_str(cls_name.data(), "BearTrap"))) {
                ue4::SpatialAndKinematicPipeline::disable_trap_actor_collision(*a_ptr, guobjectarray_abs, off);
            }
        }
    }

    // Dynamically resolve UPlayerController -> AcknowledgedPawn -> UCharacterMovementComponent each frame
    const hud::CharacterMovementChain move_chain =
        ue4::SpatialAndKinematicPipeline::resolve_local_movement_chain(
            local_pc, local_pawn, guobjectarray_abs, off);
    if (move_chain.acknowledged_pawn != 0) {
        local_pawn = move_chain.acknowledged_pawn;
    }

    // Read current kinematic state if local_pawn / UCharacterMovementComponent is resolved
    static uintptr_t s_base_kin_pawn       = 0;
    static float     s_base_walk_speed     = 600.0f;
    static float     s_base_max_accel      = 2048.0f;
    static float     s_base_jump_z         = 420.0f;
    static float     s_base_time_scale     = 1.0f;
    static bool      s_kinematics_was_active = false;

    if (local_pawn != 0) {
        if (const auto td = mem::read<float>(local_pawn + off.custom_time_dilation)) {
            if (std::isfinite(*td)) {
                cur_time_scale = *td;
            }
        }
        if (move_chain.is_valid && move_chain.movement_component != 0) {
            if (const auto ws = mem::read<float>(move_chain.movement_component + off.max_walk_speed)) {
                if (std::isfinite(*ws)) cur_walk_speed = *ws;
            }
            if (const auto ma = mem::read<float>(move_chain.movement_component + off.max_acceleration)) {
                if (std::isfinite(*ma)) cur_max_accel = *ma;
            }
            if (const auto jz = mem::read<float>(move_chain.movement_component + off.jump_z_velocity)) {
                if (std::isfinite(*jz)) cur_jump_z = *jz;
            }
        }

        if (local_pawn != s_base_kin_pawn || !s_kinematics_was_active) {
            s_base_kin_pawn   = local_pawn;
            s_base_walk_speed = cur_walk_speed;
            s_base_max_accel  = cur_max_accel;
            s_base_jump_z     = cur_jump_z;
            s_base_time_scale = cur_time_scale;
        }
    } else {
        s_base_kin_pawn         = 0;
        s_kinematics_was_active = false;
    }

    if (!has_valid_camera && local_pawn != 0) {
        cam_view.location = {local_origin.x, local_origin.y, local_origin.z + 65.0f};
        cam_view.fov      = 90.0f;
        has_valid_camera  = true;
    }

    if (local_pawn == 0 && has_valid_camera) {
        local_origin = {cam_view.location.x, cam_view.location.y, cam_view.location.z};
    }

    // Item 3: Read & Override Player Name (works in BOTH Lobby and Active Match whenever APlayerState is live!)
    std::array<char, 64> current_player_name{};
    if (local_player_st != 0) {
        (void)ue4::LocalPlayerNameController::read_local_name(
            local_player_st,
            off,
            current_player_name
        );
    }

    // 3. Traverse all active ULevels -> Actors arrays using pre-allocated static memory pools (kMaxSceneNodes = 256)
    static std::array<ue4::EntitySnapshot, hud::kMaxSceneNodes> s_snapshots_pool{};
    hud::adapter::DiagnosticTickStaticPool& tick_pool = hud::adapter::get_diagnostic_tick_pool();
    tick_pool.reset();
    size_t collected_count = 0;

    if (!is_lobby_level && num_ulevels > 0 && has_valid_camera) {
        collected_count = ue4::SpatialAndKinematicPipeline::collect_nearby_entities_multi_level(
            std::span<const uintptr_t>{active_ulevels.data(), num_ulevels},
            guobjectarray_abs,
            local_pawn,
            local_origin,
            1000.0f,
            off,
            name_pool,
            s_snapshots_pool
        );

        for (size_t i = 0; i < collected_count; ++i) {
            (void)hud::adapter::push_static_entity_sample(
                tick_pool,
                s_snapshots_pool[i].world_pos.x,
                s_snapshots_pool[i].world_pos.y,
                s_snapshots_pool[i].world_pos.z,
                s_snapshots_pool[i].yaw_deg,
                s_snapshots_pool[i].distance_meters,
                s_snapshots_pool[i].progress_pct,
                false,
                s_snapshots_pool[i].category_label,
                nullptr
            );
        }
    }

    // 4. Synchronize camera, entities, player name, and kinematics with shared state
    bool  should_apply_kinematics  = false;
    float target_walk_speed        = cur_walk_speed;
    float target_accel             = cur_max_accel;
    float target_jump_z            = cur_jump_z;
    float target_time_scale        = cur_time_scale;
    bool  should_apply_player_name = false;
    std::array<char, 64> target_player_name{};

    const hud::adapter::KinematicsSyncResult sync_res = hud::adapter::sync_telemetry_to_hud(
        cam_view.location.x,
        cam_view.location.y,
        cam_view.location.z,
        cam_view.rotation.pitch,
        cam_view.rotation.yaw,
        cam_view.rotation.roll,
        cam_view.fov,
        tick_pool.samples.data(),
        tick_pool.active_count,
        s_base_walk_speed,
        s_base_max_accel,
        s_base_jump_z,
        s_base_time_scale,
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

    // Update pointers & Generator/Skillcheck telemetry in SceneTelemetryData
    size_t   final_node_count   = 0;
    bool     final_match_active = false;
    uint32_t cnt_killer         = 0;
    uint32_t cnt_survivor       = 0;
    uint32_t cnt_generator      = 0;
    uint32_t cnt_totem          = 0;
    uint32_t cnt_chest          = 0;
    uint32_t cnt_pallet         = 0;
    uint32_t cnt_hatch          = 0;
    uint32_t cnt_exitgate       = 0;
    uint32_t cnt_hook           = 0;
    {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.uworld_ptr           = *uworld;
        hud_data.ulevel_ptr           = *ulevel;
        hud_data.local_pawn           = local_pawn;
        hud_data.active_generators    = active_gen_count;
        hud_data.nearest_gen_progress = nearest_gen_pct;
        hud_data.skillchecks_handled  = s_skillchecks_handled;
        final_node_count              = hud_data.node_count;
        final_match_active            = hud_data.match_active;
        for (size_t i = 0; i < final_node_count && i < hud::kMaxSceneNodes; ++i) {
            switch (hud_data.nodes[i].category) {
                case hud::NodeCategory::Antagonist:  ++cnt_killer;    break;
                case hud::NodeCategory::Teammate:    ++cnt_survivor;  break;
                case hud::NodeCategory::PowerNode:   ++cnt_generator; break;
                case hud::NodeCategory::Shrine:      ++cnt_totem;     break;
                case hud::NodeCategory::Container:   ++cnt_chest;     break;
                case hud::NodeCategory::Barrier:     ++cnt_pallet;    break;
                case hud::NodeCategory::Portal:      ++cnt_hatch;     break;
                case hud::NodeCategory::ExitGate:    ++cnt_exitgate;  break;
                case hud::NodeCategory::SpecialProp: ++cnt_hook;      break;
                default: break;
            }
        }
        pthread_mutex_unlock(&hud_data.mutex);
    }

    // Print per-category counts at match start (and when map actor spawn completes)
    static uintptr_t s_cat_logged_ulevel = 0;
    static size_t    s_cat_logged_nodes  = 0;
    if (final_match_active && final_node_count > 0 &&
        (*ulevel != s_cat_logged_ulevel || (s_cat_logged_nodes < 10 && final_node_count >= 10))) {
        s_cat_logged_ulevel = *ulevel;
        s_cat_logged_nodes  = final_node_count;
        DIAG_LOGI(
            "[MATCH-CATEGORY-COUNTS] ULevel=0x%llX TotalNodes=%zu | Killer=%u | Survivor=%u | Generator=%u | Totem=%u | Chest=%u | Pallet=%u | Hatch=%u | ExitGate=%u | Hook=%u",
            static_cast<unsigned long long>(*ulevel),
            final_node_count,
            cnt_killer,
            cnt_survivor,
            cnt_generator,
            cnt_totem,
            cnt_chest,
            cnt_pallet,
            cnt_hatch,
            cnt_exitgate,
            cnt_hook
        );
    } else if (!final_match_active) {
        s_cat_logged_ulevel = 0;
        s_cat_logged_nodes  = 0;
    }

    const bool write_safe = (!is_lobby_level && local_pawn != 0) && (s_teardown_write_lock_ticks == 0U);

    // Execute Item 3: Player Name Override + Read-back Verification Log
    // Works in both Lobby and Active Match whenever APlayerState is a verified live UObject
    if (should_apply_player_name && local_player_st != 0 && target_player_name[0] != '\0') {
        if (ue4::SpatialAndKinematicPipeline::validate_canonical_uobject(local_player_st, 0x330U, guobjectarray_abs, off)) {
            const Status name_st = ue4::LocalPlayerNameController::override_local_name_utf8(
                local_player_st,
                off,
                target_player_name.data()
            );
            std::array<char, 64> readback_name{};
            uintptr_t matched_name_off = off.player_name_private;
            (void)ue4::LocalPlayerNameController::read_local_name(
                local_player_st,
                off,
                readback_name,
                &matched_name_off
            );
            static char s_last_logged_name[64]{};
            static uint32_t s_name_log_tick = 0;
            if (std::strcmp(s_last_logged_name, readback_name.data()) != 0 || (++s_name_log_tick % 120U) == 1U) {
                std::strncpy(s_last_logged_name, readback_name.data(), sizeof(s_last_logged_name) - 1);
                s_last_logged_name[sizeof(s_last_logged_name) - 1] = '\0';
                DIAG_LOGI(
                    "[NAME-OVERRIDE] Status=%d on APlayerState=0x%llX (+0x%llX) | Requested=\"%s\" -> Readback=\"%s\"",
                    static_cast<int>(name_st),
                    static_cast<unsigned long long>(local_player_st),
                    static_cast<unsigned long long>(matched_name_off),
                    target_player_name.data(),
                    readback_name.data()
                );
            }
            if (readback_name[0] != '\0') {
                std::strncpy(current_player_name.data(), readback_name.data(), current_player_name.size() - 1);
                current_player_name[current_player_name.size() - 1] = '\0';
                hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
                pthread_mutex_lock(&hud_data.mutex);
                std::strncpy(hud_data.in_game_player_name, readback_name.data(), hud::kMaxLabelLen - 1);
                hud_data.in_game_player_name[hud::kMaxLabelLen - 1] = '\0';
                pthread_mutex_unlock(&hud_data.mutex);
            }
        }
    }

    // Execute Item 2: Persistent Character Movement Component Hook (Kinematics Override + Baseline Restore)
    if (write_safe && should_apply_kinematics && (local_pc != 0 || local_pawn != 0)) {
        static uintptr_t s_last_kin_err_pawn = 0;
        const Status kin_st = ue4::SpatialAndKinematicPipeline::apply_kinematic_limits(
            local_pc,
            local_pawn,
            guobjectarray_abs,
            off,
            target_walk_speed,
            target_accel,
            target_jump_z,
            target_time_scale,
            sync_res.movement_dirty_flag
        );
        if (kin_st != Status::Ok && local_pawn != s_last_kin_err_pawn) {
            s_last_kin_err_pawn = local_pawn;
            DIAG_LOGE(
                "[WRITE-ERR] apply_kinematic_limits failed (Status=%d) on PC=0x%llX Pawn=0x%llX",
                static_cast<int>(kin_st),
                static_cast<unsigned long long>(local_pc),
                static_cast<unsigned long long>(local_pawn));
        } else if (kin_st == Status::Ok) {
            s_last_kin_err_pawn     = 0;
            s_kinematics_was_active = true;
        }
    } else if (write_safe && !should_apply_kinematics && s_kinematics_was_active && (local_pc != 0 || local_pawn != 0)) {
        const Status restore_st = ue4::SpatialAndKinematicPipeline::apply_kinematic_limits(
            local_pc,
            local_pawn,
            guobjectarray_abs,
            off,
            s_base_walk_speed,
            s_base_max_accel,
            s_base_jump_z,
            s_base_time_scale,
            false
        );
        if (restore_st != Status::Ok) {
            DIAG_LOGE(
                "[WRITE-ERR] baseline restore apply_kinematic_limits failed (Status=%d) on Pawn=0x%llX",
                static_cast<int>(restore_st),
                static_cast<unsigned long long>(local_pawn));
        } else {
            s_kinematics_was_active = false;
        }
    } else if (!write_safe) {
        s_kinematics_was_active = false;
    }

    // Execute Advanced Feature 2: Field of View (FOV) Changer (APlayerCameraManager::DefaultFOV +0x248)
    float clamped_fov_target = cam_view.fov;
    if (local_cam_mgr != 0 && hud::compute_clamped_fov_target(sync_res, cam_view.fov, &clamped_fov_target)) {
        static uintptr_t s_last_fov_err_mgr = 0;
        const Status fov_st = ue4::SpatialAndKinematicPipeline::apply_fov_override(
            local_cam_mgr,
            matched_pov_off,
            guobjectarray_abs,
            off,
            clamped_fov_target
        );
        if (fov_st != Status::Ok && local_cam_mgr != s_last_fov_err_mgr) {
            s_last_fov_err_mgr = local_cam_mgr;
            DIAG_LOGE(
                "[WRITE-ERR] apply_fov_override failed (Status=%d) on CamMgr=0x%llX",
                static_cast<int>(fov_st),
                static_cast<unsigned long long>(local_cam_mgr));
        } else if (fov_st == Status::Ok) {
            s_last_fov_err_mgr = 0;
            cam_view.fov       = clamped_fov_target;
        }
    }

    // Execute Advanced Feature 3: Auto Skillcheck & Trap Immunity
    if (write_safe && sync_res.auto_skillcheck && (local_pawn != 0 || local_pc != 0)) {
        if (ue4::SpatialAndKinematicPipeline::apply_auto_skillcheck(local_pawn, local_pc, guobjectarray_abs, off)) {
            ++s_skillchecks_handled;
        }
    }
    if (write_safe && sync_res.trap_immunity && local_pawn != 0) {
        ue4::SpatialAndKinematicPipeline::apply_trap_immunity(local_pawn, guobjectarray_abs, off);
    }

    // Execute Advanced Feature 4: Fly Cheat & Force Escape
    static bool s_was_flying = false;
    if (write_safe && (local_pc != 0 || local_pawn != 0) && (sync_res.fly_cheat_enabled || s_was_flying)) {
        static uintptr_t s_last_fly_err_pawn = 0;
        const Status fly_st = ue4::SpatialAndKinematicPipeline::apply_fly_mode(
            local_pc,
            local_pawn,
            guobjectarray_abs,
            off,
            sync_res.fly_cheat_enabled,
            target_walk_speed
        );
        if (fly_st != Status::Ok && local_pawn != s_last_fly_err_pawn) {
            s_last_fly_err_pawn = local_pawn;
            DIAG_LOGE(
                "[WRITE-ERR] apply_fly_mode failed (Status=%d) on Pawn=0x%llX",
                static_cast<int>(fly_st),
                static_cast<unsigned long long>(local_pawn));
        } else if (fly_st == Status::Ok) {
            s_last_fly_err_pawn = 0;
        }
        s_was_flying = sync_res.fly_cheat_enabled;
    } else if (!write_safe) {
        s_was_flying = false;
    }
    if (write_safe && sync_res.force_escape && local_pawn != 0) {
        const ue4::FVector* escape_pos = nullptr;
        for (size_t i = 0; i < collected_count; ++i) {
            if (std::strcmp(s_snapshots_pool[i].category_label, "ExitGate") == 0 ||
                std::strcmp(s_snapshots_pool[i].category_label, "Hatch") == 0) {
                escape_pos = &s_snapshots_pool[i].world_pos;
                break;
            }
        }
        const Status esc_st = ue4::SpatialAndKinematicPipeline::apply_force_escape(
            local_pawn,
            local_player_st,
            escape_pos,
            guobjectarray_abs,
            off
        );
        if (esc_st != Status::Ok) {
            DIAG_LOGE(
                "[WRITE-ERR] apply_force_escape failed (Status=%d) on Pawn=0x%llX PS=0x%llX",
                static_cast<int>(esc_st),
                static_cast<unsigned long long>(local_pawn),
                static_cast<unsigned long long>(local_player_st));
        }
        hud::set_force_escape(false);
    }

    // Execute Advanced Feature 5: Immobilize State & Player Role Setters
    if (write_safe && sync_res.should_apply_immobilize && local_pawn != 0) {
        static uintptr_t s_last_imm_err_pawn = 0;
        const Status imm_st = ue4::SpatialAndKinematicPipeline::apply_immobilize_state(
            local_pawn,
            guobjectarray_abs,
            off,
            sync_res.immobilize_state
        );
        if (imm_st != Status::Ok && local_pawn != s_last_imm_err_pawn) {
            s_last_imm_err_pawn = local_pawn;
            DIAG_LOGE(
                "[WRITE-ERR] apply_immobilize_state failed (Status=%d) on Pawn=0x%llX",
                static_cast<int>(imm_st),
                static_cast<unsigned long long>(local_pawn));
        } else if (imm_st == Status::Ok) {
            s_last_imm_err_pawn = 0;
        }
    }
    if (write_safe && sync_res.should_apply_role && local_player_st != 0) {
        static uintptr_t s_last_role_err_ps = 0;
        const Status role_st = ue4::SpatialAndKinematicPipeline::apply_player_role(
            local_player_st,
            guobjectarray_abs,
            off,
            sync_res.player_role
        );
        if (role_st != Status::Ok && local_player_st != s_last_role_err_ps) {
            s_last_role_err_ps = local_player_st;
            DIAG_LOGE(
                "[WRITE-ERR] apply_player_role failed (Status=%d) on PS=0x%llX",
                static_cast<int>(role_st),
                static_cast<unsigned long long>(local_player_st));
        } else if (role_st == Status::Ok) {
            s_last_role_err_ps = 0;
        }
    }

    // Periodic Telemetry Summary Log (Items 1 & 4) every ~120 ticks (~2 seconds)
    static uint32_t s_summary_tick = 0;
    if ((++s_summary_tick % 120U) == 1U) {
        DIAG_LOGI(
            "[AURA-SYNC] MatchActive=%d | Actors=%d | Collected=%zu | AuraNodes=%zu | PC=0x%llX Pawn=0x%llX PS=0x%llX (\"%s\") | CamMgr=0x%llX (POV+0x%llX) Pos=(%.1f, %.1f, %.1f) Rot=(%.1f, %.1f, %.1f) FOV=%.1f",
            final_match_active ? 1 : 0,
            static_cast<int>(level_actor_count),
            collected_count,
            final_node_count,
            static_cast<unsigned long long>(local_pc),
            static_cast<unsigned long long>(local_pawn),
            static_cast<unsigned long long>(local_player_st),
            current_player_name.data(),
            static_cast<unsigned long long>(local_cam_mgr),
            static_cast<unsigned long long>(matched_pov_off),
            static_cast<double>(cam_view.location.x),
            static_cast<double>(cam_view.location.y),
            static_cast<double>(cam_view.location.z),
            static_cast<double>(cam_view.rotation.pitch),
            static_cast<double>(cam_view.rotation.yaw),
            static_cast<double>(cam_view.rotation.roll),
            static_cast<double>(cam_view.fov)
        );
        DIAG_LOGI(
            "[GEN-SKILLCHECK] ActiveGenerators=%u | NearestGenProgress=%.1f%% | SkillChecksHandled=%u",
            static_cast<unsigned>(active_gen_count),
            static_cast<double>(nearest_gen_pct),
            static_cast<unsigned>(s_skillchecks_handled)
        );
        if (final_match_active) {
            DIAG_LOGI(
                "[MATCH-CATEGORY-COUNTS] ULevel=0x%llX TotalNodes=%zu | Killer=%u | Survivor=%u | Generator=%u | Totem=%u | Chest=%u | Pallet=%u | Hatch=%u | ExitGate=%u | Hook=%u",
                static_cast<unsigned long long>(*ulevel),
                final_node_count,
                cnt_killer,
                cnt_survivor,
                cnt_generator,
                cnt_totem,
                cnt_chest,
                cnt_pallet,
                cnt_hatch,
                cnt_exitgate,
                cnt_hook
            );
        }
    }

    // Publish lockless shared telemetry snapshot (/data/local/tmp/dbdm_telemetry_shm.bin) for 60 FPS APK overlay
    publish_shared_snapshot();
}

INSTRUMENT_LOCAL static void* ipc_server_worker_entry(void* /*arg*/) noexcept {
    pthread_setname_np(pthread_self(), "ue4_ipc_srv");

    const int srv_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv_fd < 0) {
        DIAG_LOGE("Failed to create AF_UNIX IPC socket.");
        return nullptr;
    }

    struct sockaddr_un addr{};
    const socklen_t addr_len = ipc::build_abstract_sockaddr(addr);
    if (::bind(srv_fd, reinterpret_cast<const struct sockaddr*>(&addr), addr_len) != 0) {
        DIAG_LOGW("Abstract socket bind failed (already bound or restricted); closing fd.");
        ::close(srv_fd);
        return nullptr;
    }

    if (::listen(srv_fd, 8) != 0) {
        ::close(srv_fd);
        return nullptr;
    }

    g_state.ipc_listen_fd.store(srv_fd, std::memory_order_release);
    DIAG_LOGI("Headless Telemetry IPC Server listening on abstract socket @%s", ipc::kAbstractSocketName);

    struct timeval accept_tv{};
    accept_tv.tv_sec  = 0;
    accept_tv.tv_usec = 250000; // 250ms poll timeout so shutdown_instrumentation wakes cleanly
    ::setsockopt(srv_fd, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv));

    while (g_state.running.load(std::memory_order_acquire)) {
        const int client_fd = ::accept4(srv_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            continue;
        }

        struct timeval io_tv{};
        io_tv.tv_sec  = 0;
        io_tv.tv_usec = 50000;
        ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &io_tv, sizeof(io_tv));
        ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &io_tv, sizeof(io_tv));

        ipc::ControlPayload req{};
        const ssize_t r = ::recv(client_fd, &req, sizeof(req), 0);
        if (r == static_cast<ssize_t>(sizeof(req)) && req.magic == ipc::kIpcMagicVersion) {
            if (req.packet_type == static_cast<std::uint32_t>(ipc::PacketType::PushControl)) {
                ipc::apply_control_to_telemetry(req);
                if (std::strncmp(req.label_filter, "@gp:", 4) == 0) {
                    hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
                    pthread_mutex_lock(&hud_data.mutex);
                    (void)hud::parse_gameplay_ipc_string(
                        req.label_filter,
                        hud_data,
                        hud::get_gameplay_config());
                    pthread_mutex_unlock(&hud_data.mutex);
                }
            }
            ipc::TelemetrySnapshotPayload resp{};
            ipc::fill_snapshot_from_telemetry(resp);
            (void)::send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        }

        ::close(client_fd);
    }

    const int fd_to_close = g_state.ipc_listen_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd_to_close >= 0) {
        ::close(fd_to_close);
    }
    return nullptr;
}

INSTRUMENT_LOCAL static void* telemetry_worker_entry(void* /*arg*/) noexcept {
    pthread_setname_np(pthread_self(), "ue4_diag_worker");
    DIAG_LOGI("Headless telemetry worker started (no EGL/ImGui hooks); polling dl_iterate_phdr for libUE4.so...");

    while (g_state.running.load(std::memory_order_acquire)) {
        const auto mod = elf::find_module("libUE4.so");
        if (mod.has_value()) {
            std::array<uint8_t, sizeof(Elf64_Ehdr)> hdr{};
            if (InProcessMemory::safe_read(mod->base_address, hdr) == Status::Ok) {
                g_state.ue4_module = *mod;
                log_resolved_engine_anchors();
                DIAG_LOGI("Headless telemetry backend active on libUE4.so @ 0x%llX (waiting for live match state)",
                          static_cast<unsigned long long>(g_state.ue4_module.base_address));
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
        "Initializing Headless UE4.27 AArch64 Instrumentation (Configured RVAs: GWorld=0x%llX, GNamePool=0x%llX)",
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
    if (rc != 0) {
        pthread_attr_destroy(&attr);
        g_state.running.store(false, std::memory_order_release);
        DIAG_LOGE("pthread_create failed with error code %d.", rc);
        return false;
    }
    g_state.thread_active.store(true, std::memory_order_release);

    if (pthread_create(&g_state.ipc_thread, &attr, &ipc_server_worker_entry, nullptr) == 0) {
        g_state.ipc_thread_active.store(true, std::memory_order_release);
    }
    pthread_attr_destroy(&attr);
    return true;
}

INSTRUMENT_LOCAL void shutdown_instrumentation() noexcept {
    if (!g_state.running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const int listen_fd = g_state.ipc_listen_fd.exchange(-1, std::memory_order_acq_rel);
    if (listen_fd >= 0) {
        ::shutdown(listen_fd, SHUT_RDWR);
        ::close(listen_fd);
    }
    if (g_state.ipc_thread_active.exchange(false, std::memory_order_acq_rel)) {
        pthread_join(g_state.ipc_thread, nullptr);
    }
    if (g_state.thread_active.exchange(false, std::memory_order_acq_rel)) {
        pthread_join(g_state.worker_thread, nullptr);
    }
}

INSTRUMENT_LOCAL static bool is_current_process_game() noexcept {
    std::FILE* fp = std::fopen("/proc/self/cmdline", "rb");
    if (!fp) {
        return false;
    }
    char cmdline[128]{};
    const std::size_t n = std::fread(cmdline, 1, sizeof(cmdline) - 1, fp);
    std::fclose(fp);
    cmdline[n] = '\0';
    return std::strstr(cmdline, "com.netease.dbdena") != nullptr;
}

// Pure 3D-to-2D perspective projection for Standalone APK Overlay
INSTRUMENT_LOCAL static bool project_world_to_overlay_screen(
    const hud::Vec3&       world_pos,
    const hud::CameraView& camera,
    float                  screen_width,
    float                  screen_height,
    float&                 out_x,
    float&                 out_y,
    float                  vertical_offset_cm = 85.0f) noexcept {

    if (screen_width <= 1.0f || screen_height <= 1.0f) {
        return false;
    }
    if (!std::isfinite(world_pos.x) || !std::isfinite(world_pos.y) || !std::isfinite(world_pos.z) ||
        !std::isfinite(camera.location.x) || !std::isfinite(camera.location.y) || !std::isfinite(camera.location.z)) {
        return false;
    }

    auto normalize_angle = [](float deg) noexcept -> float {
        if (!std::isfinite(deg)) {
            return 0.0f;
        }
        float w = std::fmod(deg, 360.0f);
        if (w < 0.0f) w += 360.0f;
        if (w > 180.0f) w -= 360.0f;
        return w;
    };

    constexpr float kDegToRad           = std::numbers::pi_v<float> / 180.0f;
    constexpr float kNearClipPlaneUnits = 1.0f;

    const hud::Vec3 elevated{
        world_pos.x,
        world_pos.y,
        world_pos.z + vertical_offset_cm
    };

    const float pitch_rad = std::clamp(normalize_angle(camera.rotation.pitch), -89.9f, 89.9f) * kDegToRad;
    const float yaw_rad   = normalize_angle(camera.rotation.yaw) * kDegToRad;
    const float roll_rad  = normalize_angle(camera.rotation.roll) * kDegToRad;

    const float sp = std::sin(pitch_rad), cp = std::cos(pitch_rad);
    const float sy = std::sin(yaw_rad),   cy = std::cos(yaw_rad);
    const float sr = std::sin(roll_rad),  cr = std::cos(roll_rad);

    const hud::Vec3 axis_forward{cp * cy, cp * sy, sp};
    const hud::Vec3 axis_right{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    const hud::Vec3 axis_up{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};

    const hud::Vec3 delta = elevated - camera.location;
    const float view_x = delta.dot(axis_right);
    const float view_y = delta.dot(axis_up);
    const float view_z = delta.dot(axis_forward);

    if (!std::isfinite(view_z) || view_z < kNearClipPlaneUnits) {
        return false;
    }

    const float clamped_fov  = std::clamp(std::isfinite(camera.fov) ? camera.fov : 90.0f, 10.0f, 170.0f);
    const float tan_half_fov = std::tan((clamped_fov * 0.5f) * kDegToRad);
    if (tan_half_fov <= 1e-5f) {
        return false;
    }

    const float center_x     = screen_width * 0.5f;
    const float center_y     = screen_height * 0.5f;
    const float focal_length = center_x / tan_half_fov;

    const float proj_x = center_x + (view_x * focal_length) / view_z;
    const float proj_y = center_y - (view_y * focal_length) / view_z;

    if (!std::isfinite(proj_x) || !std::isfinite(proj_y) ||
        proj_x < 0.0f || proj_x > screen_width ||
        proj_y < 0.0f || proj_y > screen_height) {
        return false;
    }

    out_x = proj_x;
    out_y = proj_y;
    return true;
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
            DIAG_LOGI("Zygisk postAppSpecialize matched com.netease.dbdena; starting headless telemetry worker.");
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
    // Only start the UE4 background worker when loaded inside com.netease.dbdena;
    // when loaded inside the standalone Mod Menu APK (com.dbdm.modmenu), act purely as an IPC client.
    if (runtime::is_current_process_game()) {
        if (!runtime::initialize_instrumentation()) {
            return JNI_ERR;
        }
    }
    return JNI_VERSION_1_6;
}

INSTRUMENT_C_EXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    if (runtime::is_current_process_game()) {
        runtime::shutdown_instrumentation();
    }
}

// Standalone Floating Mod Menu APK JNI Bridge (IPC Client -> Headless Zygisk Server)
INSTRUMENT_C_EXPORT void JNICALL
Java_com_dbdm_modmenu_FloatingModService_nativeSyncConfig(
    JNIEnv*  env,
    jclass   /*clazz*/,
    jboolean hud_open,
    jboolean draw_markers,
    jfloat   max_dist,
    jstring  label_filter,
    jboolean override_name,
    jstring  player_name,
    jboolean apply_kinematics,
    jfloat   walk_speed,
    jfloat   max_accel,
    jfloat   jump_vel,
    jfloat   time_scale) {

    const char* filter_cstr = label_filter ? env->GetStringUTFChars(label_filter, nullptr) : "";
    const char* name_cstr   = player_name  ? env->GetStringUTFChars(player_name, nullptr)  : "";

    ipc::ControlPayload ctrl{};
    ctrl.magic                    = ipc::kIpcMagicVersion;
    ctrl.packet_type              = static_cast<std::uint32_t>(ipc::PacketType::PushControl);
    ctrl.hud_open                 = (hud_open == JNI_TRUE) ? 1U : 0U;
    ctrl.draw_2d_node_markers     = (draw_markers == JNI_TRUE) ? 1U : 0U;
    ctrl.apply_custom_player_name = (override_name == JNI_TRUE) ? 1U : 0U;
    ctrl.apply_custom_kinematics  = (apply_kinematics == JNI_TRUE) ? 1U : 0U;
    ctrl.max_render_distance_m    = static_cast<float>(max_dist);
    ctrl.walk_speed               = static_cast<float>(walk_speed);
    ctrl.max_acceleration         = static_cast<float>(max_accel);
    ctrl.jump_velocity            = static_cast<float>(jump_vel);
    ctrl.time_scale               = static_cast<float>(time_scale);

    if (filter_cstr) {
        std::strncpy(ctrl.label_filter, filter_cstr, hud::kMaxLabelLen - 1);
        ctrl.label_filter[hud::kMaxLabelLen - 1] = '\0';
    }
    if (name_cstr) {
        std::strncpy(ctrl.custom_player_name, name_cstr, hud::kMaxLabelLen - 1);
        ctrl.custom_player_name[hud::kMaxLabelLen - 1] = '\0';
    }

    // 1. Update local state + send over Abstract UNIX Domain Socket IPC to com.netease.dbdena
    ipc::apply_control_to_telemetry(ctrl);

    bool          gp_fov_on  = false;
    float         gp_fov     = 90.0f;
    bool          gp_skill   = false;
    bool          gp_trap    = false;
    bool          gp_fly     = false;
    bool          gp_esc     = false;
    unsigned int  gp_catmask = 0xFFU;
    hud::ImmobilizeState gp_imm_state = hud::ImmobilizeState::None;
    bool          gp_imm_on  = false;
    hud::PlayerRole gp_role_val = hud::PlayerRole::None;
    bool          gp_role_on = false;

    const bool is_gp_filter = (filter_cstr && std::strncmp(filter_cstr, "@gp:", 4) == 0);
    {
        hud::SceneTelemetryData& local_cfg = hud::get_telemetry_data();
        pthread_mutex_lock(&local_cfg.mutex);
        hud::GameplayManipulationConfig& gp_cfg = hud::get_gameplay_config();
        if (is_gp_filter) {
            (void)hud::parse_gameplay_ipc_string(filter_cstr, local_cfg, gp_cfg);
        }
        gp_fov_on    = gp_cfg.apply_fov_override;
        gp_fov       = gp_cfg.target_fov;
        gp_skill     = gp_cfg.auto_skillcheck;
        gp_trap      = gp_cfg.trap_immunity;
        gp_fly       = gp_cfg.fly_cheat_enabled;
        gp_esc       = gp_cfg.force_escape;
        gp_imm_on    = gp_cfg.apply_immobilize_state;
        gp_imm_state = gp_cfg.target_immobilize_state;
        gp_role_on   = gp_cfg.apply_player_role;
        gp_role_val  = gp_cfg.target_player_role;

        gp_catmask = 0U;
        if (local_cfg.show_antagonist)    gp_catmask |= 0x01U;
        if (local_cfg.show_teammates)     gp_catmask |= 0x02U;
        if (local_cfg.show_power_nodes)   gp_catmask |= 0x04U;
        if (local_cfg.show_shrines)       gp_catmask |= 0x08U;
        if (local_cfg.show_containers)    gp_catmask |= 0x10U;
        if (local_cfg.show_barriers)      gp_catmask |= 0x20U;
        if (local_cfg.show_portals)       gp_catmask |= 0x40U;
        if (local_cfg.show_special_props) gp_catmask |= 0x80U;
        pthread_mutex_unlock(&local_cfg.mutex);
    }

    (void)ipc::client_exchange_packet(&ctrl, nullptr);

    // 2. Also write fallback config file if permissible
    static constexpr const char* kCfgPath = "/data/local/tmp/dbdm_mod_config.cfg";
    if (std::FILE* fp = std::fopen(kCfgPath, "w")) {
        const char* imm_str = "off";
        if (gp_imm_on) {
            switch (gp_imm_state) {
                case hud::ImmobilizeState::None:    imm_str = "none";    break;
                case hud::ImmobilizeState::Hooked:  imm_str = "hooked";  break;
                case hud::ImmobilizeState::Trapped: imm_str = "trapped"; break;
                case hud::ImmobilizeState::Hiding:  imm_str = "hiding";  break;
                case hud::ImmobilizeState::Dead:    imm_str = "dead";    break;
                case hud::ImmobilizeState::Caged:   imm_str = "caged";   break;
            }
        }
        const char* role_str = "none";
        if (gp_role_on) {
            switch (gp_role_val) {
                case hud::PlayerRole::Camper:   role_str = "camper";   break;
                case hud::PlayerRole::Slasher:  role_str = "slasher";  break;
                case hud::PlayerRole::Observer: role_str = "observer"; break;
                default:                        role_str = "none";     break;
            }
        }
        std::fprintf(
            fp,
            "hud_open=%d\n"
            "draw_markers=%d\n"
            "max_dist=%.1f\n"
            "label_filter=%s\n"
            "gp_payload=%s\n"
            "override_name=%d\n"
            "player_name=%s\n"
            "apply_kinematics=%d\n"
            "walk_speed=%.1f\n"
            "max_accel=%.1f\n"
            "jump_vel=%.1f\n"
            "time_scale=%.2f\n"
            "apply_fov=%d\n"
            "fov=%.1f\n"
            "auto_skillcheck=%d\n"
            "trap_immunity=%d\n"
            "fly_cheat=%d\n"
            "force_escape=%d\n"
            "set_immobilize=%s\n"
            "set_player_role=%s\n"
            "cat_mask=%02X\n",
            hud_open ? 1 : 0,
            draw_markers ? 1 : 0,
            static_cast<double>(max_dist),
            (!is_gp_filter && filter_cstr) ? filter_cstr : "",
            (is_gp_filter && filter_cstr) ? filter_cstr : "",
            override_name ? 1 : 0,
            name_cstr ? name_cstr : "",
            apply_kinematics ? 1 : 0,
            static_cast<double>(walk_speed),
            static_cast<double>(max_accel),
            static_cast<double>(jump_vel),
            static_cast<double>(time_scale),
            gp_fov_on ? 1 : 0,
            static_cast<double>(gp_fov),
            gp_skill ? 1 : 0,
            gp_trap ? 1 : 0,
            gp_fly ? 1 : 0,
            gp_esc ? 1 : 0,
            imm_str,
            role_str,
            gp_catmask);
        std::fclose(fp);
        ::chmod(kCfgPath, 0666);
    }

    if (label_filter && filter_cstr) {
        env->ReleaseStringUTFChars(label_filter, filter_cstr);
    }
    if (player_name && name_cstr) {
        env->ReleaseStringUTFChars(player_name, name_cstr);
    }
}

INSTRUMENT_C_EXPORT jstring JNICALL
Java_com_dbdm_modmenu_FloatingModService_nativeGetStatus(JNIEnv* env, jclass /*clazz*/) {
    ipc::TelemetrySnapshotPayload snap{};
    char buf[256]{};
    if (ipc::client_exchange_packet(nullptr, &snap)) {
        std::snprintf(
            buf,
            sizeof(buf),
            "IPC: ONLINE | %s | Nodes: %u | Gens: %u (%.0f%%) | Player: %s",
            snap.match_active ? "IN MATCH (ACTIVE)" : "LOBBY / STANDBY",
            static_cast<unsigned>(snap.node_count),
            static_cast<unsigned>(snap.active_generators),
            static_cast<double>(snap.nearest_gen_progress),
            snap.active_player_name[0] != '\0' ? snap.active_player_name : "(In-Game Default)");
    } else {
        std::snprintf(buf, sizeof(buf), "IPC: WAITING FOR GAME (com.netease.dbdena)");
    }
    return env->NewStringUTF(buf);
}

// Polls the headless Zygisk backend via IPC, projects live 3D entities into 2D screen space,
// and writes [screen_x, screen_y, distance_m, category_id, extra] (5 floats per entity) into out_packed.
// Category IDs: 1 = Killer, 2 = Survivor, 3 = Generator, 4 = Portal, 5 = Chest, 6 = Totem, 7 = Pallet, 9 = SpecialProp.
INSTRUMENT_C_EXPORT jint JNICALL
Java_com_dbdm_modmenu_FloatingModService_nativePollOverlayMarkers(
    JNIEnv*     env,
    jclass      /*clazz*/,
    jfloat      screen_width,
    jfloat      screen_height,
    jfloatArray out_packed) {

    constexpr int kStride = 5;

    if (!out_packed || screen_width <= 1.0f || screen_height <= 1.0f) {
        return 0;
    }

    const jsize arr_len = env->GetArrayLength(out_packed);
    if (arr_len < kStride) {
        return 0;
    }

    ipc::TelemetrySnapshotPayload snap{};
    if (!ipc::client_exchange_packet(nullptr, &snap) || snap.match_active == 0 || snap.node_count == 0) {
        return 0;
    }

    bool  draw_markers       = true;
    float max_dist_m         = 250.0f;
    bool  show_antagonist    = true;
    bool  show_teammates     = true;
    bool  show_power_nodes   = true;
    bool  show_shrines       = true;
    bool  show_containers    = true;
    bool  show_barriers      = true;
    bool  show_portals       = true;
    bool  show_special_props = true;
    char  filter_buf[hud::kMaxLabelLen]{};
    {
        hud::SceneTelemetryData& local_cfg = hud::get_telemetry_data();
        pthread_mutex_lock(&local_cfg.mutex);
        draw_markers       = local_cfg.draw_2d_node_markers;
        max_dist_m         = local_cfg.max_render_distance_m;
        show_antagonist    = local_cfg.show_antagonist;
        show_teammates     = local_cfg.show_teammates;
        show_power_nodes   = local_cfg.show_power_nodes;
        show_shrines       = local_cfg.show_shrines;
        show_containers    = local_cfg.show_containers;
        show_barriers      = local_cfg.show_barriers;
        show_portals       = local_cfg.show_portals;
        show_special_props = local_cfg.show_special_props;
        std::memcpy(filter_buf, local_cfg.label_filter, hud::kMaxLabelLen);
        filter_buf[hud::kMaxLabelLen - 1] = '\0';
        pthread_mutex_unlock(&local_cfg.mutex);
    }

    if (!draw_markers) {
        return 0;
    }

    hud::CameraView cam{};
    cam.location = {snap.cam_x, snap.cam_y, snap.cam_z};
    cam.rotation = {snap.cam_pitch, snap.cam_yaw, snap.cam_roll};
    cam.fov      = snap.cam_fov;

    const std::size_t max_out_nodes = std::min<std::size_t>(
        hud::kMaxSceneNodes,
        static_cast<std::size_t>(arr_len / kStride));
    const std::size_t wn_count   = std::min<std::size_t>(snap.node_count, hud::kMaxSceneNodes);
    const std::size_t filter_len = hud::detail_ipc::safe_len(filter_buf);

    float packed[hud::kMaxSceneNodes * 5]{};
    jint  out_count = 0;

    for (std::size_t i = 0; i < wn_count && static_cast<std::size_t>(out_count) < max_out_nodes; ++i) {
        const auto& wn = snap.nodes[i];
        if (!std::isfinite(wn.x) || !std::isfinite(wn.distance_m) || wn.distance_m > max_dist_m) {
            continue;
        }

        float cat_id = 0.0f;
        switch (hud::adapter::classify_node_category(wn.label)) {
            case hud::NodeCategory::Antagonist:  if (!show_antagonist)    continue; cat_id = 1.0f; break;
            case hud::NodeCategory::Teammate:    if (!show_teammates)     continue; cat_id = 2.0f; break;
            case hud::NodeCategory::PowerNode:   if (!show_power_nodes)   continue;
                                                 if (hud::adapter::should_hide_generator(wn.progress_pct)) continue;
                                                 cat_id = 3.0f; break;
            case hud::NodeCategory::Portal:      if (!show_portals)       continue; cat_id = 4.0f; break;
            case hud::NodeCategory::Container:   if (!show_containers)    continue; cat_id = 5.0f; break;
            case hud::NodeCategory::Shrine:      if (!show_shrines)       continue;
                                                 if (!hud::adapter::is_totem_active(wn.progress_pct, wn.label)) continue;
                                                 cat_id = 6.0f; break;
            case hud::NodeCategory::Barrier:     if (!show_barriers)      continue; cat_id = 7.0f; break;
            case hud::NodeCategory::ExitGate:    if (!show_portals)       continue; cat_id = 8.0f; break;
            case hud::NodeCategory::SpecialProp: if (!show_special_props) continue; cat_id = 9.0f; break;
            case hud::NodeCategory::Unknown:
            default: continue;
        }
        if (wn.label[0] == '\0' ||
            std::strcmp(wn.label, "Unknown") == 0 ||
            hud::adapter::detail::is_ignored_auxiliary_actor(wn.label)) {
            continue;
        }
        if (filter_len > 0 && !hud::detail_ipc::icontains_str(wn.label, filter_buf)) {
            continue;
        }

        float sx = 0.0f;
        float sy = 0.0f;
        if (!runtime::project_world_to_overlay_screen(
                hud::Vec3{wn.x, wn.y, wn.z},
                cam,
                static_cast<float>(screen_width),
                static_cast<float>(screen_height),
                sx,
                sy)) {
            continue;
        }

        float foot_sx = sx;
        float foot_sy = sy;
        float head_sx = sx;
        float head_sy = sy;
        const bool has_foot = runtime::project_world_to_overlay_screen(
            hud::Vec3{wn.x, wn.y, wn.z - hud::kCapsuleHalfHeightCm},
            cam,
            static_cast<float>(screen_width),
            static_cast<float>(screen_height),
            foot_sx,
            foot_sy,
            0.0f);
        const bool has_head = runtime::project_world_to_overlay_screen(
            hud::Vec3{wn.x, wn.y, wn.z + hud::kCapsuleHalfHeightCm},
            cam,
            static_cast<float>(screen_width),
            static_cast<float>(screen_height),
            head_sx,
            head_sy,
            0.0f);
        if (has_foot) {
            sx = foot_sx;
            sy = foot_sy;
        }
        const float proj_height = (has_foot && has_head)
            ? std::clamp(std::abs(foot_sy - head_sy), 28.0f, 450.0f)
            : std::clamp((static_cast<float>(screen_width) * 0.45f) / std::max(1.0f, wn.distance_m), 28.0f, 450.0f);

        float* base = packed + (static_cast<std::size_t>(out_count) * static_cast<std::size_t>(kStride));
        base[0] = sx;
        base[1] = sy;
        base[2] = wn.distance_m;
        base[3] = cat_id;
        base[4] = (cat_id == 3.0f) ? std::clamp(wn.progress_pct, 0.0f, 100.0f) : proj_height;
        ++out_count;
    }

    if (out_count > 0) {
        env->SetFloatArrayRegion(out_packed, 0, static_cast<jsize>(out_count * kStride), packed);
        static std::uint32_t s_overlay_log_tick = 0;
        if ((++s_overlay_log_tick % 120U) == 1U) {
            DIAG_LOGI(
                "[OVERLAY-RENDER] Projected %d / %u 2D markers onto %.0fx%.0f canvas | FirstMarker=(%.1f, %.1f) dist=%.1fm cat=%.0f extra=%.1f",
                static_cast<int>(out_count),
                static_cast<unsigned>(snap.node_count),
                static_cast<double>(screen_width),
                static_cast<double>(screen_height),
                static_cast<double>(packed[0]),
                static_cast<double>(packed[1]),
                static_cast<double>(packed[2]),
                static_cast<double>(packed[3]),
                static_cast<double>(packed[4])
            );
        }
    }
    return out_count;
}

#if defined(DBDM_STANDALONE_DAEMON)
namespace runtime {

INSTRUMENT_LOCAL static pid_t find_game_process_pid() noexcept {
    DIR* dir = ::opendir("/proc");
    if (!dir) {
        return 0;
    }
    pid_t found_pid = 0;
    while (struct dirent* ent = ::readdir(dir)) {
        const char* dname = ent->d_name;
        if (dname[0] < '1' || dname[0] > '9') {
            continue;
        }
        const int pid_val = std::atoi(dname);
        if (pid_val <= 0 || pid_val == ::getpid()) {
            continue;
        }
        char cmd_path[64]{};
        std::snprintf(cmd_path, sizeof(cmd_path), "/proc/%d/cmdline", pid_val);
        std::FILE* fp = std::fopen(cmd_path, "rb");
        if (!fp) {
            continue;
        }
        char cmdline[128]{};
        const std::size_t n = std::fread(cmdline, 1, sizeof(cmdline) - 1, fp);
        std::fclose(fp);
        cmdline[n] = '\0';
        if (std::strstr(cmdline, "com.netease.dbdena") != nullptr &&
            std::strstr(cmdline, ":PushService") == nullptr) {
            found_pid = static_cast<pid_t>(pid_val);
            break;
        }
    }
    ::closedir(dir);
    return found_pid;
}

INSTRUMENT_LOCAL static int run_standalone_daemon() noexcept {
    DIAG_LOGI("Standalone Headless Telemetry Daemon started (PID=%d); monitoring com.netease.dbdena...",
              static_cast<int>(::getpid()));

    g_state.running.store(true, std::memory_order_release);

    pthread_t ipc_tid{};
    if (pthread_create(&ipc_tid, nullptr, ipc_server_worker_entry, nullptr) == 0) {
        pthread_detach(ipc_tid);
    }

    while (g_state.running.load(std::memory_order_acquire)) {
        const pid_t game_pid = find_game_process_pid();
        if (game_pid <= 0) {
            InProcessMemory::set_target_pid(0);
            g_state.ue4_module = {};
            g_state.last_logged_uworld = 0;
            g_state.last_logged_ulevel = 0;
            g_state.gnamepool_verified = false;
            sleep_ms(500);
            continue;
        }

        if (InProcessMemory::effective_pid() != game_pid || g_state.ue4_module.base_address == 0) {
            InProcessMemory::set_target_pid(game_pid);
            const auto mod = elf::find_module("libUE4.so");
            if (!mod.has_value() || mod->base_address == 0) {
                sleep_ms(250);
                continue;
            }
            std::array<uint8_t, sizeof(Elf64_Ehdr)> hdr{};
            if (InProcessMemory::safe_read(mod->base_address, hdr) != Status::Ok) {
                sleep_ms(250);
                continue;
            }
            g_state.ue4_module = *mod;
            g_state.last_logged_uworld = 0;
            g_state.last_logged_ulevel = 0;
            g_state.gnamepool_verified = false;
            DIAG_LOGI("Attached to com.netease.dbdena (PID=%d) via process_vm_readv/writev", static_cast<int>(game_pid));
            log_resolved_engine_anchors();
        }

        if (::kill(game_pid, 0) != 0) {
            InProcessMemory::set_target_pid(0);
            g_state.ue4_module = {};
            continue;
        }

        execute_diagnostic_tick();
        sleep_ms(16);
    }
    return 0;
}

} // namespace runtime

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::strcmp(argv[i], "--daemon") == 0) {
            (void)::daemon(0, 0);
            break;
        }
    }
    return runtime::run_standalone_daemon();
}
#endif
