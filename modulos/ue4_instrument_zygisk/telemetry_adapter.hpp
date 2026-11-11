// File: telemetry_adapter.hpp
#pragma once

#include "imgui_gles3_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <span>

namespace hud::adapter {

struct RawEntitySample {
    float       x{0.0f};
    float       y{0.0f};
    float       z{0.0f};
    float       dist_m{0.0f};
    const char* name{nullptr};
};

struct KinematicsSyncResult {
    bool  should_apply_kinematics{false};
    float walk_speed{600.0f};
    float max_acceleration{2048.0f};
    float jump_velocity{420.0f};
    float time_scale{1.0f};
    bool  should_apply_player_name{false};
    char  custom_player_name[kMaxLabelLen]{};
};

namespace detail {

struct ScopedPthreadLock {
    explicit ScopedPthreadLock(pthread_mutex_t* mtx) noexcept : mtx_(mtx) {
        pthread_mutex_lock(mtx_);
    }
    ~ScopedPthreadLock() noexcept {
        pthread_mutex_unlock(mtx_);
    }
    ScopedPthreadLock(const ScopedPthreadLock&)            = delete;
    ScopedPthreadLock& operator=(const ScopedPthreadLock&) = delete;

private:
    pthread_mutex_t* mtx_;
};

// Validates that any raw pointer lies in canonical 48-bit AArch64 user-space [0x10000, 0x00007FFFFFFFFFFF]
[[nodiscard]] constexpr bool is_valid_user_ptr(const void* ptr) noexcept {
    if (ptr == nullptr) {
        return false;
    }
    const auto raw  = reinterpret_cast<std::uintptr_t>(ptr);
    const auto addr = raw & 0x0000FFFFFFFFFFFFULL;
    return (addr >= 0x00010000ULL && addr <= 0x00007FFFFFFFFFFFULL);
}

// Bounded, null-safe string length check (never reads past max_scan bytes)
[[nodiscard]] inline std::size_t safe_strnlen(const char* s, std::size_t max_scan = kMaxLabelLen) noexcept {
    if (!is_valid_user_ptr(s)) {
        return 0;
    }
    std::size_t len = 0;
    while (len < max_scan && s[len] != '\0') {
        ++len;
    }
    return len;
}

inline void copy_bounded_label(char (&dest)[kMaxLabelLen], const char* src) noexcept {
    if (!is_valid_user_ptr(src)) {
        std::strncpy(dest, "Unknown", kMaxLabelLen - 1);
        dest[kMaxLabelLen - 1] = '\0';
        return;
    }
    format_friendly_node_label(src, dest);
}

inline void copy_raw_string(char* dest, std::size_t max_len, const char* src) noexcept {
    if (!is_valid_user_ptr(dest) || max_len == 0) {
        return;
    }
    if (!is_valid_user_ptr(src) || src[0] == '\0') {
        dest[0] = '\0';
        return;
    }
    const std::size_t copy_len = safe_strnlen(src, max_len - 1);
    std::memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

[[nodiscard]] constexpr float sanitize_float(float val, float fallback) noexcept {
    return std::isfinite(val) ? val : fallback;
}

[[nodiscard]] inline bool icontains_str(const char* haystack, const char* needle) noexcept {
    if (!is_valid_user_ptr(haystack) || !is_valid_user_ptr(needle) || needle[0] == '\0') {
        return false;
    }
    const std::size_t h_len = safe_strnlen(haystack, 128);
    const std::size_t n_len = safe_strnlen(needle, 64);
    if (n_len == 0 || n_len > h_len) {
        return false;
    }
    auto to_lower = [](unsigned char c) constexpr noexcept -> unsigned char {
        return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
    };
    for (std::size_t i = 0; i <= h_len - n_len; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < n_len; ++j) {
            if (to_lower(static_cast<unsigned char>(haystack[i + j])) !=
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
}

// Excludes non-spawned templates, lobby mannequins, menu controllers, handheld items/consumables, and auxiliary components
[[nodiscard]] inline bool is_ignored_auxiliary_actor(const char* raw_name) noexcept {
    if (!is_valid_user_ptr(raw_name) || raw_name[0] == '\0') {
        return true;
    }
    return icontains_str(raw_name, "Default__")       ||
           icontains_str(raw_name, "Menu")            ||
           icontains_str(raw_name, "Lobby")           ||
           icontains_str(raw_name, "Pawn_")           ||
           icontains_str(raw_name, "Controller")      ||
           icontains_str(raw_name, "PlayerState")     ||
           icontains_str(raw_name, "GameState")       ||
           icontains_str(raw_name, "GameMode")        ||
           icontains_str(raw_name, "Camera")          ||
           icontains_str(raw_name, "AnimInstance")    ||
           icontains_str(raw_name, "Component")       ||
           icontains_str(raw_name, "Collectable")     ||
           icontains_str(raw_name, "Medkit")          ||
           icontains_str(raw_name, "Flashlight")      ||
           icontains_str(raw_name, "Toolbox")         ||
           icontains_str(raw_name, "Firecracker")     ||
           icontains_str(raw_name, "MapItem")         ||
           icontains_str(raw_name, "Key")             ||
           icontains_str(raw_name, "Addon")           ||
           icontains_str(raw_name, "Chargeable")      ||
           icontains_str(raw_name, "Inventory")       ||
           icontains_str(raw_name, "Item")            ||
           icontains_str(raw_name, "Audio")           ||
           icontains_str(raw_name, "Light")           ||
           icontains_str(raw_name, "Volume")          ||
           icontains_str(raw_name, "Trigger")         ||
           icontains_str(raw_name, "Spawner")         ||
           icontains_str(raw_name, "Outline")         ||
           icontains_str(raw_name, "Tracker")         ||
           icontains_str(raw_name, "VFX")             ||
           icontains_str(raw_name, "Particle")        ||
           icontains_str(raw_name, "Decal")           ||
           icontains_str(raw_name, "Interaction")     ||
           icontains_str(raw_name, "Hook")            ||
           icontains_str(raw_name, "Power")           ||
           icontains_str(raw_name, "Projectile")      ||
           icontains_str(raw_name, "Trap")            ||
           icontains_str(raw_name, "Stalker")         ||
           icontains_str(raw_name, "Husk")            ||
           icontains_str(raw_name, "Illusion")        ||
           icontains_str(raw_name, "Phantom")         ||
           icontains_str(raw_name, "Dead")            ||
           icontains_str(raw_name, "Corpse")          ||
           icontains_str(raw_name, "Sacrifice")       ||
           icontains_str(raw_name, "Mori");
}

// Identifies the true spawned Killer character pawn (e.g. BP_Slasher_Character_XX_C)
[[nodiscard]] inline bool is_true_killer_actor(const char* raw_name) noexcept {
    if (!raw_name || is_ignored_auxiliary_actor(raw_name)) {
        return false;
    }
    return icontains_str(raw_name, "Slasher_Character") ||
           icontains_str(raw_name, "BP_Slasher")        ||
           icontains_str(raw_name, "SlasherPlayer")     ||
           icontains_str(raw_name, "Killer_Character")  ||
           icontains_str(raw_name, "Slasher");
}

} // namespace detail

// Primary pointer-based synchronization entry point for 60 Hz worker thread
inline void sync_telemetry_to_hud(
    float                  cam_x,
    float                  cam_y,
    float                  cam_z,
    float                  pitch,
    float                  yaw,
    float                  roll,
    float                  fov,
    const RawEntitySample* entities,
    std::size_t            entity_count,
    float                  current_walk_speed,
    float                  current_accel,
    float                  current_jump_z,
    float                  current_time_scale,
    bool*                  out_should_apply_kinematics,
    float*                 out_walk_speed,
    float*                 out_accel,
    float*                 out_jump_z,
    float*                 out_time_scale,
    const char*            current_player_name          = nullptr,
    bool*                  out_should_apply_player_name = nullptr,
    char*                  out_custom_player_name       = nullptr,
    std::size_t            out_custom_player_name_size  = kMaxLabelLen) noexcept {

    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);

    // 1. Update camera state
    data.current_camera.location = {
        detail::sanitize_float(cam_x, 0.0f),
        detail::sanitize_float(cam_y, 0.0f),
        detail::sanitize_float(cam_z, 0.0f)
    };
    data.current_camera.rotation = {
        detail::sanitize_float(pitch, 0.0f),
        detail::sanitize_float(yaw, 0.0f),
        detail::sanitize_float(roll, 0.0f)
    };
    data.current_camera.fov = std::clamp(detail::sanitize_float(fov, 90.0f), 10.0f, 170.0f);

    // 2. Validate active match state & strictly filter entities (single Killer, clean Chests/Survivors/Generators)
    const bool camera_valid =
        (std::abs(data.current_camera.location.x) > 1.0f ||
         std::abs(data.current_camera.location.y) > 1.0f ||
         std::abs(data.current_camera.location.z) > 1.0f);

    const std::size_t raw_bounded = detail::is_valid_user_ptr(entities)
        ? std::min(entity_count, kMaxSceneNodes)
        : 0;

    // First pass: identify the single best/closest true Killer candidate and count active match objectives
    std::size_t best_killer_index = kMaxSceneNodes + 1;
    int         best_killer_score = -1;
    float       best_killer_dist  = 1e9f;
    std::size_t generator_count   = 0;
    std::size_t character_count   = 0;
    std::size_t objective_count   = 0;

    for (std::size_t i = 0; i < raw_bounded; ++i) {
        const RawEntitySample& src = entities[i];
        if (!detail::is_valid_user_ptr(src.name) || src.name[0] == '\0' || detail::is_ignored_auxiliary_actor(src.name)) {
            continue;
        }
        if (!std::isfinite(src.x) || !std::isfinite(src.y) || !std::isfinite(src.z) || !std::isfinite(src.dist_m)) {
            continue;
        }
        const float sx = src.x;
        const float sy = src.y;
        const float sz = src.z;
        const float sdist = src.dist_m;

        // Reject unspawned origin templates at (0,0,0) or absurd coordinates
        if ((std::abs(sx) < 1.0f && std::abs(sy) < 1.0f && std::abs(sz) < 1.0f) ||
            std::abs(sx) > 500000.0f || std::abs(sy) > 500000.0f || std::abs(sz) > 500000.0f ||
            sdist < 0.5f || sdist > 600.0f) {
            continue;
        }

        char clean_label[kMaxLabelLen]{};
        detail::copy_bounded_label(clean_label, src.name);

        if (std::strcmp(clean_label, "Killer") == 0 && detail::is_true_killer_actor(src.name)) {
            int score = 1;
            if (detail::icontains_str(src.name, "Slasher_Character")) {
                score = 3;
            } else if (detail::icontains_str(src.name, "BP_Slasher")) {
                score = 2;
            }
            if (score > best_killer_score || (score == best_killer_score && sdist < best_killer_dist)) {
                best_killer_score = score;
                best_killer_dist  = sdist;
                best_killer_index = i;
            }
        } else if (std::strcmp(clean_label, "Generator") == 0) {
            ++generator_count;
            ++objective_count;
        } else if (std::strcmp(clean_label, "Survivor") == 0) {
            ++character_count;
        } else if (std::strcmp(clean_label, "Hatch") == 0 ||
                   std::strcmp(clean_label, "Chest") == 0 ||
                   std::strcmp(clean_label, "Totem") == 0) {
            ++objective_count;
        }
    }

    if (best_killer_index < raw_bounded) {
        ++character_count;
    }

    // Lobby / Loading Screen Guard:
    // A real Dead by Daylight Mobile trial has a valid non-zero camera AND active trial objectives
    // (at least 1 generator, or multiple gameplay objectives/characters spawned in the level).
    const bool is_match_live =
        camera_valid &&
        (generator_count >= 1 || (objective_count >= 2 && character_count >= 1) || character_count >= 2);

    data.match_active = is_match_live;

    std::size_t accepted_count = 0;
    if (is_match_live) {
        bool killer_added = false;
        for (std::size_t i = 0; i < raw_bounded && accepted_count < kMaxSceneNodes; ++i) {
            const RawEntitySample& src = entities[i];
            if (!detail::is_valid_user_ptr(src.name) || src.name[0] == '\0' || detail::is_ignored_auxiliary_actor(src.name)) {
                continue;
            }
            if (!std::isfinite(src.x) || !std::isfinite(src.y) || !std::isfinite(src.z) || !std::isfinite(src.dist_m)) {
                continue;
            }

            const float sx    = src.x;
            const float sy    = src.y;
            const float sz    = src.z;
            const float sdist = std::max(0.0f, src.dist_m);

            if ((std::abs(sx) < 1.0f && std::abs(sy) < 1.0f && std::abs(sz) < 1.0f) ||
                std::abs(sx) > 500000.0f || std::abs(sy) > 500000.0f || std::abs(sz) > 500000.0f ||
                sdist < 0.5f || sdist > 600.0f) {
                continue;
            }

            char clean_label[kMaxLabelLen]{};
            detail::copy_bounded_label(clean_label, src.name);

            // Only allow recognized gameplay categories (reject unmapped props/triggers)
            if (std::strcmp(clean_label, "Unknown") == 0) {
                continue;
            }

            // Enforce strict single-Killer rule
            if (std::strcmp(clean_label, "Killer") == 0) {
                if (killer_added || i != best_killer_index) {
                    continue;
                }
                killer_added = true;
            }

            // Deduplicate identical entities at the exact same world coordinates (< 0.75m apart)
            bool is_duplicate = false;
            for (std::size_t j = 0; j < accepted_count; ++j) {
                const float dx = data.nodes[j].world_pos.x - sx;
                const float dy = data.nodes[j].world_pos.y - sy;
                const float dz = data.nodes[j].world_pos.z - sz;
                if ((dx * dx + dy * dy + dz * dz) < (75.0f * 75.0f) &&
                    std::strcmp(data.nodes[j].label, clean_label) == 0) {
                    is_duplicate = true;
                    break;
                }
            }
            if (is_duplicate) {
                continue;
            }

            SceneNode& dst = data.nodes[accepted_count++];
            dst.world_pos  = {sx, sy, sz};
            dst.distance_m = sdist;
            detail::copy_raw_string(dst.label, sizeof(dst.label), clean_label);
        }
    }
    data.node_count = accepted_count;

    // 3. Bidirectional player display name synchronization
    if (data.apply_custom_player_name && data.custom_player_name[0] != '\0') {
        if (out_should_apply_player_name) {
            *out_should_apply_player_name = true;
        }
        detail::copy_raw_string(
            out_custom_player_name, out_custom_player_name_size, data.custom_player_name);
    } else {
        if (current_player_name && current_player_name[0] != '\0' && !data.apply_custom_player_name) {
            detail::copy_raw_string(
                data.custom_player_name, sizeof(data.custom_player_name), current_player_name);
        }
        if (out_should_apply_player_name) {
            *out_should_apply_player_name = false;
        }
        detail::copy_raw_string(
            out_custom_player_name, out_custom_player_name_size, data.custom_player_name);
    }

    // 4. Bidirectional kinematics synchronization
    if (data.apply_custom_kinematics) {
        if (out_should_apply_kinematics) {
            *out_should_apply_kinematics = true;
        }
        if (out_walk_speed) {
            *out_walk_speed = data.walk_speed;
        }
        if (out_accel) {
            *out_accel = data.max_acceleration;
        }
        if (out_jump_z) {
            *out_jump_z = data.jump_velocity;
        }
        if (out_time_scale) {
            *out_time_scale = data.time_scale;
        }
    } else {
        // Keep HUD sliders synchronized with live engine state until overridden
        if (std::isfinite(current_walk_speed) && current_walk_speed > 0.0f) {
            data.walk_speed = std::clamp(current_walk_speed, 100.0f, 3000.0f);
        }
        if (std::isfinite(current_accel) && current_accel > 0.0f) {
            data.max_acceleration = std::clamp(current_accel, 512.0f, 8192.0f);
        }
        if (std::isfinite(current_jump_z) && current_jump_z > 0.0f) {
            data.jump_velocity = std::clamp(current_jump_z, 200.0f, 2500.0f);
        }
        if (std::isfinite(current_time_scale) && current_time_scale > 0.0f) {
            data.time_scale = std::clamp(current_time_scale, 0.1f, 5.0f);
        }

        if (out_should_apply_kinematics) {
            *out_should_apply_kinematics = false;
        }
        if (out_walk_speed) {
            *out_walk_speed = data.walk_speed;
        }
        if (out_accel) {
            *out_accel = data.max_acceleration;
        }
        if (out_jump_z) {
            *out_jump_z = data.jump_velocity;
        }
        if (out_time_scale) {
            *out_time_scale = data.time_scale;
        }
    }
}

// Convenience C++20 std::span overload returning KinematicsSyncResult by value
[[nodiscard]] inline KinematicsSyncResult sync_telemetry_to_hud(
    const CameraView&                camera,
    std::span<const RawEntitySample> entities,
    float                            current_walk_speed,
    float                            current_accel,
    float                            current_jump_z,
    float                            current_time_scale,
    const char*                      current_player_name = nullptr) noexcept {

    KinematicsSyncResult result{};
    sync_telemetry_to_hud(
        camera.location.x,
        camera.location.y,
        camera.location.z,
        camera.rotation.pitch,
        camera.rotation.yaw,
        camera.rotation.roll,
        camera.fov,
        entities.data(),
        entities.size(),
        current_walk_speed,
        current_accel,
        current_jump_z,
        current_time_scale,
        &result.should_apply_kinematics,
        &result.walk_speed,
        &result.max_acceleration,
        &result.jump_velocity,
        &result.time_scale,
        current_player_name,
        &result.should_apply_player_name,
        result.custom_player_name,
        sizeof(result.custom_player_name));
    return result;
}

} // namespace hud::adapter
