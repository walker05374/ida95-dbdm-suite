// File: scene_collector_helpers.hpp
#pragma once

#include "imgui_gles3_hud.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hud::adapter {

namespace detail_helpers {

// Validates that a raw pointer is non-null and resides in canonical 48-bit user-space
[[nodiscard]] constexpr bool is_readable_string_ptr(const char* ptr) noexcept {
    if (ptr == nullptr) {
        return false;
    }
    const auto raw  = reinterpret_cast<std::uintptr_t>(ptr);
    const auto addr = raw & 0x0000FFFFFFFFFFFFULL;
    return (addr >= 0x00010000ULL && addr <= 0x00007FFFFFFFFFFFULL);
}

[[nodiscard]] constexpr unsigned char ascii_to_lower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

// Bounded, allocation-free, case-insensitive substring check
[[nodiscard]] inline bool icontains_bounded(
    const char* haystack,
    const char* needle,
    std::size_t max_haystack_len = 256) noexcept {

    if (!is_readable_string_ptr(haystack) || !is_readable_string_ptr(needle) || needle[0] == '\0') {
        return false;
    }

    std::size_t h_len = 0;
    while (h_len < max_haystack_len && haystack[h_len] != '\0') {
        ++h_len;
    }

    std::size_t n_len = 0;
    while (n_len < 64 && needle[n_len] != '\0') {
        ++n_len;
    }

    if (n_len == 0 || n_len > h_len) {
        return false;
    }

    for (std::size_t i = 0; i <= h_len - n_len; ++i) {
        bool matched = true;
        for (std::size_t j = 0; j < n_len; ++j) {
            if (ascii_to_lower(static_cast<unsigned char>(haystack[i + j])) !=
                ascii_to_lower(static_cast<unsigned char>(needle[j]))) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline float sanitize_coord(float value, float fallback = 0.0f) noexcept {
    return std::isfinite(value) ? value : fallback;
}

} // namespace detail_helpers

// 1. Case-insensitively maps actor/class names to hud::NodeCategory
[[nodiscard]] inline hud::NodeCategory classify_node_category(const char* class_name) noexcept {
    if (!detail_helpers::is_readable_string_ptr(class_name) || class_name[0] == '\0') {
        return hud::NodeCategory::SpecialProp;
    }

    if (detail_helpers::icontains_bounded(class_name, "Slasher")   ||
        detail_helpers::icontains_bounded(class_name, "Killer")    ||
        detail_helpers::icontains_bounded(class_name, "Assassino") ||
        detail_helpers::icontains_bounded(class_name, "Wraith")    ||
        detail_helpers::icontains_bounded(class_name, "Trapper")   ||
        detail_helpers::icontains_bounded(class_name, "Hillbilly") ||
        detail_helpers::icontains_bounded(class_name, "Nurse")     ||
        detail_helpers::icontains_bounded(class_name, "Huntress")  ||
        detail_helpers::icontains_bounded(class_name, "Spirit")    ||
        detail_helpers::icontains_bounded(class_name, "Myers")     ||
        detail_helpers::icontains_bounded(class_name, "Shape")     ||
        detail_helpers::icontains_bounded(class_name, "Cannibal")  ||
        detail_helpers::icontains_bounded(class_name, "Legion")    ||
        detail_helpers::icontains_bounded(class_name, "Doctor")    ||
        detail_helpers::icontains_bounded(class_name, "Haddie")    ||
        detail_helpers::icontains_bounded(class_name, "Oni")       ||
        detail_helpers::icontains_bounded(class_name, "Blight")    ||
        detail_helpers::icontains_bounded(class_name, "Trickster")) {
        return hud::NodeCategory::Antagonist;
    }

    if (detail_helpers::icontains_bounded(class_name, "Camper")   ||
        detail_helpers::icontains_bounded(class_name, "Survivor") ||
        detail_helpers::icontains_bounded(class_name, "Sobrevivente")) {
        return hud::NodeCategory::Teammate;
    }

    if (detail_helpers::icontains_bounded(class_name, "Generator") ||
        detail_helpers::icontains_bounded(class_name, "Gerador")) {
        return hud::NodeCategory::PowerNode;
    }

    if (detail_helpers::icontains_bounded(class_name, "Totem") ||
        detail_helpers::icontains_bounded(class_name, "Hex")) {
        return hud::NodeCategory::Shrine;
    }

    if (detail_helpers::icontains_bounded(class_name, "Chest")      ||
        detail_helpers::icontains_bounded(class_name, "Searchable") ||
        detail_helpers::icontains_bounded(class_name, "Baú")) {
        return hud::NodeCategory::Container;
    }

    if (detail_helpers::icontains_bounded(class_name, "Pallet") ||
        detail_helpers::icontains_bounded(class_name, "Bookshelf")) {
        return hud::NodeCategory::Barrier;
    }

    if (detail_helpers::icontains_bounded(class_name, "Hatch")      ||
        detail_helpers::icontains_bounded(class_name, "EscapeDoor") ||
        detail_helpers::icontains_bounded(class_name, "ExitGate")   ||
        detail_helpers::icontains_bounded(class_name, "Alçapão")    ||
        detail_helpers::icontains_bounded(class_name, "Portão de Saída")) {
        return hud::NodeCategory::Portal;
    }

    return hud::NodeCategory::SpecialProp;
}

// 2. Rejects NaN/Inf coordinates and unspawned origin templates near (0, 0, 0)
[[nodiscard]] inline bool is_valid_world_coordinate(float x, float y, float z) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }
    if (std::abs(x) < 10.0f && std::abs(y) < 10.0f && std::abs(z) < 10.0f) {
        return false;
    }
    return true;
}

// 3. Generates a 14-joint 3D humanoid stick-figure rig around base_pos rotated by yaw_deg
//    anchored to character capsule bounds (Z - 45.0f to Z + 45.0f) via transform_bone_local_to_world
inline void compute_skeleton_fallback_joints(
    const hud::Vec3& base_pos,
    float            yaw_deg,
    hud::Vec3        (&out_joints)[hud::kSkeletonJointCount]) noexcept {

    const float bx  = detail_helpers::sanitize_coord(base_pos.x, 0.0f);
    const float by  = detail_helpers::sanitize_coord(base_pos.y, 0.0f);
    const float bz  = detail_helpers::sanitize_coord(base_pos.z, 0.0f);
    const float yaw = detail_helpers::sanitize_coord(yaw_deg, 0.0f);

    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float     half_rad  = (yaw * kDegToRad) * 0.5f;

    hud::BoneTransform comp_to_world{};
    comp_to_world.qx          = 0.0f;
    comp_to_world.qy          = 0.0f;
    comp_to_world.qz          = std::sin(half_rad);
    comp_to_world.qw          = std::cos(half_rad);
    comp_to_world.translation = {bx, by, bz};
    comp_to_world.scale       = {1.0f, 1.0f, 1.0f};

    auto offset_joint = [&](float forward_cm, float right_cm, float up_cm) noexcept -> hud::Vec3 {
        return hud::transform_bone_local_to_world(hud::Vec3{forward_cm, right_cm, up_cm}, comp_to_world);
    };

    // Axial centerline joints (within capsule bounds Z - 45.0f .. Z + 45.0f)
    out_joints[static_cast<std::size_t>(hud::BoneIndex::Head)]   = offset_joint(0.0f, 0.0f,  hud::kCapsuleHalfHeightCm);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::Neck)]   = offset_joint(0.0f, 0.0f,  32.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::Spine)]  = offset_joint(0.0f, 0.0f,  15.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::Pelvis)] = offset_joint(0.0f, 0.0f,   0.0f);

    // Upper limbs (Shoulders, Elbows, Hands)
    out_joints[static_cast<std::size_t>(hud::BoneIndex::LShoulder)] = offset_joint(0.0f, -16.0f, 30.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::RShoulder)] = offset_joint(0.0f,  16.0f, 30.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::LElbow)]    = offset_joint(0.0f, -22.0f, 14.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::RElbow)]    = offset_joint(0.0f,  22.0f, 14.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::LHand)]     = offset_joint(4.0f, -24.0f, -2.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::RHand)]     = offset_joint(4.0f,  24.0f, -2.0f);

    // Lower limbs (Knees, Feet at Z - 45.0f capsule base)
    out_joints[static_cast<std::size_t>(hud::BoneIndex::LKnee)] = offset_joint(2.0f, -10.0f, -22.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::RKnee)] = offset_joint(2.0f,  10.0f, -22.0f);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::LFoot)] = offset_joint(3.0f, -11.0f, -hud::kCapsuleHalfHeightCm);
    out_joints[static_cast<std::size_t>(hud::BoneIndex::RFoot)] = offset_joint(3.0f,  11.0f, -hud::kCapsuleHalfHeightCm);
}

inline void compute_skeleton_fallback_joints(
    const hud::Vec3&                             base_pos,
    float                                        yaw_deg,
    std::span<hud::Vec3, hud::kSkeletonJointCount> out_joints) noexcept {

    hud::Vec3 temp[hud::kSkeletonJointCount]{};
    compute_skeleton_fallback_joints(base_pos, yaw_deg, temp);
    for (std::size_t i = 0; i < hud::kSkeletonJointCount; ++i) {
        out_joints[i] = temp[i];
    }
}

} // namespace hud::adapter

namespace hud {
using adapter::classify_node_category;
using adapter::compute_skeleton_fallback_joints;
using adapter::is_valid_world_coordinate;
} // namespace hud

using hud::adapter::classify_node_category;
using hud::adapter::compute_skeleton_fallback_joints;
using hud::adapter::is_valid_world_coordinate;

