// File: telemetry_adapter.hpp
#pragma once

#include "imgui_gles3_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
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

inline void copy_bounded_label(char (&dest)[kMaxLabelLen], const char* src) noexcept {
    if (!src || src[0] == '\0') {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, kMaxLabelLen - 1);
    dest[kMaxLabelLen - 1] = '\0';
}

[[nodiscard]] constexpr float sanitize_float(float val, float fallback) noexcept {
    return std::isfinite(val) ? val : fallback;
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
    float*                 out_time_scale) noexcept {

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

    // 2. Copy up to kMaxSceneNodes (64) entities into HUD buffer
    const std::size_t bounded_count = (entities != nullptr)
        ? std::min(entity_count, kMaxSceneNodes)
        : 0;

    for (std::size_t i = 0; i < bounded_count; ++i) {
        const RawEntitySample& src = entities[i];
        SceneNode&             dst = data.nodes[i];

        dst.world_pos = {
            detail::sanitize_float(src.x, 0.0f),
            detail::sanitize_float(src.y, 0.0f),
            detail::sanitize_float(src.z, 0.0f)
        };
        dst.distance_m = std::max(0.0f, detail::sanitize_float(src.dist_m, 0.0f));
        detail::copy_bounded_label(dst.label, src.name);
    }
    data.node_count = bounded_count;

    // 3. Bidirectional kinematics synchronization
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
    float                            current_time_scale) noexcept {

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
        &result.time_scale);
    return result;
}

} // namespace hud::adapter

