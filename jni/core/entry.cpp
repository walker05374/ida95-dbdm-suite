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
#include "native_outline_controller.hpp"
#include "diagnostic_tick_bridge.hpp"
#include "crash_logger.hpp"
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
    uintptr_t world_game_state          = 0x03B0;     // AGameStateBase* UWorld::GameState (+0x3B0)
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
    uintptr_t custom_time_dilation      = 0x00A0;     // float AActor::CustomTimeDilation (FClassParams @ 0x0F23EEA0, FPropertyParams @ 0x0F23DC70)
    uintptr_t root_component            = 0x0140;     // AActor::RootComponent (FClassParams @ 0x0C7ABD84)
    uintptr_t skeletal_mesh             = 0x0290;     // USkeletalMeshComponent* ACharacter::Mesh (FClassParams @ 0x0F28ACE8, FPropertyParams @ 0x0F28AE78)
    uintptr_t character_movement        = 0x0298;     // UCharacterMovementComponent* ACharacter::CharacterMovement (FClassParams @ 0x0F28ACE8, FPropertyParams @ 0x0F28AED0)

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
    uintptr_t component_to_world        = 0x01A0;     // FTransform USceneComponent::ComponentToWorld (+0x01A0; FTransform::Translation at +0x01B0)

    // FUObjectArray / FChunkedFixedUObjectArray (for live lifecycle / PendingKill verification)
    uintptr_t guobject_objects_offset   = 0x0010;     // ObjObjects.Objects (FUObjectItem**)
    uintptr_t guobject_max_elems_offset = 0x0020;     // ObjObjects.MaxElements (int32)
    uintptr_t guobject_num_elems_offset = 0x0024;     // ObjObjects.NumElements (int32)
    uintptr_t guobject_num_chunks_offset= 0x002C;     // ObjObjects.NumChunks (int32)

    // UCharacterMovementComponent
    uintptr_t movement_velocity         = 0x00CC;     // FVector UMovementComponent::Velocity (FPropertyParams @ 0x0F36F3A8)
    uintptr_t gravity_scale             = 0x0160;     // float UCharacterMovementComponent::GravityScale (FPropertyParams @ 0x0F28E720)
    uintptr_t jump_z_velocity           = 0x0168;     // float UCharacterMovementComponent::JumpZVelocity (FPropertyParams @ 0x0F28E770)
    uintptr_t movement_mode             = 0x0178;     // uint8 EMovementMode UCharacterMovementComponent::MovementMode (Walking=1, Flying=5)
    uintptr_t max_walk_speed            = 0x019C;     // float UCharacterMovementComponent::MaxWalkSpeed
    uintptr_t max_walk_speed_crouched   = 0x01A0;     // float UCharacterMovementComponent::MaxWalkSpeedCrouched
    uintptr_t max_fly_speed             = 0x01A8;     // float UCharacterMovementComponent::MaxFlySpeed
    uintptr_t max_custom_movement_speed = 0x01AC;     // float UCharacterMovementComponent::MaxCustomMovementSpeed
    uintptr_t max_acceleration          = 0x01B0;     // float UCharacterMovementComponent::MaxAcceleration
    uintptr_t braking_decel_flying      = 0x01D0;     // float UCharacterMovementComponent::BrakingDecelerationFlying
    uintptr_t current_floor             = 0x0300;     // FFindFloorResult UCharacterMovementComponent::CurrentFloor (+0x0300; bitfield bBlockingHit/bWalkableFloor at +0x0300)
    uintptr_t movement_dirty_flags      = 0x039F;     // uint8 UCharacterMovementComponent bitfield (bCheatFlying/bJustTeleported/bNetworkUpdateReceived)

    // APlayerCameraManager FOV & AActor Collision / NetRole
    uintptr_t actor_collision_flags     = 0x0058;     // uint8 bitfield (bActorEnableCollision)
    uintptr_t actor_local_role          = 0x00FC;     // uint8 ENetRole AActor::Role (+0x00FC: 3 = ROLE_Authority)
    uintptr_t camera_default_fov        = 0x0248;     // float APlayerCameraManager::DefaultFOV (+0x248)
    uintptr_t camera_locked_fov         = 0x0250;     // float APlayerCameraManager::LockedFOV (+0x250)

    // AGenerator & UChargeableComponent / FSpeedBasedNetSyncedValue & USkillCheck (Verified in libUE4.so Build 292949)
    uintptr_t chargeable_current_charge = 0x0378;     // float AGenerator::NativePercentComplete (+0x378, 0.0f..1.0f)
    uintptr_t generator_activated       = 0x0368;     // uint8 AGenerator::Activated (+0x368)
    uintptr_t generator_is_repaired     = 0x0369;     // uint8 AGenerator::IsRepaired (+0x369)
    uintptr_t generator_native_percent  = 0x0378;     // float AGenerator::NativePercentComplete (+0x378, 0.0f..1.0f)
    uintptr_t generator_charge_comp     = 0x0520;     // UChargeableComponent* AGenerator::_generatorCharge (+0x520)
    uintptr_t chargeable_replicated_chg = 0x01B8;     // float UChargeableComponent::_currentCharge._replicatedValue (+0x1A0 + 0x18)
    uintptr_t chargeable_live_charge    = 0x01BC;     // float UChargeableComponent::_currentCharge live interpolated value (+0x1A0 + 0x1C)
    uintptr_t chargeable_tunable_data   = 0x02F8;     // TMap<FName, float> UChargeableComponent::_tunableValueMap.Data (+0x2F8)
    uintptr_t chargeable_tunable_count  = 0x0300;     // int32 UChargeableComponent::_tunableValueMap.ArrayNum (+0x300)
    uintptr_t chargeable_tunable_val    = 0x000C;     // float first tunable entry value at Data + 0x0C (MaxCharge ~ 90.0f)
    uintptr_t player_interaction_handler= 0x0A90;     // UPlayerInteractionHandler* ADBDPlayer::_interactionHandler (+0x0A90)
    uintptr_t handler_requested_inter   = 0x01F0;     // UInteractionDefinition* UPlayerInteractionHandler::_requestedInteraction (+0x01F0)
    uintptr_t handler_current_inter     = 0x0200;     // UInteractionDefinition* UPlayerInteractionHandler::_currentInteraction (+0x0200)
    uintptr_t interaction_id_fstr       = 0x0240;     // FString UInteractionDefinition::InteractionID (+0x0240)
    uintptr_t interaction_atlanta_id    = 0x0258;     // FName UInteractionDefinition::AtlantaInteractionID (+0x0258)
    uintptr_t interaction_anim          = 0x026D;     // uint8 EInteractionAnimation UInteractionDefinition::InteractionAnimation (+0x026D)
    uintptr_t skillcheck_component      = 0x0290;     // USkillCheck* UPlayerInteractionHandler::_skillCheck (+0x0290)
    uintptr_t skillcheck_activated_flag = 0x028C;     // uint8 bool USkillCheck::_activated (+0x028C)
    uintptr_t skillcheck_displayed_flag = 0x028D;     // uint8 bool USkillCheck::_displayed (+0x028D)
    uintptr_t skillcheck_current_prog   = 0x0290;     // float USkillCheck::_currentProgress (+0x0290)
    uintptr_t skillcheck_ui_prog        = 0x0294;     // float USkillCheck::_currentUIProgress (+0x0294)
    uintptr_t skillcheck_gen_count      = 0x02A0;     // int32 USkillCheck::_skillCheckCount (+0x02A0)
    uintptr_t skillcheck_owner_pawn     = 0x02B8;     // ADBDPlayer* USkillCheck::_owner (+0x02B8)
    uintptr_t skillcheck_current_inter  = 0x02C0;     // UChargeableInteractionDefinition* USkillCheck::_currentInteraction (+0x02C0)
    uintptr_t skillcheck_live_succ_start= 0x02CC;     // float live SuccessZoneStart (+0x02CC)
    uintptr_t skillcheck_live_succ_end  = 0x02D0;     // float live SuccessZoneEnd (+0x02D0)
    uintptr_t skillcheck_live_bonus_len = 0x02D4;     // float live BonusZoneLength (+0x02D4)
    uintptr_t skillcheck_live_bonus_start=0x02D8;     // float live BonusZoneStart (+0x02D8)
    uintptr_t skillcheck_live_displayed = 0x02F4;     // uint8 live bIsDisplayed (+0x02F4)
    uintptr_t skillcheck_success_start  = 0x02F8;     // float FSkillCheckDefinition::SuccessZoneStart (+0x2F8 + 0x00)
    uintptr_t skillcheck_success_length = 0x02FC;     // float FSkillCheckDefinition::SuccessZoneEnd   (+0x2F8 + 0x04)
    uintptr_t skillcheck_bonus_length   = 0x0300;     // float FSkillCheckDefinition::BonusZoneLength  (+0x2F8 + 0x08)
    uintptr_t skillcheck_bonus_start    = 0x0304;     // float FSkillCheckDefinition::BonusZoneStart   (+0x2F8 + 0x0C)
    uintptr_t skillcheck_last_result    = 0x0404;     // int32 ESkillCheckResult USkillCheck::_lastSkillCheckResult (+0x404)
    uintptr_t rva_skillcheck_activate   = 0x06E18208; // USkillCheck::Local_ActivateSkillCheck
    uintptr_t rva_skillcheck_input      = 0x06E18CF0; // USkillCheck::OnSkillCheckInput
    uintptr_t rva_skillcheck_res_branch = 0x06E18F1C; // USkillCheck::OnSkillCheckInputImpl result branch
    uintptr_t rva_skillcheck_res_gate   = 0x06E1903C; // USkillCheck::OnSkillCheckInputImpl zone decision gate
    uintptr_t rva_skillcheck_tick_gate  = 0x06E16B80; // USkillCheck::TickComponent auto-resolve gate
    uintptr_t camper_health_component   = 0x1838;     // UHealthComponent* ACamperPlayer::_healthComponent (+0x1838)
    uintptr_t health_in_bleedout        = 0x0170;     // uint8 bool UHealthComponent::_inInjuredBleedOut (+0x0170)
    uintptr_t health_state_count        = 0x01EC;     // int32 UHealthComponent::_currentHealthStateCount (+0x01EC)
    uintptr_t health_heal_dying_chg     = 0x0248;     // UChargeableComponent* UHealthComponent::_healFromDyingChargeable (+0x0248)
    uintptr_t health_heal_injured_chg   = 0x0250;     // UChargeableComponent* UHealthComponent::_healFromInjuredChargeable (+0x0250)
    uintptr_t rva_health_set_count      = 0x06F92394; // UHealthComponent::SetCurrentHealthStateCount

    // ADBDGameState, ATotem, APallet, ASearchable, AMeatHook Live Match State (Verified in libUE4.so Build 292949)
    uintptr_t gamestate_activated_gens  = 0x0628;     // int32 ADBDGameState::_activatedGeneratorCount (+0x628)
    uintptr_t gamestate_required_gens   = 0x062C;     // int32 ADBDGameState::_requiredActivatedGeneratorCount (+0x62C)
    uintptr_t totem_state               = 0x03A8;     // uint8 ATotem::_totemState (+0x3A8: 0=Cleansed, 1=Dull, 2=Hex, 3=Boon)
    uintptr_t pallet_state              = 0x0368;     // uint8 APallet::_state (+0x368: 0=Up, 1=Falling, 2=Fallen, 3=Destroyed, 4=Illusionary)
    uintptr_t searchable_searched       = 0x038C;     // uint8 ASearchable::_hasBeenSearched (+0x38C)
    uintptr_t meathook_is_sacrificed    = 0x04E2;     // uint8 AMeatHook::_isSacrificed (+0x4E2)
    uintptr_t meathook_is_sabotaged     = 0x04E4;     // uint8 AMeatHook::_isSabotaged (+0x4E4)

    // ACamperPlayer / ADBDPlayerState / AHatch / ABearTrap Gameplay State & Role
    uintptr_t dbd_player_state_machine  = 0x0A88;     // UStateMachine* ADBDPlayer::_SM (+0x0A88; +0x0AA8 is _playerData)
    uintptr_t state_machine_immobilize  = 0x01A0;     // uint8 EImmobilizedState (*(_SM + 0x01A0))
    uintptr_t rva_local_set_immobilized = 0x06F44FC0; // ACamperPlayer::Local_SetImmobilized
    uintptr_t camper_immobilize_state   = 0x1158;     // uint8 EImmobilizeState (+0x1158)
    uintptr_t camper_trap_immunity_flag = 0x115C;     // uint8 Trap Immunity / Hazard Bypass (+0x115C)
    uintptr_t camper_overlapping_escape = 0x1738;     // AActor* ACamperPlayer::_overlappingEscape (+0x1738)
    uintptr_t hatch_state               = 0x03A0;     // uint8 EHatchState AHatch::_hatchState (+0x03A0)
    uintptr_t hatch_is_force_open       = 0x03A3;     // uint8 bool AHatch::_isForceOpen (+0x03A3)
    uintptr_t rva_hatch_set_state       = 0x06DC9B00; // AHatch::Local_SetHatchState
    uintptr_t rva_camper_hatch_entered  = 0x06F400D8; // ACamperPlayer::HatchEscapeZoneEntered
    uintptr_t rva_camper_escape         = 0x06F3FABC; // ACamperPlayer::Escape
    uintptr_t player_name_private       = 0x0310;     // FString (TArray<char16_t>)
    uintptr_t player_state_game_role    = 0x039A;     // uint8 EPlayerRole (+0x39A: 1=Camper, 2=Slasher, 3=Observer)
    uintptr_t player_state_camper_state = 0x0498;     // uint8 EGameState ADBDPlayerState::PlayerData._playerGameState (+0x0440 + 0x0058 = +0x0498)
    uintptr_t player_state_escape_hatch = 0x0499;     // uint8 bool ADBDPlayerState::PlayerData._isEscapeHatch (+0x0440 + 0x0059 = +0x0499; +0x049C is _prestigeLevel)
    uintptr_t rva_process_event         = 0x0A74DF10; // UObject::ProcessEvent
    uintptr_t rva_ps_set_game_state     = 0x07845CC0; // ADBDPlayerState::SetPlayerGameState
    uintptr_t beartrap_contains_surv    = 0x02CF;     // uint8 bool ABearTrap::_containsSurvivor (+0x02CF)
    uintptr_t beartrap_is_trap_set      = 0x02D0;     // uint8 bool ABearTrap::_isTrapSet (+0x02D0)
    uintptr_t beartrap_was_disarmed     = 0x02D1;     // uint8 bool ABearTrap::_wasJustDisarmed (+0x02D1)

    // Client-Proxy State Reconciliation & Resolved UFunction RVAs (docs/offsets/offsets.json)
    uintptr_t state_tag_offset          = 0x1158;     // uint8 ACamperPlayer::_immobilizeState (+0x1158)
    uintptr_t velocity                  = 0x00CC;     // FVector UMovementComponent::Velocity (+0x00CC)
    uintptr_t interaction_handler       = 0x0A90;     // UPlayerInteractionHandler* ADBDPlayer::_interactionHandler (+0x0A90)
    uintptr_t timing_component          = 0x0290;     // USkillCheck* UPlayerInteractionHandler::_skillCheck (+0x0290)
    uintptr_t fn_timing_callback        = 0x06E18CF0; // USkillCheck::OnSkillCheckInput (@ 0x06E18CF0, void(*)(uintptr_t))
    uintptr_t fn_interaction_dispatch   = 0x06F400D8; // ACamperPlayer::HatchEscapeZoneEntered (@ 0x06F400D8)

    // UDBDOutlineComponent (StaticClass @ 0x077F1C04, SetTargetColor @ 0x0748A47C) & UOutlineUpdateStrategy Offsets
    uintptr_t rva_dbd_outline_static_cls= 0x077F1C04; // UDBDOutlineComponent::StaticClass()
    uintptr_t rva_outline_set_target_col= 0x0748A47C; // UDBDOutlineComponent::SetTargetColor(FLinearColor)
    uintptr_t actor_instance_components = 0x0200;     // TArray<UActorComponent*> AActor::InstanceComponents (+0x0200)
    uintptr_t actor_bp_created_comps    = 0x0210;     // TArray<UActorComponent*> AActor::BlueprintCreatedComponents (+0x0210)
    uintptr_t strategy_outline_comp_ptr = 0x00B8;     // UDBDOutlineComponent* UOutlineUpdateStrategy::_outlineComponent (+0x00B8)
    uintptr_t vaultable_can_be_revealed = 0x0138;     // uint8 bool UVaultableOutlineUpdateStrategy::_canBeRevealedToLocalPlayer (+0x0138)
    uintptr_t generator_force_reveal    = 0x036D;     // uint8 bool AGenerator::ForceRevealToLocalPlayer (+0x036D)
    uintptr_t outline_should_be_above   = 0x02B4;     // uint8 bool UDBDOutlineComponent::ShouldBeAboveOutlines (+0x02B4)
    uintptr_t outline_limit_custom_depth= 0x02B5;     // uint8 bool UDBDOutlineComponent::LimitToCustomDepthObjects (+0x02B5)
    uintptr_t outline_fade_closing_in   = 0x02B6;     // uint8 bool UDBDOutlineComponent::FadeOutAsClosingIn (+0x02B6)
    uintptr_t outline_is_always_visible = 0x02B7;     // uint8 bool UDBDOutlineComponent::IsAlwaysVisible (+0x02B7)
    uintptr_t outline_min_distance      = 0x02B8;     // float UDBDOutlineComponent::MinimumOutlineDistance (+0x02B8)
    uintptr_t outline_force_far_away    = 0x02BD;     // uint8 bool UDBDOutlineComponent::ForceOutlineFarAway (+0x02BD)
    uintptr_t outline_target_color_r    = 0x0314;     // float UDBDOutlineComponent::TargetColor.R (+0x0314)
    uintptr_t outline_target_color_g    = 0x0318;     // float UDBDOutlineComponent::TargetColor.G (+0x0318)
    uintptr_t outline_target_color_b    = 0x031C;     // float UDBDOutlineComponent::TargetColor.B (+0x031C)
    uintptr_t outline_target_color_a    = 0x0320;     // float UDBDOutlineComponent::TargetColor.A (+0x0320)
};

using Offsets = EngineOffsets;
using TimingCallbackFn = void(*)(uintptr_t);
using InteractionDispatchFn = void(*)(uintptr_t, uintptr_t);

INSTRUMENT_LOCAL inline Status sync_proxy_state_frame(
    uintptr_t actor_ptr,
    uint8_t desired_tag,
    const FVector& dir_unit,
    float dt,
    const Offsets& off
) noexcept {
    if (actor_ptr == 0) {
        return Status::InvalidPtr;
    }
    if (const Status st = mem::write<uint8_t>(actor_ptr + off.state_tag_offset, desired_tag); st != Status::Ok) {
        return st;
    }
    const auto root_ptr = mem::read<uintptr_t>(actor_ptr + off.root_component);
    if (!root_ptr.has_value() || *root_ptr == 0) {
        return Status::InvalidPtr;
    }
    const auto pos = mem::read<FVector>(*root_ptr + off.relative_location);
    if (!pos.has_value()) {
        return Status::InvalidPtr;
    }
    const float clamped_dt = std::clamp(dt, 0.001f, 0.050f);
    const FVector next_pos{
        pos->x + dir_unit.x * 820.0f * clamped_dt,
        pos->y + dir_unit.y * 820.0f * clamped_dt,
        pos->z
    };
    return mem::write<FVector>(*root_ptr + off.relative_location, next_pos);
}

INSTRUMENT_LOCAL inline Status clamp_kinematic_envelope(
    uintptr_t move_comp,
    float target_scalar,
    bool lock_vertical,
    float z_rate,
    const Offsets& off
) noexcept {
    if (move_comp == 0) {
        return Status::InvalidPtr;
    }
    const auto mode = mem::read<uint8_t>(move_comp + off.movement_mode);
    if (!mode.has_value()) {
        return Status::InvalidPtr;
    }
    if (*mode == 5U) {
        if (const Status st = mem::write<uint8_t>(move_comp + off.movement_mode, uint8_t(1)); st != Status::Ok) {
            return st;
        }
    }
    const float clamped_speed = std::clamp(target_scalar, 226.0f, 845.0f);
    if (const Status st = mem::write<float>(move_comp + off.max_walk_speed, clamped_speed); st != Status::Ok) {
        return st;
    }
    if (lock_vertical) {
        auto vel = mem::read<FVector>(move_comp + off.velocity);
        if (!vel.has_value()) {
            return Status::InvalidPtr;
        }
        vel->z = std::clamp(z_rate, -150.0f, 220.0f);
        if (const Status st = mem::write<FVector>(move_comp + off.velocity, *vel); st != Status::Ok) {
            return st;
        }
    }
    return Status::Ok;
}

INSTRUMENT_LOCAL inline Status evaluate_ui_timing_window(
    uintptr_t actor_ptr,
    uintptr_t module_base,
    const Offsets& off
) noexcept {
    if (actor_ptr == 0 || module_base == 0) {
        return Status::InvalidPtr;
    }
    const auto handler = mem::read<uintptr_t>(actor_ptr + off.interaction_handler);
    if (!handler.has_value() || *handler == 0) {
        return Status::InvalidPtr;
    }
    const auto widget_comp = mem::read<uintptr_t>(*handler + off.timing_component);
    if (!widget_comp.has_value() || *widget_comp == 0) {
        return Status::InvalidPtr;
    }
    const auto active = mem::read<bool>(*widget_comp + off.skillcheck_activated_flag);
    if (!active.has_value()) {
        return Status::InvalidPtr;
    }
    if (!*active) {
        return Status::Ok;
    }
    if (const Status st = mem::write<float>(*widget_comp + off.skillcheck_live_succ_start, 0.0f); st != Status::Ok) {
        return st;
    }
    if (const Status st = mem::write<float>(*widget_comp + off.skillcheck_live_succ_end, 1.0f); st != Status::Ok) {
        return st;
    }
    const auto cursor = mem::read<float>(*widget_comp + off.skillcheck_current_prog);
    if (!cursor.has_value()) {
        return Status::InvalidPtr;
    }
    if (*cursor >= 0.05f) {
#if !defined(DBDM_STANDALONE_DAEMON)
        if (mem::is_in_process()) {
            reinterpret_cast<TimingCallbackFn>(module_base + off.fn_timing_callback)(*widget_comp);
        }
#endif
        if (const Status st = mem::write<bool>(*widget_comp + off.skillcheck_activated_flag, false); st != Status::Ok) {
            return st;
        }
    }
    return Status::Ok;
}

INSTRUMENT_LOCAL inline Status align_and_dispatch_interaction(
    uintptr_t source_actor,
    uintptr_t target_actor,
    uintptr_t module_base,
    const Offsets& off
) noexcept {
    if (!source_actor || !target_actor || !module_base) {
        return Status::InvalidPtr;
    }
    const auto src_root = mem::read<uintptr_t>(source_actor + off.root_component);
    const auto dst_root = mem::read<uintptr_t>(target_actor + off.root_component);
    if (!src_root.has_value() || *src_root == 0 || !dst_root.has_value() || *dst_root == 0) {
        return Status::InvalidPtr;
    }
    auto dst_pos = mem::read<FVector>(*dst_root + off.relative_location);
    if (!dst_pos.has_value()) {
        return Status::InvalidPtr;
    }
    dst_pos->z += 15.0f;
    if (const Status st = mem::write<FVector>(*src_root + off.relative_location, *dst_pos); st != Status::Ok) {
        return st;
    }
    if (const Status st = mem::write<uint8_t>(target_actor + off.hatch_state, uint8_t(2)); st != Status::Ok) {
        return st;
    }
#if !defined(DBDM_STANDALONE_DAEMON)
    if (mem::is_in_process()) {
        reinterpret_cast<InteractionDispatchFn>(module_base + off.fn_interaction_dispatch)(target_actor, source_actor);
    }
#endif
    return Status::Ok;
}

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

        // Never overwrite Actor::CustomTimeDilation (+0x00AC) in online matches to prevent
        // USkeletalMeshComponent animation desync from UCapsuleComponent (sideways tilt / half-body sinking).
        (void)target_time_scale;
        if (std::isfinite(target_walk_speed) && target_walk_speed >= 50.0f && target_walk_speed <= 5000.0f) {
            const float safe_walk_speed = std::clamp(target_walk_speed, 50.0f, 810.0f);
            const Status st = mem::write_data<float>(move_comp + off.max_walk_speed, safe_walk_speed);
            if (st != Status::Ok) {
                return st;
            }
            (void)mem::write_data<float>(move_comp + off.max_walk_speed_crouched, safe_walk_speed);
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

    [[nodiscard]] static uint64_t skillcheck_now_ms() noexcept {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
               static_cast<uint64_t>(ts.tv_nsec / 1000000LL);
    }

    [[nodiscard]] static bool is_allowed_skillcheck_interaction(
        uintptr_t inter_ptr,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        const GNamePoolResolver& name_pool
    ) noexcept {
        if (inter_ptr == 0ULL || !validate_canonical_uobject(inter_ptr, 0x0330U, guobjectarray_abs, off)) {
            return false;
        }

        std::array<char, 64> inter_id_utf8{};
        if (const auto fstr = mem::read<TArrayHeader>(inter_ptr + off.interaction_id_fstr);
            fstr.has_value() && fstr->data_ptr != 0 && fstr->count > 1 && fstr->count <= 64) {
            std::array<char16_t, 64> u16_buf{};
            const size_t char_cnt = std::min<size_t>(static_cast<size_t>(fstr->count - 1), u16_buf.size() - 1U);
            std::span<uint8_t> raw_slice{reinterpret_cast<uint8_t*>(u16_buf.data()), char_cnt * sizeof(char16_t)};
            if (InProcessMemory::safe_read(fstr->data_ptr, raw_slice) == Status::Ok) {
                for (size_t i = 0; i < char_cnt; ++i) {
                    const char16_t ch = u16_buf[i];
                    inter_id_utf8[i] = (ch >= 0x20U && ch <= 0x7EU) ? static_cast<char>(ch) : '\0';
                }
                inter_id_utf8[char_cnt] = '\0';
            }
        }

        std::array<char, 96> obj_name{};
        if (const auto fn_idx = mem::read<uint32_t>(inter_ptr + off.fname_comparison_index);
            fn_idx.has_value() && *fn_idx != 0U) {
            (void)name_pool.resolve_name(*fn_idx, obj_name);
        }

        std::array<char, 96> atlanta_name{};
        if (const auto atl_idx = mem::read<uint32_t>(inter_ptr + off.interaction_atlanta_id);
            atl_idx.has_value() && *atl_idx != 0U) {
            (void)name_pool.resolve_name(*atl_idx, atlanta_name);
        }

        std::array<char, 96>  cls_leaf{};
        std::array<char, 224> cls_chain{};
        resolve_uclass_hierarchy(inter_ptr, name_pool, cls_leaf, cls_chain);

        auto matches_any = [&](const char* needle) noexcept -> bool {
            return hud::detail::icontains_str(inter_id_utf8.data(), needle) ||
                   hud::detail::icontains_str(obj_name.data(), needle) ||
                   hud::detail::icontains_str(atlanta_name.data(), needle) ||
                   hud::detail::icontains_str(cls_leaf.data(), needle) ||
                   hud::detail::icontains_str(cls_chain.data(), needle);
        };

        if (matches_any("Totem") || matches_any("Cleanse") || matches_any("Bless")) {
            return false;
        }

        const uint8_t anim = mem::read<uint8_t>(inter_ptr + off.interaction_anim).value_or(0xFFU);
        switch (anim) {
            case 1U:  // VE_Generator
            case 2U:  // VE_PullDownLeft (Pallet)
            case 3U:  // VE_PullDownRight (Pallet)
            case 4U:  // VE_Hiding (Locker)
            case 5U:  // VE_SearchCloset (Locker)
            case 6U:  // VE_HealingOther
            case 8U:  // VE_StruggleFree (Unhook)
            case 9U:  // VE_HealOther
            case 10U: // VE_HealSelf
            case 15U: // VE_BeingHooked
                return true;
            default:
                break;
        }

        if (matches_any("Generator") || matches_any("Repair") ||
            matches_any("Heal") || matches_any("Medkit") || matches_any("SelfCare") || matches_any("Mend") ||
            matches_any("Unhook") || matches_any("Struggle") || matches_any("FreeSelf") || matches_any("Hook") ||
            matches_any("Pallet") || matches_any("PullDown") || matches_any("Vault") ||
            matches_any("Locker") || matches_any("Closet") || matches_any("Hide")) {
            return true;
        }
        return false;
    }

    [[nodiscard]] static bool is_pallet_or_vault_interaction(
        uintptr_t inter_ptr,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        const GNamePoolResolver& name_pool
    ) noexcept {
        if (inter_ptr == 0ULL || !validate_canonical_uobject(inter_ptr, 0x0330U, guobjectarray_abs, off)) {
            return false;
        }
        const uint8_t anim = mem::read<uint8_t>(inter_ptr + off.interaction_anim).value_or(0xFFU);
        if (anim == 2U || anim == 3U) {
            return true;
        }
        std::array<char, 64> inter_id_utf8{};
        if (const auto fstr = mem::read<TArrayHeader>(inter_ptr + off.interaction_id_fstr);
            fstr.has_value() && fstr->data_ptr != 0 && fstr->count > 1 && fstr->count <= 64) {
            std::array<char16_t, 64> u16_buf{};
            const size_t char_cnt = std::min<size_t>(static_cast<size_t>(fstr->count - 1), u16_buf.size() - 1U);
            std::span<uint8_t> raw_slice{reinterpret_cast<uint8_t*>(u16_buf.data()), char_cnt * sizeof(char16_t)};
            if (InProcessMemory::safe_read(fstr->data_ptr, raw_slice) == Status::Ok) {
                for (size_t i = 0; i < char_cnt; ++i) {
                    const char16_t ch = u16_buf[i];
                    inter_id_utf8[i] = (ch >= 0x20U && ch <= 0x7EU) ? static_cast<char>(ch) : '\0';
                }
                inter_id_utf8[char_cnt] = '\0';
            }
        }
        std::array<char, 96> obj_name{};
        if (const auto fn_idx = mem::read<uint32_t>(inter_ptr + off.fname_comparison_index);
            fn_idx.has_value() && *fn_idx != 0U) {
            (void)name_pool.resolve_name(*fn_idx, obj_name);
        }
        std::array<char, 96> atlanta_name{};
        if (const auto atl_idx = mem::read<uint32_t>(inter_ptr + off.interaction_atlanta_id);
            atl_idx.has_value() && *atl_idx != 0U) {
            (void)name_pool.resolve_name(*atl_idx, atlanta_name);
        }
        std::array<char, 96>  cls_leaf{};
        std::array<char, 224> cls_chain{};
        resolve_uclass_hierarchy(inter_ptr, name_pool, cls_leaf, cls_chain);

        auto matches_any = [&](const char* needle) noexcept -> bool {
            return hud::detail::icontains_str(inter_id_utf8.data(), needle) ||
                   hud::detail::icontains_str(obj_name.data(), needle) ||
                   hud::detail::icontains_str(atlanta_name.data(), needle) ||
                   hud::detail::icontains_str(cls_leaf.data(), needle) ||
                   hud::detail::icontains_str(cls_chain.data(), needle);
        };
        return matches_any("Pallet") || matches_any("PullDown") || matches_any("Vault");
    }

    static void patch_skillcheck_great_branch(
        uintptr_t ue4_base,
        const EngineOffsets& off,
        bool enable_patch
    ) noexcept {
        if (ue4_base == 0 || InProcessMemory::effective_pid() != ::getpid()) {
            return;
        }
        static bool s_patched = false;
        if (enable_patch == s_patched) {
            return;
        }

        const uintptr_t addr_branch = ue4_base + off.rva_skillcheck_res_branch;
        const uintptr_t addr_gate   = ue4_base + off.rva_skillcheck_res_gate;
        const uintptr_t addr_tick   = ue4_base + off.rva_skillcheck_tick_gate;

        auto make_rw = [](uintptr_t addr, size_t len) noexcept -> bool {
            const uintptr_t page_sz    = static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE));
            const uintptr_t page_start = addr & ~(page_sz - 1ULL);
            const uintptr_t page_end   = (addr + len + page_sz - 1ULL) & ~(page_sz - 1ULL);
            return ::mprotect(reinterpret_cast<void*>(page_start), page_end - page_start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
        };
        auto make_rx = [](uintptr_t addr, size_t len) noexcept {
            const uintptr_t page_sz    = static_cast<uintptr_t>(::sysconf(_SC_PAGESIZE));
            const uintptr_t page_start = addr & ~(page_sz - 1ULL);
            const uintptr_t page_end   = (addr + len + page_sz - 1ULL) & ~(page_sz - 1ULL);
            (void)::mprotect(reinterpret_cast<void*>(page_start), page_end - page_start, PROT_READ | PROT_EXEC);
        };

        if (enable_patch) {
            if (make_rw(addr_branch, 0x130U)) {
                const uint32_t b_great_from_8f1c = 0x140000CBU;
                const uint32_t b_great_from_903c = 0x14000083U;
                std::memcpy(reinterpret_cast<void*>(addr_branch), &b_great_from_8f1c, sizeof(uint32_t));
                std::memcpy(reinterpret_cast<void*>(addr_gate),   &b_great_from_903c, sizeof(uint32_t));
                __builtin___clear_cache(reinterpret_cast<char*>(addr_branch), reinterpret_cast<char*>(addr_gate + 4U));
                make_rx(addr_branch, 0x130U);
            }
            if (make_rw(addr_tick, 16U)) {
                const uint32_t tick_patch[4] = {0xD503201FU, 0x2A1F03E4U, 0xAA1303E0U, 0x52800021U};
                std::memcpy(reinterpret_cast<void*>(addr_tick), tick_patch, sizeof(tick_patch));
                __builtin___clear_cache(reinterpret_cast<char*>(addr_tick), reinterpret_cast<char*>(addr_tick + 16U));
                make_rx(addr_tick, 16U);
            }
            s_patched = true;
        } else {
            if (make_rw(addr_branch, 0x130U)) {
                const uint32_t orig_8f1c = 0x394BD288U;
                const uint32_t orig_903c = 0x52000109U;
                std::memcpy(reinterpret_cast<void*>(addr_branch), &orig_8f1c, sizeof(uint32_t));
                std::memcpy(reinterpret_cast<void*>(addr_gate),   &orig_903c, sizeof(uint32_t));
                __builtin___clear_cache(reinterpret_cast<char*>(addr_branch), reinterpret_cast<char*>(addr_gate + 4U));
                make_rx(addr_branch, 0x130U);
            }
            if (make_rw(addr_tick, 16U)) {
                const uint32_t orig_tick[4] = {0x34000168U, 0x52800024U, 0xAA1303E0U, 0x2A1F03E1U};
                std::memcpy(reinterpret_cast<void*>(addr_tick), orig_tick, sizeof(orig_tick));
                __builtin___clear_cache(reinterpret_cast<char*>(addr_tick), reinterpret_cast<char*>(addr_tick + 16U));
                make_rx(addr_tick, 16U);
            }
            s_patched = false;
        }
    }

    static void apply_pallet_vault_heal(
        uintptr_t camper_pawn,
        uintptr_t ue4_base,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        bool in_process
    ) noexcept {
        if (!validate_canonical_uobject(camper_pawn, 0x1840U, guobjectarray_abs, off)) {
            return;
        }
        const auto hc = mem::read<uintptr_t>(camper_pawn + off.camper_health_component);
        if (!hc.has_value() || !validate_canonical_uobject(*hc, 0x0328U, guobjectarray_abs, off)) {
            return;
        }
        (void)mem::write_data<uint8_t>(*hc + off.health_in_bleedout, 0U);
        (void)mem::write_data<int32_t>(*hc + off.health_state_count, 2); // 2 = Healthy

        for (uintptr_t chg_off : {off.health_heal_dying_chg, off.health_heal_injured_chg}) {
            if (const auto chg = mem::read<uintptr_t>(*hc + chg_off);
                chg.has_value() && validate_canonical_uobject(*chg, 0x0310U, guobjectarray_abs, off)) {
                (void)mem::write_data<float>(*chg + off.chargeable_replicated_chg, 16.0f);
                (void)mem::write_data<float>(*chg + off.chargeable_live_charge,    16.0f);
            }
        }
        if (in_process && ue4_base != 0) {
            using SetCurrentHealthStateCountFn = void(*)(uintptr_t, int32_t, bool);
            const auto set_hp_fn = reinterpret_cast<SetCurrentHealthStateCountFn>(ue4_base + off.rva_health_set_count);
            set_hp_fn(*hc, 2, false);
        }
    }

    // 3c. Auto Heal (Aba Física): drives fast skill check path + completes heal on ACamperPlayer::_healthComponent (+0x1838)
    static void apply_auto_heal(
        uintptr_t local_pawn,
        uintptr_t local_pc,
        uintptr_t ue4_base,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!validate_canonical_uobject(local_pawn, 0x1840U, guobjectarray_abs, off)) {
            return;
        }
        const auto hc = mem::read<uintptr_t>(local_pawn + off.camper_health_component);
        if (!hc.has_value() || !validate_canonical_uobject(*hc, 0x0328U, guobjectarray_abs, off)) {
            return;
        }
        const int32_t hp_state = mem::read<int32_t>(*hc + off.health_state_count).value_or(2);
        const uint8_t bleedout = mem::read<uint8_t>(*hc + off.health_in_bleedout).value_or(0U);
        if (hp_state >= 2 && bleedout == 0U) {
            return;
        }
        const bool in_process = (InProcessMemory::effective_pid() == ::getpid()) && (ue4_base != 0);
        (void)apply_auto_skillcheck(local_pawn, local_pc, ue4_base, guobjectarray_abs, off, true, 150);
        apply_pallet_vault_heal(local_pawn, ue4_base, guobjectarray_abs, off, in_process);
    }

    // 3a. Auto Skillcheck (Chaining + Auto-Great)
    static bool apply_auto_skillcheck(
        uintptr_t local_pawn,
        uintptr_t local_pc,
        uintptr_t ue4_base,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        bool auto_skillcheck_enabled = true,
        int interval_ms = 650,
        float nearest_gen_dist_sq = 0.0f,
        float nearest_gen_pct = 50.0f
    ) noexcept {
#if defined(DBDM_STANDALONE_DAEMON)
        (void)local_pawn;
        (void)local_pc;
        (void)ue4_base;
        (void)guobjectarray_abs;
        (void)off;
        (void)auto_skillcheck_enabled;
        (void)interval_ms;
        (void)nearest_gen_dist_sq;
        (void)nearest_gen_pct;
        return false;
#else
        static bool    s_was_auto_skillcheck_enabled = false;
        static bool    s_fired_for_current_window    = false;
        static int32_t s_last_triggered_count        = -1;

        if (!auto_skillcheck_enabled && !s_was_auto_skillcheck_enabled) {
            return false;
        }

        const hud::CharacterMovementChain chain = resolve_local_movement_chain(
            local_pc, local_pawn, guobjectarray_abs, off);
        const uintptr_t expected_owner = (chain.acknowledged_pawn != 0) ? chain.acknowledged_pawn : local_pawn;
        uintptr_t ih_addr = 0ULL;
        uintptr_t sc_addr = 0ULL;
        if (expected_owner != 0 && validate_canonical_uobject(expected_owner, 0x0AA0U, guobjectarray_abs, off)) {
            if (const auto ih = mem::read<uintptr_t>(expected_owner + off.player_interaction_handler);
                ih.has_value() && validate_canonical_uobject(*ih, 0x02A0U, guobjectarray_abs, off)) {
                ih_addr = *ih;
                if (const auto sc = mem::read<uintptr_t>(ih_addr + off.skillcheck_component);
                    sc.has_value() && validate_canonical_uobject(*sc, 0x0420U, guobjectarray_abs, off)) {
                    sc_addr = *sc;
                }
            }
        }

        auto clear_stale_skillcheck_state = [&]() noexcept {
            if (sc_addr != 0ULL) {
                (void)mem::write_data<uint8_t>(sc_addr + off.skillcheck_activated_flag,  0U);
                (void)mem::write_data<uint8_t>(sc_addr + off.skillcheck_displayed_flag,  0U);
                (void)mem::write_data<uint8_t>(sc_addr + off.skillcheck_live_displayed,  0U);
                (void)mem::write_data<uintptr_t>(sc_addr + off.skillcheck_current_inter, 0ULL);
            }
            s_fired_for_current_window = false;
        };

        if (!auto_skillcheck_enabled) {
            patch_skillcheck_great_branch(ue4_base, off, false);
            if (s_was_auto_skillcheck_enabled) {
                clear_stale_skillcheck_state();
                s_was_auto_skillcheck_enabled = false;
            }
            return false;
        }
        s_was_auto_skillcheck_enabled = true;
        patch_skillcheck_great_branch(ue4_base, off, true);

        if (ih_addr == 0ULL || sc_addr == 0ULL) {
            return false;
        }

        float zone_start  = 0.0f;
        float zone_length = 1.0f;
        float bonus_len   = 1.0f;
        hud::compute_skillcheck_zone_override(zone_start, zone_length, bonus_len);

        const uint8_t act_flag  = mem::read<uint8_t>(sc_addr + off.skillcheck_activated_flag).value_or(0U);
        const uint8_t disp_flag = mem::read<uint8_t>(sc_addr + off.skillcheck_displayed_flag).value_or(0U);
        const uint8_t live_disp = mem::read<uint8_t>(sc_addr + off.skillcheck_live_displayed).value_or(0U);

        float speed_2d = 0.0f;
        if (chain.is_valid && chain.movement_component != 0) {
            if (const auto vel = mem::read<FVector>(chain.movement_component + off.movement_velocity)) {
                const float speed_sq_2d = (vel->x * vel->x) + (vel->y * vel->y);
                if (std::isfinite(speed_sq_2d) && speed_sq_2d >= 0.0f) {
                    speed_2d = std::sqrt(speed_sq_2d);
                }
            }
        }

        // Rely strictly on UPlayerInteractionHandler::_currentInteraction (+0x0200).
        // Never fall back to _requestedInteraction (+0x01F0) or USkillCheck::_currentInteraction (+0x02C0).
        const GNamePoolResolver name_pool{ue4_base != 0 ? (ue4_base + off.gnamepool_rva) : 0ULL};
        const uintptr_t cur_inter = mem::read<uintptr_t>(ih_addr + off.handler_current_inter).value_or(0ULL);
        if (!validate_canonical_uobject(cur_inter, 0x0100U, guobjectarray_abs, off)) {
            if (act_flag != 0U || disp_flag != 0U || live_disp != 0U) {
                clear_stale_skillcheck_state();
            }
            return false;
        }
        if (!is_allowed_skillcheck_interaction(cur_inter, guobjectarray_abs, off, name_pool)) {
            if (act_flag != 0U || disp_flag != 0U || live_disp != 0U) {
                clear_stale_skillcheck_state();
            }
            return false;
        }

        static uint64_t s_last_spawn_ms = 0ULL;
        const uint64_t  now_ms          = skillcheck_now_ms();
        const uint64_t  clamped_int_ms  = static_cast<uint64_t>(std::clamp(interval_ms, 150, 5000));
        const bool      in_process      = (InProcessMemory::effective_pid() == ::getpid()) && (ue4_base != 0);

        if (is_pallet_or_vault_interaction(cur_inter, guobjectarray_abs, off, name_pool)) {
            clear_stale_skillcheck_state();
            (void)mem::write_data<int32_t>(sc_addr + off.skillcheck_last_result, 1);
            if ((now_ms - s_last_spawn_ms) >= clamped_int_ms) {
                s_last_spawn_ms = now_ms;
                apply_pallet_vault_heal(expected_owner, ue4_base, guobjectarray_abs, off, in_process);
                return true;
            }
            return false;
        }

        // Verify the survivor is stationary (<= 25 cm/s horizontal speed); moving survivors cannot repair or heal.
        if (speed_2d > 25.0f) {
            if (act_flag != 0U || disp_flag != 0U || live_disp != 0U) {
                clear_stale_skillcheck_state();
            }
            return false;
        }

        bool is_generator_repair = false;
        auto is_chargeable_skillcheck_interaction = [&](uintptr_t inter_ptr) noexcept -> bool {
            if (inter_ptr == 0ULL || !validate_canonical_uobject(inter_ptr, 0x0330U, guobjectarray_abs, off)) {
                return false;
            }
            const uint8_t anim = mem::read<uint8_t>(inter_ptr + off.interaction_anim).value_or(0xFFU);
            std::array<char, 96>  cls_leaf{};
            std::array<char, 224> cls_chain{};
            resolve_uclass_hierarchy(inter_ptr, name_pool, cls_leaf, cls_chain);
            if (anim == 1U ||
                hud::detail::icontains_str(cls_leaf.data(), "Generator") ||
                hud::detail::icontains_str(cls_leaf.data(), "Repair")) {
                is_generator_repair = true;
            }
            if (anim == 1U || anim == 6U || anim == 9U || anim == 10U) {
                return true;
            }
            return hud::detail::icontains_str(cls_chain.data(), "ChargeableInteraction") ||
                   is_generator_repair ||
                   hud::detail::icontains_str(cls_leaf.data(),  "Heal") ||
                   hud::detail::icontains_str(cls_leaf.data(),  "Mend") ||
                   hud::detail::icontains_str(cls_leaf.data(),  "Sabotage");
        };

        const bool is_valid_chargeable = is_chargeable_skillcheck_interaction(cur_inter);
        if (!is_valid_chargeable) {
            if (act_flag != 0U || disp_flag != 0U || live_disp != 0U) {
                clear_stale_skillcheck_state();
            }
            return false;
        }

        // If repairing a generator, ensure the nearest generator is within interaction distance (<= 4.5m)
        // and is actively being repaired (0.0% <= progress < 99.9%).
        if (is_generator_repair) {
            if (!std::isfinite(nearest_gen_dist_sq) || nearest_gen_dist_sq > (4.5f * 4.5f) ||
                !std::isfinite(nearest_gen_pct) || nearest_gen_pct < 0.0f || nearest_gen_pct >= 99.9f) {
                if (act_flag != 0U || disp_flag != 0U || live_disp != 0U) {
                    clear_stale_skillcheck_state();
                }
                return false;
            }
        }

        // Strictly reactive: only respond when the game naturally opens a skill check window
        if (live_disp == 0U || act_flag == 0U) {
            s_fired_for_current_window = false;
            return false;
        }

        const int32_t gen_count = mem::read<int32_t>(sc_addr + off.skillcheck_gen_count).value_or(0);
        if (gen_count != s_last_triggered_count) {
            s_fired_for_current_window = false;
        }

        (void)mem::write_data<float>(sc_addr + off.skillcheck_live_succ_start,  zone_start);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_live_succ_end,    zone_length);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_live_bonus_len,   bonus_len);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_live_bonus_start, zone_start);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_success_start,    zone_start);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_success_length,   zone_length);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_bonus_length,     bonus_len);
        (void)mem::write_data<float>(sc_addr + off.skillcheck_bonus_start,      zone_start);

        const float cur_prog = mem::read<float>(sc_addr + off.skillcheck_current_prog).value_or(0.0f);
        const float ui_prog  = mem::read<float>(sc_addr + off.skillcheck_ui_prog).value_or(0.0f);
        const float eff_prog = std::max(cur_prog, ui_prog);
        const float target_threshold = std::max(0.10f, zone_start + 0.02f);

        if (!s_fired_for_current_window && std::isfinite(eff_prog) && eff_prog >= target_threshold) {
            s_fired_for_current_window = true;
            s_last_triggered_count     = gen_count;
            const float great_mid      = std::clamp(zone_start + (bonus_len * 0.5f), 0.15f, 0.85f);
            (void)mem::write_data<float>(sc_addr + off.skillcheck_current_prog, great_mid);
            (void)mem::write_data<float>(sc_addr + off.skillcheck_ui_prog,      great_mid);
            (void)mem::write_data<int32_t>(sc_addr + off.skillcheck_last_result, 1);
            if (in_process) {
                const uintptr_t sc_owner = mem::read<uintptr_t>(sc_addr + off.skillcheck_owner_pawn).value_or(0ULL);
                const uintptr_t sc_inter = mem::read<uintptr_t>(sc_addr + off.skillcheck_current_inter).value_or(0ULL);
                if (validate_canonical_uobject(sc_owner, 0x0AA0U, guobjectarray_abs, off) &&
                    validate_canonical_uobject(sc_inter, 0x0100U, guobjectarray_abs, off)) {
                    using OnSkillCheckInputFn = void(*)(uintptr_t);
                    const auto input_fn = reinterpret_cast<OnSkillCheckInputFn>(ue4_base + off.rva_skillcheck_input);
                    input_fn(sc_addr);
                    return true;
                }
            }
        }
        return false;
#endif
    }

    // 3b. Trap Immunity: Clears Trapped immobilize state on local pawn's _stateMachine (+0x0AA8 -> +0x01A0)
    //     and disables collision overlap on nearby BearTrap actors
    static void apply_trap_immunity(
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!validate_canonical_uobject(local_pawn, 0x0AC0U, guobjectarray_abs, off)) {
            return;
        }
        if (const auto sm = mem::read<uintptr_t>(local_pawn + off.dbd_player_state_machine);
            sm.has_value() && validate_canonical_uobject(*sm, 0x01B0U, guobjectarray_abs, off)) {
            if (const auto imm = mem::read<uint8_t>(*sm + off.state_machine_immobilize)) {
                if (*imm == static_cast<uint8_t>(hud::ImmobilizeState::Trapped)) {
                    (void)mem::write_data<uint8_t>(
                        *sm + off.state_machine_immobilize,
                        static_cast<uint8_t>(hud::ImmobilizeState::None));
                }
            }
        }
    }

    static void disable_trap_actor_collision(
        uintptr_t trap_actor,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!validate_canonical_uobject(trap_actor, 0x02E0U, guobjectarray_abs, off)) {
            return;
        }
        if (const auto is_set = mem::read<uint8_t>(trap_actor + off.beartrap_is_trap_set);
            is_set.has_value() && *is_set != 0U) {
            (void)mem::write_data<uint8_t>(trap_actor + off.beartrap_is_trap_set, 0U);
            (void)mem::write_data<uint8_t>(trap_actor + off.beartrap_was_disarmed, 1U);
        }
        if (const auto contains = mem::read<uint8_t>(trap_actor + off.beartrap_contains_surv);
            contains.has_value() && *contains != 0U) {
            (void)mem::write_data<uint8_t>(trap_actor + off.beartrap_contains_surv, 0U);
        }
        if (const auto flags = mem::read<uint8_t>(trap_actor + off.actor_collision_flags)) {
            const uint8_t cleared = hud::compute_actor_collision_byte(*flags, false);
            if (cleared != *flags) {
                (void)mem::write_data<uint8_t>(trap_actor + off.actor_collision_flags, cleared);
            }
        }
    }

    // 4a. Bidirectional Fly Cheat & Safe Mid-Air Disable Sequence
    static Status apply_fly_mode(
        uintptr_t local_pc,
        uintptr_t local_pawn,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off,
        bool fly_enabled,
        float fly_speed,
        float fly_vertical_speed = 0.0f
    ) noexcept {
        const hud::CharacterMovementChain chain = resolve_local_movement_chain(
            local_pc, local_pawn, guobjectarray_abs, off);
        if (!chain.is_valid || chain.acknowledged_pawn == 0 || chain.movement_component == 0) {
            return Status::UnmappedOrFault;
        }
        const uintptr_t pawn_addr = chain.acknowledged_pawn;
        const uintptr_t move_comp = chain.movement_component;

        static uintptr_t s_held_pawn          = 0;
        static float     s_held_z             = 0.0f;
        static bool      s_has_held_z         = false;
        static uintptr_t s_fly_saved_pawn     = 0;
        static uint8_t   s_saved_col_flags    = 0;
        static bool      s_has_saved_col      = false;
        static uint8_t   s_saved_dirty_flags  = 0;
        static bool      s_has_saved_dirty    = false;
        static float     s_saved_gravity      = 1.0f;
        static float     s_saved_braking_fly  = 0.0f;
        static float     s_saved_max_fly_spd  = 600.0f;

        if (!fly_enabled) {
            const FVector zero_vel{0.0f, 0.0f, 0.0f};
            (void)mem::write_data<FVector>(move_comp + off.movement_velocity, zero_vel);
            (void)mem::write_data<float>(move_comp + off.gravity_scale, s_has_saved_dirty ? s_saved_gravity : 1.0f);
            if (s_has_saved_dirty) {
                (void)mem::write_data<float>(move_comp + off.braking_decel_flying, s_saved_braking_fly);
                (void)mem::write_data<float>(move_comp + off.max_fly_speed, s_saved_max_fly_spd);
                (void)mem::write_data<uint8_t>(move_comp + off.movement_dirty_flags, s_saved_dirty_flags);
            } else if (const auto cur_flags = mem::read<uint8_t>(move_comp + off.movement_dirty_flags)) {
                (void)mem::write_data<uint8_t>(
                    move_comp + off.movement_dirty_flags,
                    static_cast<uint8_t>(*cur_flags & ~0x04U));
            }
            if (s_has_saved_col) {
                (void)mem::write_data<uint8_t>(pawn_addr + off.actor_collision_flags, s_saved_col_flags);
            } else if (const auto col_flags = mem::read<uint8_t>(pawn_addr + off.actor_collision_flags)) {
                const uint8_t col_byte = hud::compute_actor_collision_byte(*col_flags, true);
                (void)mem::write_data<uint8_t>(pawn_addr + off.actor_collision_flags, col_byte);
            }
            // Transition to MOVE_Falling (3) so UE4's PhysFalling safely lands and enters MOVE_Walking
            // without crashing on a missing CurrentFloor or mid-air state.
            constexpr uint8_t kMoveFalling = 3U;
            const Status mode_st = mem::write_data<uint8_t>(move_comp + off.movement_mode, kMoveFalling);
            s_held_pawn       = 0;
            s_has_held_z      = false;
            s_fly_saved_pawn  = 0;
            s_has_saved_col   = false;
            s_has_saved_dirty = false;
            return mode_st;
        }

        if (s_fly_saved_pawn != pawn_addr) {
            s_fly_saved_pawn = pawn_addr;
            if (const auto col = mem::read<uint8_t>(pawn_addr + off.actor_collision_flags)) {
                s_saved_col_flags = *col;
                s_has_saved_col   = true;
            }
            if (const auto dflags = mem::read<uint8_t>(move_comp + off.movement_dirty_flags)) {
                s_saved_dirty_flags = *dflags;
                s_saved_gravity     = mem::read<float>(move_comp + off.gravity_scale).value_or(1.0f);
                s_saved_braking_fly = mem::read<float>(move_comp + off.braking_decel_flying).value_or(0.0f);
                s_saved_max_fly_spd = mem::read<float>(move_comp + off.max_fly_speed).value_or(600.0f);
                s_has_saved_dirty   = true;
            }
        }

        (void)fly_speed;
        (void)mem::write_data<float>(move_comp + off.gravity_scale, 0.0f);

        FVector cur_vel = mem::read<FVector>(move_comp + off.movement_velocity).value_or(FVector{0.0f, 0.0f, 0.0f});
        const auto rc_opt = mem::read<uintptr_t>(pawn_addr + off.root_component);
        const bool rc_valid = rc_opt.has_value() && validate_canonical_uobject(*rc_opt, 0x01D0U, guobjectarray_abs, off);

        if (std::isfinite(fly_vertical_speed) && std::abs(fly_vertical_speed) > 1.0f) {
            // Use MOVE_Falling (3U) rather than MOVE_Flying (5U) while ascending/descending
            // so survivor root bone and capsule alignment never tilt sideways or sink into the floor.
            constexpr uint8_t kMoveFalling = 3U;
            const Status mode_st = mem::write_data<uint8_t>(move_comp + off.movement_mode, kMoveFalling);
            cur_vel.z = std::clamp(fly_vertical_speed, -600.0f, 600.0f);
            (void)mem::write_data<FVector>(move_comp + off.movement_velocity, cur_vel);
            if (rc_valid) {
                if (const auto rel_loc = mem::read<FVector>(*rc_opt + off.relative_location);
                    rel_loc.has_value() && std::isfinite(rel_loc->z)) {
                    FVector updated_loc = *rel_loc;
                    updated_loc.z += cur_vel.z * 0.016f;
                    (void)mem::write_data<FVector>(*rc_opt + off.relative_location, updated_loc);
                    s_held_pawn  = pawn_addr;
                    s_held_z     = updated_loc.z;
                    s_has_held_z = true;
                }
            }
            return mode_st;
        } else {
            // Holding altitude (0% vertical speed): keep MOVE_Walking (1U) with zero vertical velocity
            // so normal survivor walking animations and capsule alignment stay intact.
            constexpr uint8_t kMoveWalking = 1U;
            const Status mode_st = mem::write_data<uint8_t>(move_comp + off.movement_mode, kMoveWalking);
            cur_vel.z = 0.0f;
            (void)mem::write_data<FVector>(move_comp + off.movement_velocity, cur_vel);
            if (rc_valid) {
                if (const auto rel_loc = mem::read<FVector>(*rc_opt + off.relative_location);
                    rel_loc.has_value() && std::isfinite(rel_loc->z)) {
                    if (!s_has_held_z || s_held_pawn != pawn_addr) {
                        s_held_pawn  = pawn_addr;
                        s_held_z     = rel_loc->z;
                        s_has_held_z = true;
                    } else {
                        FVector held_loc = *rel_loc;
                        held_loc.z       = s_held_z;
                        (void)mem::write_data<FVector>(*rc_opt + off.relative_location, held_loc);
                    }
                }
            }
            return mode_st;
        }
    }

    // 4b. Instant Hatch Escape (Thread-safe, binary-verified memory writes only)
    static Status apply_force_escape(
        uintptr_t local_pawn,
        uintptr_t local_player_st,
        uintptr_t hatch_actor_ptr,
        const FVector* escape_portal_pos,
        uintptr_t /*ue4_base*/,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!validate_canonical_uobject(local_pawn, 0x1740U, guobjectarray_abs, off)) {
            return Status::InvalidArgument;
        }

        const bool hatch_valid = (hatch_actor_ptr != 0) &&
            validate_canonical_uobject(hatch_actor_ptr, 0x03E0U, guobjectarray_abs, off);
        const bool has_dest = (escape_portal_pos != nullptr) &&
            std::isfinite(escape_portal_pos->x) &&
            std::isfinite(escape_portal_pos->y) &&
            std::isfinite(escape_portal_pos->z);

        if (!hatch_valid && !has_dest) {
            return Status::InvalidArgument;
        }

        if (const auto col_flags = mem::read<uint8_t>(local_pawn + off.actor_collision_flags)) {
            const uint8_t col_on = hud::compute_actor_collision_byte(*col_flags, true);
            (void)mem::write_data<uint8_t>(local_pawn + off.actor_collision_flags, col_on);
        }
        if (const auto move_comp = mem::read<uintptr_t>(local_pawn + off.character_movement);
            move_comp.has_value() && validate_canonical_uobject(*move_comp, 0x0180U, guobjectarray_abs, off)) {
            (void)mem::write_data<uint8_t>(*move_comp + off.movement_mode, hud::resolve_movement_mode_byte(false));
        }

        if (hatch_valid) {
            constexpr uint8_t kHatchStateOpened = 2U; // EHatchState::Opened
            (void)mem::write_data<uint8_t>(hatch_actor_ptr + off.hatch_state, kHatchStateOpened);
            (void)mem::write_data<uint8_t>(hatch_actor_ptr + off.hatch_is_force_open, 1U);
            crashlog::CrashLogger::instance().log_action(
                "ESCAPE",
                "apply_force_escape:hatch",
                off.hatch_state,
                "hatch_valid=1 wrote state=2 force_open=1");
        } else {
            crashlog::CrashLogger::instance().log_action(
                "ESCAPE",
                "apply_force_escape:hatch",
                off.hatch_state,
                "hatch_valid=0");
        }

        Status move_st = Status::Ok;
        if (has_dest) {
            const auto root_comp = mem::read<uintptr_t>(local_pawn + off.root_component);
            if (root_comp.has_value() && validate_canonical_uobject(*root_comp, 0x0170U, guobjectarray_abs, off)) {
                const FVector dest{
                    escape_portal_pos->x,
                    escape_portal_pos->y,
                    escape_portal_pos->z + 65.0f
                };
                move_st = mem::write_data<FVector>(*root_comp + off.relative_location, dest);
                char msg[128]{};
                std::snprintf(msg, sizeof(msg), "teleport rc=0x%llX st=%d pos=(%.1f,%.1f,%.1f)",
                    static_cast<unsigned long long>(*root_comp),
                    static_cast<int>(move_st),
                    static_cast<double>(dest.x),
                    static_cast<double>(dest.y),
                    static_cast<double>(dest.z));
                crashlog::CrashLogger::instance().log_action("ESCAPE", "apply_force_escape:tp", off.relative_location, msg);
            } else {
                crashlog::CrashLogger::instance().log_action(
                    "ESCAPE",
                    "apply_force_escape:tp",
                    off.root_component,
                    "root_component invalid or null");
            }
        } else {
            crashlog::CrashLogger::instance().log_action("ESCAPE", "apply_force_escape:tp", 0, "has_dest=0");
        }
        (void)local_player_st;
        return move_st;
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

            // Priority 4: Hatch (AHatch) — explicitly exclude Huntress Hatchets (BP_HatchetRack / BP_HatchetProjectile)
            if (!type_contains("Hatchet") &&
                !icontains(class_name, "Hatchet") &&
                !icontains(actor_name, "Hatchet") &&
                (type_contains("->Hatch->") ||
                 type_contains("BP_Hatch")  ||
                 icontains(class_name, "Hatch"))) {
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

        if (!icontains(actor_name, "Hatchet") &&
            (icontains(actor_name, "BP_Hatch") ||
             icontains(actor_name, "Hatch01")  ||
             icontains(actor_name, "Hatch_"))) {
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

    // Per-generator component & tunable MaxCharge cache (keyed by actor_ptr, 100ms pointer/tunable refresh).
    // Live progress fields (IsRepaired +0x369, NativePercentComplete +0x378, and UChargeableComponent::_currentCharge +0x1BC/+0x1B8)
    // are read directly every frame so repairing/regressing/completed generators update in real time.
    struct GeneratorProgressCacheEntry {
        uintptr_t actor_ptr{0};
        uintptr_t charge_comp_ptr{0};
        float     max_charge{90.0f};
        float     last_logged_pct{-10.0f};
        uint64_t  last_comp_refresh_ms{0};
    };

    [[nodiscard]] static uint64_t monotonic_now_ms() noexcept {
        struct timespec ts{};
        if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            return 0ULL;
        }
        return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
               static_cast<uint64_t>(ts.tv_nsec / 1000000L);
    }

    [[nodiscard]] static float read_generator_progress_pct(
        uintptr_t actor_ptr,
        uintptr_t guobjectarray_abs,
        const EngineOffsets& off
    ) noexcept {
        if (!InProcessMemory::IsValidPtr(actor_ptr, off.generator_charge_comp + sizeof(uintptr_t))) {
            return 0.0f;
        }

        static std::array<GeneratorProgressCacheEntry, 16> s_gen_cache{};
        const uint64_t now_ms = monotonic_now_ms();

        size_t slot = s_gen_cache.size();
        size_t oldest_slot = 0;
        uint64_t oldest_ms = UINT64_MAX;
        for (size_t i = 0; i < s_gen_cache.size(); ++i) {
            if (s_gen_cache[i].actor_ptr == actor_ptr) {
                slot = i;
                break;
            }
            if (s_gen_cache[i].actor_ptr == 0) {
                oldest_slot = i;
                oldest_ms   = 0;
            } else if (s_gen_cache[i].last_comp_refresh_ms < oldest_ms) {
                oldest_ms   = s_gen_cache[i].last_comp_refresh_ms;
                oldest_slot = i;
            }
        }
        if (slot == s_gen_cache.size()) {
            slot = oldest_slot;
            s_gen_cache[slot] = GeneratorProgressCacheEntry{};
            s_gen_cache[slot].actor_ptr = actor_ptr;
        }

        GeneratorProgressCacheEntry& entry = s_gen_cache[slot];
        if (entry.last_comp_refresh_ms == 0ULL || (now_ms - entry.last_comp_refresh_ms) >= 100ULL) {
            entry.last_comp_refresh_ms = now_ms;
            uintptr_t resolved_comp = 0;
            float     resolved_max  = 90.0f;

            if (const auto maybe_comp = mem::read<uintptr_t>(actor_ptr + off.generator_charge_comp);
                maybe_comp.has_value())
            {
                const uintptr_t untagged_comp = *maybe_comp & 0x00FFFFFFFFFFFFFFULL;
                if ((untagged_comp & 0x7ULL) == 0ULL &&
                    untagged_comp >= 0x0000000100000000ULL &&
                    untagged_comp <= 0x00007FFFFFFFFFFFULL &&
                    validate_canonical_uobject(
                        *maybe_comp,
                        off.chargeable_tunable_count + sizeof(int32_t),
                        guobjectarray_abs,
                        off))
                {
                    resolved_comp = *maybe_comp;
                    const auto map_data  = mem::read<uintptr_t>(resolved_comp + off.chargeable_tunable_data);
                    const auto map_count = mem::read<int32_t>(resolved_comp + off.chargeable_tunable_count);
                    if (map_data.has_value() && map_count.has_value() &&
                        *map_count > 0 && *map_count <= 64 &&
                        InProcessMemory::IsValidPtr(*map_data, off.chargeable_tunable_val + sizeof(float)))
                    {
                        if (const auto tunable_max = mem::read<float>(*map_data + off.chargeable_tunable_val);
                            tunable_max.has_value() && std::isfinite(*tunable_max) &&
                            *tunable_max >= 10.0f && *tunable_max <= 200.0f)
                        {
                            resolved_max = *tunable_max;
                        }
                    }
                }
            }
            entry.charge_comp_ptr = resolved_comp;
            entry.max_charge      = resolved_max;
        }

        // 1. Live completion flags on AGenerator: IsRepaired (+0x0369) or Activated (+0x0368)
        const uint8_t is_repaired = mem::read<uint8_t>(actor_ptr + off.generator_is_repaired).value_or(0U);
        const uint8_t activated   = mem::read<uint8_t>(actor_ptr + off.generator_activated).value_or(0U);

        // 2. Live AGenerator::NativePercentComplete (+0x0378, float in [0.0f, 1.0f])
        float native_pct   = -1.0f;
        float native_ratio = -1.0f;
        if (const auto raw_native = mem::read<float>(actor_ptr + off.generator_native_percent);
            raw_native.has_value() && std::isfinite(*raw_native) &&
            *raw_native >= 0.0f && *raw_native <= 1.05f)
        {
            native_ratio = *raw_native;
            native_pct   = hud::adapter::normalize_generator_progress_pct(*raw_native, 1.0f);
        }

        // 3. Live UChargeableComponent::_currentCharge (FSpeedBasedNetSyncedValue at +0x01BC live / +0x01B8 replicated)
        float charge_pct = -1.0f;
        float raw_charge = -1.0f;
        if (entry.charge_comp_ptr != 0) {
            const float max_chg = (entry.max_charge >= 10.0f && entry.max_charge <= 200.0f) ? entry.max_charge : 90.0f;
            if (const auto live_chg = mem::read<float>(entry.charge_comp_ptr + off.chargeable_live_charge);
                live_chg.has_value() && std::isfinite(*live_chg) &&
                *live_chg >= 0.0f && *live_chg <= max_chg * 1.05f)
            {
                raw_charge = *live_chg;
                charge_pct = hud::adapter::normalize_generator_progress_pct(*live_chg, max_chg);
            }
            if (const auto rep_chg = mem::read<float>(entry.charge_comp_ptr + off.chargeable_replicated_chg);
                rep_chg.has_value() && std::isfinite(*rep_chg) &&
                *rep_chg >= 0.0f && *rep_chg <= max_chg * 1.05f)
            {
                const float rep_pct = hud::adapter::normalize_generator_progress_pct(*rep_chg, max_chg);
                if (charge_pct < 0.0f || (charge_pct == 0.0f && rep_pct > 0.0f)) {
                    raw_charge = *rep_chg;
                    charge_pct = rep_pct;
                }
            }
        }

        float final_pct = 0.0f;
        if ((is_repaired & 0x01U) != 0U || (activated & 0x01U) != 0U) {
            final_pct = 100.0f;
        } else if (charge_pct >= 0.0f && native_pct >= 0.0f) {
            // Prefer live FSpeedBasedNetSyncedValue when non-zero; fall back to NativePercentComplete when component is 0
            final_pct = (charge_pct > 0.0f) ? charge_pct : native_pct;
            if (native_pct >= 99.9f || charge_pct >= 99.9f) {
                final_pct = 100.0f;
            }
        } else if (charge_pct >= 0.0f) {
            final_pct = (charge_pct >= 99.9f) ? 100.0f : charge_pct;
        } else if (native_pct >= 0.0f) {
            final_pct = (native_pct >= 99.9f) ? 100.0f : native_pct;
        }

        if (std::fabs(final_pct - entry.last_logged_pct) >= 5.0f ||
            (final_pct >= 100.0f && entry.last_logged_pct < 100.0f)) {
            entry.last_logged_pct = final_pct;
            DIAG_LOGI(
                "[GEN-PROGRESS] Gen=0x%llX Progress=%.1f%% | NativeRatio=%.4f | ChargeComp=0x%llX RawCharge=%.2f/%.1f | IsRepaired=%u Activated=%u",
                static_cast<unsigned long long>(actor_ptr),
                static_cast<double>(final_pct),
                static_cast<double>(native_ratio),
                static_cast<unsigned long long>(entry.charge_comp_ptr),
                static_cast<double>(raw_charge),
                static_cast<double>(entry.max_charge),
                static_cast<unsigned>(is_repaired),
                static_cast<unsigned>(activated)
            );
        }

        return std::clamp(final_pct, 0.0f, 100.0f);
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

        // 1. Check ATotem::_totemState (+0x03A8): 0 = ETotemState::Cleansed, 1 = Dull, 2 = Hex, 3 = Boon
        if (InProcessMemory::IsValidPtr(actor_ptr, off.totem_state + sizeof(uint8_t))) {
            if (const auto t_state = mem::read<uint8_t>(actor_ptr + off.totem_state);
                t_state.has_value() && (*t_state == 0U || *t_state > 3U)) {
                return -1.0f;
            }
        }

        // 2. Verify RootComponent visibility flags: if bHiddenInGame (0x1000) is set and bVisible (0x0040) is cleared,
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

        static uint64_t s_outline_tick = 0ULL;
        ++s_outline_tick;
        hud::outline::NativeOutlineController& outline_ctrl = hud::outline::get_native_outline_controller();
        const hud::outline::NativeOutlineConfig& outline_cfg = hud::outline::get_native_outline_config();
        auto outline_read_fn = [](uintptr_t addr, auto& out_val) noexcept -> bool {
            if (const auto v = mem::read<std::decay_t<decltype(out_val)>>(addr)) {
                out_val = *v;
                return true;
            }
            return false;
        };
        auto outline_write_fn = [](uintptr_t addr, const auto& val) noexcept -> bool {
            return mem::write_data<std::decay_t<decltype(val)>>(addr, val) == Status::Ok;
        };

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

                const hud::outline::OutlineCategory outline_cat = hud::outline::classify_outline_category(
                    name_buf.data(),
                    class_buf.data(),
                    super_chain_buf.data());

                int32_t prio = 99;
                bool is_unresolved = false;
                const char* cat_label = classify_actor_category(
                    name_buf.data(),
                    class_buf.data(),
                    super_chain_buf.data(),
                    &prio,
                    &is_unresolved);
                if (cat_label == nullptr) {
                    if (outline_cat != hud::outline::OutlineCategory::None) {
                        if (const auto w_pos = read_actor_location(*actor_ptr, guobjectarray_abs, off);
                            w_pos.has_value() && hud::adapter::is_valid_world_coordinate(w_pos->x, w_pos->y, w_pos->z)) {
                            const float w_dsq = origin.dist_squared(*w_pos);
                            if (std::isfinite(w_dsq) && w_dsq <= max_dist_sq) {
                                const float w_dist_m = std::sqrt(w_dsq) * 0.01f;
                                outline_ctrl.evaluate_actor(
                                    outline_read_fn,
                                    outline_write_fn,
                                    *actor_ptr,
                                    outline_cat,
                                    w_dist_m,
                                    0.0f,
                                    s_outline_tick,
                                    guobjectarray_abs,
                                    outline_cfg,
                                    ignore_actor);
                            }
                        }
                    }
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
                if (prio == 2) {
                    // Exclude sacrificed/dead survivors (EImmobilizeState::Dead == 4)
                    if (InProcessMemory::IsValidPtr(*actor_ptr, off.camper_immobilize_state + sizeof(uint8_t))) {
                        if (const auto imm = mem::read<uint8_t>(*actor_ptr + off.camper_immobilize_state);
                            imm.has_value() && *imm == static_cast<uint8_t>(hud::ImmobilizeState::Dead)) {
                            continue;
                        }
                    }
                } else if (prio == 3) {
                    gen_pct = read_generator_progress_pct(*actor_ptr, guobjectarray_abs, off);
                    if (hud::adapter::should_hide_generator(gen_pct)) {
                        continue;
                    }
                } else if (prio == 6) {
                    // Preserve Shrine (Totem) nodes in the telemetry array with progress_pct >= 0.0f
                    // until ATotem::_totemState (+0x03A8) or visibility flags confirm cleansing (progress_pct < 0.0f)
                    gen_pct = read_totem_state_progress(
                        *actor_ptr,
                        guobjectarray_abs,
                        off,
                        name_buf.data(),
                        class_buf.data());
                    if (!hud::adapter::is_totem_active(gen_pct, cat_label)) {
                        continue;
                    }
                } else if (prio == 7) {
                    // Exclude already searched/opened Chests (ASearchable::_hasBeenSearched at +0x038C)
                    if (InProcessMemory::IsValidPtr(*actor_ptr, off.searchable_searched + sizeof(uint8_t))) {
                        if (const auto searched = mem::read<uint8_t>(*actor_ptr + off.searchable_searched);
                            searched.has_value() && (*searched & 0x01U) != 0U) {
                            continue;
                        }
                    }
                } else if (prio == 8) {
                    // Exclude sacrificed or sabotaged Hooks (AMeatHook::_isSacrificed +0x04E2, _isSabotaged +0x04E4)
                    if (InProcessMemory::IsValidPtr(*actor_ptr, off.meathook_is_sabotaged + sizeof(uint8_t))) {
                        const uint8_t sac = mem::read<uint8_t>(*actor_ptr + off.meathook_is_sacrificed).value_or(0U);
                        const uint8_t sab = mem::read<uint8_t>(*actor_ptr + off.meathook_is_sabotaged).value_or(0U);
                        if ((sac & 0x01U) != 0U || (sab & 0x01U) != 0U) {
                            continue;
                        }
                    }
                } else if (prio == 9) {
                    // Exclude destroyed or illusionary Pallets (APallet::_state at +0x0368: 3=Destroyed, 4=Illusionary)
                    if (InProcessMemory::IsValidPtr(*actor_ptr, off.pallet_state + sizeof(uint8_t))) {
                        if (const auto p_state = mem::read<uint8_t>(*actor_ptr + off.pallet_state);
                            p_state.has_value() && *p_state >= 3U) {
                            continue;
                        }
                    }
                    gen_pct = 0.0f;
                }

                if (outline_cat != hud::outline::OutlineCategory::None) {
                    outline_ctrl.evaluate_actor(
                        outline_read_fn,
                        outline_write_fn,
                        *actor_ptr,
                        outline_cat,
                        dist_m,
                        gen_pct,
                        s_outline_tick,
                        guobjectarray_abs,
                        outline_cfg,
                        ignore_actor);
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

        outline_ctrl.prune_and_restore_stale(
            outline_read_fn,
            outline_write_fn,
            s_outline_tick,
            guobjectarray_abs,
            ulevels.front());

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

        // Strictly clamp to ArrayMax - 1 in-place so UE4's FMallocBinned2 heap pointer (fstr.data_ptr)
        // is never replaced with a static .bss address (prevents checkMalloc SIGABRT on login/reallocation).
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

        const std::array<uintptr_t, 3> primary_offsets = {
            off.player_name_private,
            0x0308U,
            0x0300U
        };

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
            const Status st = write_fstring_u16_safe(fstring_addr, *fstr, new_name);
            if (st == Status::Ok) {
                return Status::Ok;
            }
        }

        return Status::UnmappedOrFault;
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
    static bool          s_first_poll_init   = false;

    static constexpr const char* kCfgPath = "/data/local/tmp/dbdm_mod_config.cfg";
    if (!s_first_poll_init) {
        s_first_poll_init = true;
        struct stat init_st{};
        if (::stat(kCfgPath, &init_st) == 0) {
            // Ignore any stale config file left over from a previous session on initial game boot
            s_last_cfg_mtime_ns =
                static_cast<std::int64_t>(init_st.st_mtim.tv_sec) * 1000000000LL +
                static_cast<std::int64_t>(init_st.st_mtim.tv_nsec);
        }
        return;
    }

    if ((++s_tick_counter & 0x03U) != 0U) {
        return; // Poll every 4 ticks (~60ms)
    }

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

    bool req_crash_log_active = false;
    char req_crash_log_path[crashlog::kMaxPathLen]{};

    char line[320]{};
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
        } else if (std::strcmp(key, "native_aura") == 0) {
            gp_cfg.native_aura_enabled = (val[0] == '1');
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
        } else if (std::strcmp(key, "crash_log_active") == 0) {
            req_crash_log_active = (val[0] == '1');
        } else if (std::strcmp(key, "crash_log_path") == 0) {
            std::strncpy(req_crash_log_path, val, crashlog::kMaxPathLen - 1);
            req_crash_log_path[crashlog::kMaxPathLen - 1] = '\0';
        }
    }

    DIAG_LOGI(
        "[IPC-CMD] Config synced from APK -> draw_markers=%d | max_dist=%.1f | filter=\"%s\" | override_name=%d (\"%s\") | apply_kinematics=%d (walk=%.1f%%, accel=%.1f%%, jump=%.1f%%, time=%.2f) | auto_skillcheck=%d | fly_cheat_enabled=%d (fly_vertical_speed=%.1f) | apply_fov_override=%d",
        hud_data.draw_2d_node_markers ? 1 : 0,
        static_cast<double>(hud_data.max_render_distance_m),
        hud_data.label_filter,
        hud_data.apply_custom_player_name ? 1 : 0,
        hud_data.custom_player_name,
        hud_data.apply_custom_kinematics ? 1 : 0,
        static_cast<double>(hud_data.walk_speed_pct),
        static_cast<double>(hud_data.accel_pct),
        static_cast<double>(hud_data.jump_pct),
        static_cast<double>(hud_data.time_scale),
        gp_cfg.auto_skillcheck ? 1 : 0,
        gp_cfg.fly_cheat_enabled ? 1 : 0,
        static_cast<double>(gp_cfg.fly_vertical_speed),
        gp_cfg.apply_fov_override ? 1 : 0
    );

    pthread_mutex_unlock(&hud_data.mutex);
    std::fclose(fp);

    if (req_crash_log_active && req_crash_log_path[0] != '\0') {
        if (crashlog::CrashLogger::instance().attach_session_path(req_crash_log_path)) {
            if (g_state.ue4_module.base_address != 0) {
                crashlog::CrashLogger::instance().set_module_bases(g_state.ue4_module.base_address, 0);
                crashlog::CrashLogger::instance().install_signal_handlers();
                if (memory::InProcessMemory::effective_pid() == ::getpid()) {
                    crashlog::CrashLogger::instance().install_ue4_crash_hooks(g_state.ue4_module.base_address);
                }
            }
        }
    } else if (!req_crash_log_active && crashlog::CrashLogger::instance().is_active()) {
        crashlog::CrashLogger::instance().stop_session();
    }
    crashlog::CrashLogger::instance().log_action(
        "TOGGLE",
        "poll_external_mod_menu_config",
        0,
        "Synced config from APK");
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

INSTRUMENT_LOCAL inline uint64_t compute_fnv1a64(std::string_view data) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

INSTRUMENT_LOCAL inline Status normalize_pipe_status_record(const char* file_path) noexcept {
    if (file_path == nullptr || file_path[0] == '\0') {
        return Status::InvalidArgument;
    }
    std::FILE* fp = std::fopen(file_path, "rb");
    if (!fp) {
        return Status::Ok;
    }
    char line[512]{};
    const char* got = std::fgets(line, sizeof(line), fp);
    std::fclose(fp);
    if (!got) {
        return Status::Ok;
    }

    size_t len = std::strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    if (std::strncmp(line, "1|", 2) != 0) {
        return Status::Ok;
    }

    std::array<char*, 6> cols{};
    cols[0] = line;
    size_t col_idx = 1;
    for (char* p = line; *p != '\0' && col_idx < cols.size(); ++p) {
        if (*p == '|') {
            *p = '\0';
            cols[col_idx++] = p + 1;
        }
    }
    if (col_idx < 6 || std::strchr(cols[5], '|') != nullptr) {
        return Status::Ok;
    }
    if (std::strcmp(cols[3], "0") == 0) {
        return Status::Ok;
    }

    char prefix[384]{};
    const int prefix_len = std::snprintf(
        prefix,
        sizeof(prefix),
        "%s|%s|%s|0|%s",
        cols[0],
        cols[1],
        cols[2],
        cols[4]
    );
    if (prefix_len <= 0 || static_cast<size_t>(prefix_len) >= sizeof(prefix)) {
        return Status::InvalidArgument;
    }

    const char* hash_str = cols[5];
    char recomputed_hash[24]{};
    if (hash_str[0] == '\0') {
        const uint64_t fnv = compute_fnv1a64(std::string_view{prefix, static_cast<size_t>(prefix_len)});
        std::snprintf(recomputed_hash, sizeof(recomputed_hash), "%016llx", static_cast<unsigned long long>(fnv));
        hash_str = recomputed_hash;
    }

    char out_line[512]{};
    const int out_len = std::snprintf(out_line, sizeof(out_line), "%s|%s\n", prefix, hash_str);
    if (out_len <= 0 || static_cast<size_t>(out_len) >= sizeof(out_line)) {
        return Status::InvalidArgument;
    }

    char tmp_path[320]{};
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);
    if (std::FILE* out_fp = std::fopen(tmp_path, "wb")) {
        const size_t written = std::fwrite(out_line, 1, static_cast<size_t>(out_len), out_fp);
        std::fclose(out_fp);
        if (written == static_cast<size_t>(out_len) && std::rename(tmp_path, file_path) == 0) {
            return Status::Ok;
        }
        (void)::unlink(tmp_path);
    }

    if (std::FILE* out_fp = std::fopen(file_path, "wb")) {
        const size_t written = std::fwrite(out_line, 1, static_cast<size_t>(out_len), out_fp);
        std::fclose(out_fp);
        return (written == static_cast<size_t>(out_len)) ? Status::Ok : Status::PartialTransfer;
    }
    return Status::UnmappedOrFault;
}

INSTRUMENT_LOCAL static void execute_diagnostic_tick() noexcept {
    poll_external_mod_menu_config();
    static constexpr const char* kLocalDiagStatusPath = "/data/local/tmp/dbdm_diag_status.log";
    (void)normalize_pipe_status_record(kLocalDiagStatusPath);

    hud::GameplayManipulationConfig tick_gp_cfg{};
    float tick_max_dist_m          = 250.0f;
    bool  tick_show_antagonist     = true;
    bool  tick_show_teammates      = true;
    bool  tick_show_power_nodes    = true;
    bool  tick_show_shrines        = true;
    bool  tick_show_containers     = true;
    bool  tick_show_barriers       = true;
    bool  tick_show_portals        = true;
    {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud::GameplayManipulationConfig& gp_cfg = hud::get_gameplay_config();
        if (std::strncmp(hud_data.label_filter, "@gp:", 4) == 0) {
            (void)hud::parse_gameplay_ipc_string(hud_data.label_filter, hud_data, gp_cfg);
        }
        tick_gp_cfg           = gp_cfg;
        tick_max_dist_m       = hud_data.max_render_distance_m;
        tick_show_antagonist  = hud_data.show_antagonist;
        tick_show_teammates   = hud_data.show_teammates;
        tick_show_power_nodes = hud_data.show_power_nodes;
        tick_show_shrines     = hud_data.show_shrines;
        tick_show_containers  = hud_data.show_containers;
        tick_show_barriers    = hud_data.show_barriers;
        tick_show_portals     = hud_data.show_portals;
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
        hud_data.remaining_nodes         = 0;
        hud_data.active_generators       = 0;
        hud_data.required_generators     = 0;
        hud_data.nearest_gen_progress    = 0.0f;
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
        hud_data.remaining_nodes         = 0;
        hud_data.active_generators       = 0;
        hud_data.required_generators     = 0;
        hud_data.nearest_gen_progress    = 0.0f;
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
    //     - Fallback Pawn / PlayerState / CameraManager / ADBDGameState when in Lobby or Custom Match
    //     - Diagnostic actor dump on level change
    uint32_t active_gen_count     = 0;
    uint32_t required_gen_count   = 0;
    uintptr_t local_game_state    = 0;
    uintptr_t hatch_actor_ptr     = 0;
    if (const auto gs = mem::read<uintptr_t>(*uworld + off.world_game_state);
        gs.has_value() &&
        InProcessMemory::IsValidPtr(*gs, off.gamestate_required_gens + sizeof(int32_t)) &&
        ue4::SpatialAndKinematicPipeline::validate_live_uobject(*gs, guobjectarray_abs, off, nullptr))
    {
        local_game_state = *gs;
    }
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

            // Fallback ADBDGameState discovery in ULevel::Actors
            if (local_game_state == 0 &&
                (hud::detail::icontains_str(scan_name.data(), "DBDGameState") ||
                 hud::detail::icontains_str(cls_name.data(), "DBDGameState")) &&
                InProcessMemory::IsValidPtr(*a_ptr, off.gamestate_required_gens + sizeof(int32_t))) {
                local_game_state = *a_ptr;
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

            // Capture Hatch actor pointer for Instant Hatch Escape (Física tab)
            if (hatch_actor_ptr == 0 &&
                (hud::detail::icontains_str(scan_name.data(), "BP_Hatch") ||
                 hud::detail::icontains_str(cls_name.data(), "Hatch")) &&
                !hud::detail::icontains_str(cls_name.data(), "Component") &&
                !hud::detail::icontains_str(cls_name.data(), "Zone") &&
                !hud::detail::icontains_str(cls_name.data(), "Widget")) {
                hatch_actor_ptr = *a_ptr;
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

    {
        hud::outline::NativeOutlineConfig& outline_cfg = hud::outline::get_native_outline_config();
        const bool outline_write_ready =
            (!is_lobby_level && local_pawn != 0 && s_teardown_write_lock_ticks == 0U);
        outline_cfg.master_enabled = outline_write_ready && tick_gp_cfg.native_aura_enabled;
        const float clamped_dist_m = std::clamp(tick_max_dist_m, 10.0f, 1000.0f);

        outline_cfg.filter(hud::outline::OutlineCategory::Slasher).enabled        = tick_show_antagonist;
        outline_cfg.filter(hud::outline::OutlineCategory::Slasher).max_distance_m = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Camper).enabled         = tick_show_teammates;
        outline_cfg.filter(hud::outline::OutlineCategory::Camper).max_distance_m  = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Generator).enabled      = tick_show_power_nodes;
        outline_cfg.filter(hud::outline::OutlineCategory::Generator).max_distance_m = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Pallet).enabled         = tick_show_barriers;
        outline_cfg.filter(hud::outline::OutlineCategory::Pallet).max_distance_m  = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Window).enabled         = tick_show_barriers;
        outline_cfg.filter(hud::outline::OutlineCategory::Window).max_distance_m  = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Totem).enabled          = tick_show_shrines;
        outline_cfg.filter(hud::outline::OutlineCategory::Totem).max_distance_m   = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Hatch).enabled          = tick_show_portals;
        outline_cfg.filter(hud::outline::OutlineCategory::Hatch).max_distance_m   = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::EscapeDoor).enabled     = tick_show_portals;
        outline_cfg.filter(hud::outline::OutlineCategory::EscapeDoor).max_distance_m = clamped_dist_m;
        outline_cfg.filter(hud::outline::OutlineCategory::Searchable).enabled     = tick_show_containers;
        outline_cfg.filter(hud::outline::OutlineCategory::Searchable).max_distance_m = clamped_dist_m;

        if (is_lobby_level || s_teardown_write_lock_ticks > 0U) {
            hud::outline::get_native_outline_controller().reset_cache_without_writes();
        }
    }

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
            if (s_snapshots_pool[i].priority_rank == 3 &&
                std::isfinite(s_snapshots_pool[i].progress_pct) &&
                s_snapshots_pool[i].progress_pct >= 0.0f)
            {
                ++active_gen_count;
                const float dsq = s_snapshots_pool[i].distance_meters * s_snapshots_pool[i].distance_meters;
                if (dsq < nearest_gen_dist_sq) {
                    nearest_gen_dist_sq = dsq;
                    nearest_gen_pct     = s_snapshots_pool[i].progress_pct;
                }
            }
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

    // Update pointers & live Match State counters (Remaining Nodes & Remaining/Required Generators) in SceneTelemetryData
    size_t   final_node_count     = 0;
    uint32_t final_rem_nodes      = 0;
    bool     final_match_active   = false;
    uint32_t cnt_killer           = 0;
    uint32_t cnt_survivor         = 0;
    uint32_t cnt_generator        = 0;
    uint32_t cnt_completed_gen    = 0;
    uint32_t cnt_totem            = 0;
    uint32_t cnt_chest            = 0;
    uint32_t cnt_pallet           = 0;
    uint32_t cnt_hatch            = 0;
    uint32_t cnt_exitgate         = 0;
    uint32_t cnt_hook             = 0;

    int32_t gs_activated_gens = -1;
    int32_t gs_required_gens  = -1;
    if (local_game_state != 0 &&
        InProcessMemory::IsValidPtr(local_game_state, off.gamestate_required_gens + sizeof(int32_t)))
    {
        gs_activated_gens = mem::read<int32_t>(local_game_state + off.gamestate_activated_gens).value_or(-1);
        gs_required_gens  = mem::read<int32_t>(local_game_state + off.gamestate_required_gens).value_or(-1);
    }

    {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.uworld_ptr        = *uworld;
        hud_data.ulevel_ptr        = *ulevel;
        hud_data.local_pawn        = local_pawn;
        hud_data.skillchecks_handled = s_skillchecks_handled;

        if (is_lobby_level || collected_count == 0) {
            hud_data.match_active         = false;
            hud_data.node_count           = 0;
            hud_data.remaining_nodes      = 0;
            hud_data.active_generators    = 0;
            hud_data.required_generators  = 0;
            hud_data.nearest_gen_progress = 0.0f;
            final_node_count              = 0;
            final_match_active            = false;
        } else {
            final_node_count   = hud_data.node_count;
            final_match_active = hud_data.match_active;
            for (size_t i = 0; i < final_node_count && i < hud::kMaxSceneNodes; ++i) {
                const hud::SceneNode& nd = hud_data.nodes[i];
                switch (nd.category) {
                    case hud::NodeCategory::Antagonist:  ++cnt_killer;    ++final_rem_nodes; break;
                    case hud::NodeCategory::Teammate:    ++cnt_survivor;  ++final_rem_nodes; break;
                    case hud::NodeCategory::PowerNode:
                        ++cnt_generator;
                        if (std::isfinite(nd.progress_pct) && nd.progress_pct >= 99.9f) {
                            ++cnt_completed_gen;
                        } else {
                            ++final_rem_nodes;
                        }
                        break;
                    case hud::NodeCategory::Shrine:      ++cnt_totem;     ++final_rem_nodes; break;
                    case hud::NodeCategory::Container:   ++cnt_chest;     ++final_rem_nodes; break;
                    case hud::NodeCategory::Barrier:     ++cnt_pallet;    ++final_rem_nodes; break;
                    case hud::NodeCategory::Portal:      ++cnt_hatch;     ++final_rem_nodes; break;
                    case hud::NodeCategory::ExitGate:    ++cnt_exitgate;  ++final_rem_nodes; break;
                    case hud::NodeCategory::SpecialProp: ++cnt_hook;      ++final_rem_nodes; break;
                    default: break;
                }
            }

            if (final_match_active) {
                if (gs_required_gens >= 1 && gs_required_gens <= 10) {
                    required_gen_count = static_cast<uint32_t>(gs_required_gens);
                } else if (cnt_generator > 0U) {
                    required_gen_count = (cnt_generator >= 5U) ? 5U : cnt_generator;
                } else {
                    required_gen_count = 0U;
                }

                uint32_t completed_total = cnt_completed_gen;
                if (gs_activated_gens >= 0 && gs_activated_gens <= 10 && cnt_generator < required_gen_count) {
                    completed_total = std::max<uint32_t>(completed_total, static_cast<uint32_t>(gs_activated_gens));
                }
                active_gen_count = (required_gen_count > completed_total)
                    ? (required_gen_count - completed_total)
                    : 0U;

                hud_data.remaining_nodes      = final_rem_nodes;
                hud_data.active_generators    = active_gen_count;
                hud_data.required_generators  = required_gen_count;
                hud_data.nearest_gen_progress = nearest_gen_pct;
            } else {
                hud_data.remaining_nodes      = 0;
                hud_data.active_generators    = 0;
                hud_data.required_generators  = 0;
                hud_data.nearest_gen_progress = 0.0f;
            }
        }
        pthread_mutex_unlock(&hud_data.mutex);
    }

    static uint32_t s_prev_rem_nodes = UINT32_MAX;
    static uint32_t s_prev_rem_gens  = UINT32_MAX;
    static uint32_t s_prev_req_gens  = UINT32_MAX;
    if (final_rem_nodes != s_prev_rem_nodes ||
        active_gen_count != s_prev_rem_gens ||
        required_gen_count != s_prev_req_gens)
    {
        s_prev_rem_nodes = final_rem_nodes;
        s_prev_rem_gens  = active_gen_count;
        s_prev_req_gens  = required_gen_count;
        DIAG_LOGI(
            "[MATCH-COUNTERS] MatchActive=%d | RemainingNodes=%u (TotalRenderNodes=%zu) | Generators=%u/%u (MapGens=%u, Completed=%u, GameState=%d/%d)",
            final_match_active ? 1 : 0,
            final_rem_nodes,
            final_node_count,
            active_gen_count,
            required_gen_count,
            cnt_generator,
            cnt_completed_gen,
            static_cast<int>(gs_activated_gens),
            static_cast<int>(gs_required_gens)
        );
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

    // Authority Gate: runtime host detection via AGameStateBase::Role (+0x00FC == 3) & ADBDPlayerState::GameRole (+0x039A)
    uint8_t gs_local_role = 0U;
    if (local_game_state != 0) {
        if (const auto r = mem::read<uint8_t>(local_game_state + off.actor_local_role)) {
            gs_local_role = *r;
        }
    }
    const bool gs_has_authority = (local_game_state != 0 && gs_local_role == 3U);

    uint8_t ps_game_role = 0U;
    if (local_player_st != 0) {
        if (const auto pr = mem::read<uint8_t>(local_player_st + off.player_state_game_role)) {
            ps_game_role = *pr;
        }
    }

    {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.has_authority = gs_has_authority;
        hud_data.gs_local_role = gs_local_role;
        hud_data.ps_game_role  = ps_game_role;
        pthread_mutex_unlock(&hud_data.mutex);
    }

    static uintptr_t s_auth_logged_ulevel = 0;
    static bool      s_auth_logged_host   = false;
    static uint8_t   s_auth_logged_role   = 255U;
    if (final_match_active &&
        (*ulevel != s_auth_logged_ulevel || gs_has_authority != s_auth_logged_host || ps_game_role != s_auth_logged_role)) {
        s_auth_logged_ulevel = *ulevel;
        s_auth_logged_host   = gs_has_authority;
        s_auth_logged_role   = ps_game_role;
        DIAG_LOGI(
            "[AUTH-GATE] MatchStart ULevel=0x%llX | GameState=0x%llX Role=%u (HasAuthority=%d) | PlayerState=0x%llX GameRole=%u",
            static_cast<unsigned long long>(*ulevel),
            static_cast<unsigned long long>(local_game_state),
            static_cast<unsigned>(gs_local_role),
            gs_has_authority ? 1 : 0,
            static_cast<unsigned long long>(local_player_st),
            static_cast<unsigned>(ps_game_role));
        char auth_msg[192]{};
        std::snprintf(
            auth_msg,
            sizeof(auth_msg),
            "GameState=0x%llX Role=%u HasAuthority=%d PlayerState=0x%llX GameRole=%u",
            static_cast<unsigned long long>(local_game_state),
            static_cast<unsigned>(gs_local_role),
            gs_has_authority ? 1 : 0,
            static_cast<unsigned long long>(local_player_st),
            static_cast<unsigned>(ps_game_role));
        crashlog::CrashLogger::instance().log_action("AUTH-GATE", "MatchStart", off.actor_local_role, auth_msg);
    } else if (!final_match_active) {
        s_auth_logged_ulevel = 0;
        s_auth_logged_role   = 255U;
    }

    uint8_t pawn_local_role = 0U;
    if (local_pawn != 0) {
        if (const auto r = mem::read<uint8_t>(local_pawn + off.actor_local_role)) {
            pawn_local_role = *r;
        }
    }
    // Allow state reconciliation on both ROLE_Authority (3) and ROLE_AutonomousProxy (2)
    const bool authoritative_write_safe =
        write_safe && (gs_has_authority || pawn_local_role == 2U || pawn_local_role == 3U);

    // Execute Item 3: Player Name Override + Read-back Verification Log
    // Works in both Lobby and Active Match whenever APlayerState is a verified live UObject (never on pre-login Start screen)
    if (write_safe && should_apply_player_name && local_player_st != 0 && target_player_name[0] != '\0' &&
        std::strncmp(current_player_name.data(), "localhost", 9) != 0) {
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
            if (std::strcmp(s_last_logged_name, readback_name.data()) != 0) {
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
    if (write_safe && final_match_active && should_apply_kinematics && local_pawn != 0) {
        if (move_chain.is_valid && move_chain.movement_component != 0 && !sync_res.fly_cheat_enabled) {
            (void)ue4::clamp_kinematic_envelope(
                move_chain.movement_component,
                target_walk_speed,
                false,
                0.0f,
                off
            );
        }
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
    } else if (write_safe && final_match_active && !should_apply_kinematics && s_kinematics_was_active && local_pawn != 0) {
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
    } else if (!write_safe || !final_match_active) {
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

    // Execute Advanced Feature 3: Auto Skillcheck (Chaining + Auto-Great), Auto Heal & Trap Immunity
    if (write_safe && final_match_active && local_pawn != 0) {
        if (ue4::SpatialAndKinematicPipeline::apply_auto_skillcheck(
                local_pawn,
                local_pc,
                base,
                guobjectarray_abs,
                off,
                sync_res.auto_skillcheck,
                sync_res.skillcheck_interval_ms,
                nearest_gen_dist_sq,
                nearest_gen_pct)) {
            ++s_skillchecks_handled;
        }
    }
    if (authoritative_write_safe && final_match_active && sync_res.trap_immunity && local_pawn != 0) {
        ue4::SpatialAndKinematicPipeline::apply_trap_immunity(local_pawn, guobjectarray_abs, off);
    }
    if (authoritative_write_safe && final_match_active && sync_res.auto_heal && local_pawn != 0) {
        ue4::SpatialAndKinematicPipeline::apply_auto_heal(local_pawn, local_pc, base, guobjectarray_abs, off);
    }

    // Execute Advanced Feature 4: Bidirectional Fly Cheat & Instant Hatch Escape
    static bool s_was_flying = false;
    if (write_safe && final_match_active && local_pawn != 0 && (sync_res.fly_cheat_enabled || s_was_flying)) {
        static uintptr_t s_last_fly_err_pawn = 0;
        const Status fly_st = ue4::SpatialAndKinematicPipeline::apply_fly_mode(
            local_pc,
            local_pawn,
            guobjectarray_abs,
            off,
            sync_res.fly_cheat_enabled,
            target_walk_speed,
            sync_res.fly_vertical_speed
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
    } else if (!write_safe || !final_match_active) {
        s_was_flying = false;
    }
    if (write_safe && sync_res.force_escape && local_pawn != 0) {
        if (!authoritative_write_safe) {
            DIAG_LOGI(
                "[AUTH-GATE] Blocked apply_force_escape: GameState HasAuthority=0 (Role=%u, GameRole=%u)",
                static_cast<unsigned>(gs_local_role),
                static_cast<unsigned>(ps_game_role));
            crashlog::CrashLogger::instance().log_action(
                "AUTH-GATE",
                "apply_force_escape",
                off.actor_local_role,
                "blocked: not host (HasAuthority=0)");
            hud::set_force_escape(false);
        } else {
            const ue4::FVector* escape_pos = nullptr;
            for (size_t i = 0; i < collected_count; ++i) {
                if (std::strcmp(s_snapshots_pool[i].category_label, "Hatch") == 0) {
                    escape_pos = &s_snapshots_pool[i].world_pos;
                    break;
                }
            }
            if (escape_pos == nullptr) {
                for (size_t i = 0; i < collected_count; ++i) {
                    if (std::strcmp(s_snapshots_pool[i].category_label, "ExitGate") == 0) {
                        escape_pos = &s_snapshots_pool[i].world_pos;
                        break;
                    }
                }
            }
            if (hatch_actor_ptr != 0) {
                (void)ue4::align_and_dispatch_interaction(local_pawn, hatch_actor_ptr, base, off);
            }
            const Status esc_st = ue4::SpatialAndKinematicPipeline::apply_force_escape(
                local_pawn,
                local_player_st,
                hatch_actor_ptr,
                escape_pos,
                base,
                guobjectarray_abs,
                off
            );
            if (esc_st != Status::Ok) {
                DIAG_LOGE(
                    "[WRITE-ERR] apply_force_escape failed (Status=%d) on Pawn=0x%llX PS=0x%llX Hatch=0x%llX",
                    static_cast<int>(esc_st),
                    static_cast<unsigned long long>(local_pawn),
                    static_cast<unsigned long long>(local_player_st),
                    static_cast<unsigned long long>(hatch_actor_ptr));
            }
            hud::set_force_escape(false);
        }
    }

    // Execute Advanced Feature 5: Immobilize State & Player Role Setters (Authoritative & AutonomousProxy)
    if (authoritative_write_safe && sync_res.should_apply_immobilize && local_pawn != 0) {
        (void)ue4::sync_proxy_state_frame(
            local_pawn,
            hud::resolve_immobilize_state_byte(sync_res.immobilize_state),
            ue4::FVector{0.0f, 0.0f, 0.0f},
            0.016f,
            off
        );
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
    if (authoritative_write_safe && sync_res.should_apply_role && local_player_st != 0) {
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
            if (g_state.ue4_module.base_address != 0) {
                ipc::TelemetrySnapshotPayload resp{};
                ipc::fill_snapshot_from_telemetry(resp);
                (void)::send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
            }
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
                crashlog::CrashLogger::instance().set_module_bases(g_state.ue4_module.base_address, 0);
                crashlog::CrashLogger::instance().install_signal_handlers();
                if (memory::InProcessMemory::effective_pid() == ::getpid()) {
                    crashlog::CrashLogger::instance().install_ue4_crash_hooks(g_state.ue4_module.base_address);
                }
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

INSTRUMENT_C_EXPORT void dbdm_start_inprocess_worker() {
    DIAG_LOGI("dbdm_start_inprocess_worker invoked inside com.netease.dbdena (PID=%d); starting headless telemetry worker.",
              static_cast<int>(::getpid()));
    (void)runtime::initialize_instrumentation();
}

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

    bool          gp_native_aura = false;
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
        gp_native_aura = gp_cfg.native_aura_enabled;
        gp_fov_on      = gp_cfg.apply_fov_override;
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
            "native_aura=%d\n"
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
            "cat_mask=%02X\n"
            "crash_log_active=%d\n"
            "crash_log_path=%s\n",
            hud_open ? 1 : 0,
            draw_markers ? 1 : 0,
            gp_native_aura ? 1 : 0,
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
            gp_catmask,
            crashlog::CrashLogger::instance().is_active() ? 1 : 0,
            crashlog::CrashLogger::instance().active_path());
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
        const unsigned rem_nodes = snap.match_active ? static_cast<unsigned>(snap.remaining_nodes) : 0U;
        const unsigned rem_gens  = snap.match_active ? static_cast<unsigned>(snap.active_generators) : 0U;
        const unsigned req_gens  = snap.match_active ? static_cast<unsigned>(snap.required_generators) : 0U;
        std::snprintf(
            buf,
            sizeof(buf),
            "IPC: ONLINE | %s | Nodes: %u | Gens: %u/%u | Auth=%u | Role=%u | Player: %s",
            snap.match_active ? "IN MATCH (ACTIVE)" : "LOBBY / STANDBY",
            rem_nodes,
            rem_gens,
            req_gens,
            snap.match_active ? static_cast<unsigned>(snap.has_authority) : 1U,
            snap.match_active ? static_cast<unsigned>(snap.ps_game_role) : 0U,
            snap.active_player_name[0] != '\0' ? snap.active_player_name : "(In-Game Default)");
    } else {
        std::snprintf(buf, sizeof(buf), "IPC: WAITING FOR GAME (com.netease.dbdena) | Nodes: 0 | Gens: 0/0");
    }
    return env->NewStringUTF(buf);
}

INSTRUMENT_C_EXPORT jstring JNICALL
Java_com_dbdm_modmenu_FloatingModService_nativeStartCrashLogger(
    JNIEnv* env,
    jclass  /*clazz*/,
    jstring preferred_dir,
    jstring fallback_dir) {

    const char* pref_c = preferred_dir ? env->GetStringUTFChars(preferred_dir, nullptr) : "/sdcard/DBDMod/logs";
    const char* fall_c = fallback_dir  ? env->GetStringUTFChars(fallback_dir, nullptr)  : "";

    crashlog::CrashLogger::instance().install_signal_handlers();
    char out_path[crashlog::kMaxPathLen]{};
    const bool ok = crashlog::CrashLogger::instance().start_new_session(
        pref_c ? pref_c : "/sdcard/DBDMod/logs",
        fall_c ? fall_c : "",
        out_path,
        sizeof(out_path));

    if (preferred_dir && pref_c) env->ReleaseStringUTFChars(preferred_dir, pref_c);
    if (fallback_dir  && fall_c) env->ReleaseStringUTFChars(fallback_dir, fall_c);

    if (!ok) {
        return env->NewStringUTF("ERR:Falha ao criar arquivo de log (permissao negada)");
    }
    crashlog::CrashLogger::instance().log_action("TOGGLE", "Iniciar logs", 0, out_path);
    return env->NewStringUTF(out_path);
}

INSTRUMENT_C_EXPORT void JNICALL
Java_com_dbdm_modmenu_FloatingModService_nativeStopCrashLogger(
    JNIEnv* /*env*/,
    jclass  /*clazz*/) {
    crashlog::CrashLogger::instance().stop_session();
}

INSTRUMENT_C_EXPORT void JNICALL
Java_com_dbdm_modmenu_FloatingModService_nativeLogModAction(
    JNIEnv* env,
    jclass  /*clazz*/,
    jstring tag,
    jstring func,
    jlong   offset,
    jstring detail) {
    const char* tag_c    = tag    ? env->GetStringUTFChars(tag, nullptr)    : "TOGGLE";
    const char* func_c   = func   ? env->GetStringUTFChars(func, nullptr)   : "-";
    const char* detail_c = detail ? env->GetStringUTFChars(detail, nullptr) : "-";
    crashlog::CrashLogger::instance().log_action(
        tag_c ? tag_c : "TOGGLE",
        func_c ? func_c : "-",
        static_cast<std::uintptr_t>(offset),
        detail_c ? detail_c : "-");
    if (tag    && tag_c)    env->ReleaseStringUTFChars(tag, tag_c);
    if (func   && func_c)   env->ReleaseStringUTFChars(func, func_c);
    if (detail && detail_c) env->ReleaseStringUTFChars(detail, detail_c);
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
    if (::access("/data/adb/modules/ue4_instrument_core/zygisk/arm64-v8a.so", F_OK) == 0 &&
        ::access("/data/adb/modules/ue4_instrument_core/disable", F_OK) != 0) {
        DIAG_LOGI("Zygisk in-process module is active; standalone dbdm_telemetry_daemon exiting to prevent dual-worker conflict.");
        return 0;
    }
    DIAG_LOGI("Standalone Headless Telemetry Daemon started (PID=%d); monitoring com.netease.dbdena...",
              static_cast<int>(::getpid()));

    g_state.running.store(true, std::memory_order_release);

    pthread_t ipc_tid{};
    if (pthread_create(&ipc_tid, nullptr, ipc_server_worker_entry, nullptr) == 0) {
        pthread_detach(ipc_tid);
    }

    auto reset_telemetry_on_disconnect = []() noexcept {
        hud::SceneTelemetryData& hud_data = hud::get_telemetry_data();
        pthread_mutex_lock(&hud_data.mutex);
        hud_data.match_active         = false;
        hud_data.node_count           = 0;
        hud_data.remaining_nodes      = 0;
        hud_data.active_generators    = 0;
        hud_data.required_generators  = 0;
        hud_data.nearest_gen_progress = 0.0f;
        hud_data.uworld_ptr           = 0;
        hud_data.ulevel_ptr           = 0;
        hud_data.local_pawn           = 0;
        ++hud_data.tick_seq;
        pthread_mutex_unlock(&hud_data.mutex);
        (void)::unlink(ipc::kSharedSnapshotFilePath);
    };

    while (g_state.running.load(std::memory_order_acquire)) {
        const pid_t game_pid = find_game_process_pid();
        if (game_pid <= 0) {
            if (InProcessMemory::effective_pid() != 0 || g_state.ue4_module.base_address != 0) {
                reset_telemetry_on_disconnect();
            }
            InProcessMemory::set_target_pid(0);
            g_state.ue4_module = {};
            g_state.last_logged_uworld = 0;
            g_state.last_logged_ulevel = 0;
            g_state.gnamepool_verified = false;
            sleep_ms(500);
            continue;
        }

        if (InProcessMemory::effective_pid() != game_pid || g_state.ue4_module.base_address == 0) {
            reset_telemetry_on_disconnect();
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
            reset_telemetry_on_disconnect();
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
