// File: diagnostic_tick_bridge.hpp
#pragma once

#include "scene_collector_helpers.hpp"
#include "telemetry_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace hud::adapter {

// Pre-allocated static memory pool capacity (kMaxSceneNodes = 256) for zero-allocation ticking loops
inline constexpr std::size_t kDiagnosticPoolCapacity = hud::kMaxSceneNodes; // 256

struct alignas(64) DiagnosticTickStaticPool {
    std::array<RawEntitySample, kDiagnosticPoolCapacity>                samples{};
    std::array<std::array<char, hud::kMaxLabelLen>, kDiagnosticPoolCapacity> label_storage{};
    std::size_t                                                         active_count{0};

    constexpr void reset() noexcept {
        active_count = 0;
    }

    [[nodiscard]] constexpr std::span<RawEntitySample> active_span() noexcept {
        const std::size_t bounded = (active_count <= kDiagnosticPoolCapacity) ? active_count : kDiagnosticPoolCapacity;
        return std::span<RawEntitySample>{samples.data(), bounded};
    }

    [[nodiscard]] constexpr std::span<const RawEntitySample> active_span() const noexcept {
        const std::size_t bounded = (active_count <= kDiagnosticPoolCapacity) ? active_count : kDiagnosticPoolCapacity;
        return std::span<const RawEntitySample>{samples.data(), bounded};
    }
};

// Returns the singleton pre-allocated static telemetry pool (eliminating heap/stack allocations per tick)
[[nodiscard]] inline DiagnosticTickStaticPool& get_diagnostic_tick_pool() noexcept {
    static DiagnosticTickStaticPool s_pool{};
    return s_pool;
}

// 1. Populates a RawEntitySample from raw actor telemetry and synthesizes 3D skeleton joints for characters
inline void build_raw_entity_sample(
    float             x,
    float             y,
    float             z,
    float             yaw_deg,
    float             dist_m,
    float             progress_pct,
    bool              is_open,
    const char*       name,
    RawEntitySample&  out_sample) noexcept {

    const float safe_x    = std::isfinite(x) ? x : 0.0f;
    const float safe_y    = std::isfinite(y) ? y : 0.0f;
    const float safe_z    = std::isfinite(z) ? z : 0.0f;
    const float safe_yaw  = std::isfinite(yaw_deg) ? yaw_deg : 0.0f;
    const float safe_dist = (std::isfinite(dist_m) && dist_m >= 0.0f) ? dist_m : 0.0f;

    out_sample.x            = safe_x;
    out_sample.y            = safe_y;
    out_sample.z            = safe_z;
    out_sample.dist_m       = safe_dist;
    out_sample.name         = name;
    out_sample.category     = classify_node_category(name);
    out_sample.yaw_deg      = safe_yaw;
    out_sample.progress_pct = progress_pct;
    out_sample.is_open      = is_open;

    if (out_sample.category == hud::NodeCategory::Antagonist ||
        out_sample.category == hud::NodeCategory::Teammate) {
        out_sample.has_skeleton = true;
        compute_skeleton_fallback_joints(hud::Vec3{safe_x, safe_y, safe_z}, safe_yaw, out_sample.bone_joints);
    } else {
        out_sample.has_skeleton = false;
        for (std::size_t i = 0; i < hud::kSkeletonJointCount; ++i) {
            out_sample.bone_joints[i] = {0.0f, 0.0f, 0.0f};
        }
    }
}

// Appends an entity sample into the pre-allocated static pool (up to kMaxSceneNodes = 256)
// copying its label into static label_storage so dangling pointers never occur during hook/cage transitions
[[nodiscard]] inline bool push_static_entity_sample(
    DiagnosticTickStaticPool& pool,
    float                     x,
    float                     y,
    float                     z,
    float                     yaw_deg,
    float                     dist_m,
    float                     progress_pct,
    bool                      is_open,
    const char*               name,
    const hud::Vec3*          custom_bones = nullptr) noexcept {

    if (pool.active_count >= kDiagnosticPoolCapacity) {
        return false;
    }

    const std::size_t idx = pool.active_count++;
    auto&             lbl = pool.label_storage[idx];
    lbl[0] = '\0';
    if (detail_helpers::is_readable_string_ptr(name) && name[0] != '\0') {
        std::strncpy(lbl.data(), name, lbl.size() - 1U);
        lbl[lbl.size() - 1U] = '\0';
    }

    RawEntitySample& dst = pool.samples[idx];
    build_raw_entity_sample(x, y, z, yaw_deg, dist_m, progress_pct, is_open, lbl.data(), dst);

    if (custom_bones != nullptr && is_open) {
        dst.has_skeleton = true;
        for (std::size_t b = 0; b < hud::kSkeletonJointCount; ++b) {
            dst.bone_joints[b] = custom_bones[b];
        }
    }
    return true;
}

// 2. Validates and clamps custom Field of View (FOV) target within [80.0f, 120.0f]
[[nodiscard]] inline bool compute_clamped_fov_target(
    const KinematicsSyncResult& sync,
    float                       current_fov,
    float*                      out_fov) noexcept {

    if (!sync.should_apply_fov ||
        !std::isfinite(current_fov) ||
        current_fov < 20.0f ||
        current_fov > 170.0f ||
        out_fov == nullptr) {
        return false;
    }

    *out_fov = std::clamp(
        std::isfinite(sync.target_fov) ? sync.target_fov : 90.0f,
        80.0f,
        120.0f);
    return true;
}

// 3. Computes Auto-Skillcheck zone overrides (100% Great Skillcheck zone)
inline void compute_skillcheck_zone_override(
    float& out_success_start,
    float& out_success_length,
    float& out_bonus_length) noexcept {

    out_success_start  = 0.0f;
    out_success_length = 1.0f;
    out_bonus_length   = 1.0f;
}

// 4. Resolves target UCharacterMovementComponent::MovementMode byte (MOVE_Flying = 5, MOVE_Walking = 1)
[[nodiscard]] constexpr std::uint8_t resolve_movement_mode_byte(bool fly_enabled) noexcept {
    return fly_enabled
        ? static_cast<std::uint8_t>(MovementMode::Flying)
        : static_cast<std::uint8_t>(MovementMode::Walking);
}

// 5. Dynamic UPlayerController -> AcknowledgedPawn -> UCharacterMovementComponent pointer chain descriptor
struct CharacterMovementChain {
    std::uintptr_t player_controller{0};
    std::uintptr_t acknowledged_pawn{0};
    std::uintptr_t movement_component{0};
    bool           is_valid{false};
    bool           dirty_flag_active{false};
};

// 6. Computes continuous movement dirty / network-reconciliation flags on UCharacterMovementComponent
//    (bNetworkUpdateReceived | bJustTeleported | bCheatFlying) so server reconciliation doesn't revert local tweaks
[[nodiscard]] constexpr std::uint8_t compute_movement_dirty_flags(
    std::uint8_t current_flags,
    bool         kinematics_or_fly_active,
    bool         fly_enabled = false) noexcept {

    if (!kinematics_or_fly_active) {
        return static_cast<std::uint8_t>(current_flags & ~0x04U);
    }
    std::uint8_t out = static_cast<std::uint8_t>(current_flags | 0x03U); // Set continuous dirty + teleported reconciliation bits
    if (fly_enabled) {
        out = static_cast<std::uint8_t>(out | 0x04U); // bCheatFlying
    }
    return out;
}

// 7. Computes AActor::bActorEnableCollision bitfield byte (+0x58: bit 0 = bActorEnableCollision)
[[nodiscard]] constexpr std::uint8_t compute_actor_collision_byte(
    std::uint8_t current_flags,
    bool         enable_collision) noexcept {

    return enable_collision
        ? static_cast<std::uint8_t>(current_flags | 0x01U)
        : static_cast<std::uint8_t>(current_flags & ~0x01U);
}

// 8. Resolves validated UE4 EImmobilizeState byte (0 = None, 1 = Hooked, 2 = Trapped, 3 = Hiding, 4 = Dead, 5 = Caged)
[[nodiscard]] constexpr std::uint8_t resolve_immobilize_state_byte(ImmobilizeState state) noexcept {
    const auto raw = static_cast<std::uint8_t>(state);
    return (raw <= static_cast<std::uint8_t>(ImmobilizeState::Caged)) ? raw : 0U;
}

// Validates that an in-game EImmobilizeState byte is within valid bounds [0..5] and safe to transition
// without corrupting pawn state during hook/cage animations.
[[nodiscard]] constexpr bool is_immobilize_transition_safe(
    std::uint8_t    current_raw_state,
    ImmobilizeState target_state) noexcept {
    if (current_raw_state > static_cast<std::uint8_t>(ImmobilizeState::Caged)) {
        return false;
    }
    if (current_raw_state == static_cast<std::uint8_t>(ImmobilizeState::Dead)) {
        return false;
    }
    const auto target_raw = static_cast<std::uint8_t>(target_state);
    return target_raw <= static_cast<std::uint8_t>(ImmobilizeState::Caged);
}

// 9. Resolves validated UE4 EPlayerRole byte (1 = Camper, 2 = Slasher, 3 = Observer)
[[nodiscard]] constexpr std::uint8_t resolve_player_role_byte(PlayerRole role) noexcept {
    const auto raw = static_cast<std::uint8_t>(role);
    return (raw >= static_cast<std::uint8_t>(PlayerRole::Camper) &&
            raw <= static_cast<std::uint8_t>(PlayerRole::Observer))
        ? raw
        : static_cast<std::uint8_t>(PlayerRole::Camper);
}

} // namespace hud::adapter

namespace hud {
using adapter::DiagnosticTickStaticPool;
using adapter::kDiagnosticPoolCapacity;
using adapter::get_diagnostic_tick_pool;
using adapter::push_static_entity_sample;
using adapter::build_raw_entity_sample;
using adapter::compute_clamped_physics_targets;
using adapter::compute_clamped_fov_target;
using adapter::compute_skillcheck_zone_override;
using adapter::resolve_movement_mode_byte;
using adapter::CharacterMovementChain;
using adapter::compute_movement_dirty_flags;
using adapter::compute_actor_collision_byte;
using adapter::resolve_immobilize_state_byte;
using adapter::is_immobilize_transition_safe;
using adapter::resolve_player_role_byte;
} // namespace hud

using hud::adapter::DiagnosticTickStaticPool;
using hud::adapter::kDiagnosticPoolCapacity;
using hud::adapter::get_diagnostic_tick_pool;
using hud::adapter::push_static_entity_sample;
using hud::adapter::build_raw_entity_sample;
using hud::adapter::compute_clamped_physics_targets;
using hud::adapter::compute_clamped_fov_target;
using hud::adapter::compute_skillcheck_zone_override;
using hud::adapter::resolve_movement_mode_byte;
using hud::adapter::CharacterMovementChain;
using hud::adapter::compute_movement_dirty_flags;
using hud::adapter::compute_actor_collision_byte;
using hud::adapter::resolve_immobilize_state_byte;
using hud::adapter::is_immobilize_transition_safe;
using hud::adapter::resolve_player_role_byte;
