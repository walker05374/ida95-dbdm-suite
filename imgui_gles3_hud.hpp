// File: imgui_gles3_hud.hpp
#pragma once

#include <cstddef>
#include <pthread.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "imgui.h"

namespace hud {

inline constexpr std::size_t kMaxSceneNodes = 64;
inline constexpr std::size_t kMaxLabelLen   = 64;

struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Vec3 operator-(const Vec3& rhs) const noexcept {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }

    constexpr float dot(const Vec3& rhs) const noexcept {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }
};

struct Rotator {
    float pitch{0.0f}; // Degrees
    float yaw{0.0f};   // Degrees
    float roll{0.0f};  // Degrees
};

struct CameraView {
    Vec3    location{};
    Rotator rotation{};
    float   fov{90.0f}; // Horizontal FOV in degrees
};

struct SceneNode {
    Vec3  world_pos{};
    float distance_m{0.0f};
    char  label[kMaxLabelLen]{};
};

struct SceneTelemetryData {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    bool  hud_open                   = true;
    bool  draw_2d_node_markers       = true;
    float max_render_distance_m      = 250.0f;
    char  label_filter[kMaxLabelLen] = "";

    bool  apply_custom_kinematics    = false;
    float walk_speed                 = 600.0f;
    float max_acceleration           = 2048.0f;
    float jump_velocity              = 420.0f;
    float time_scale                 = 1.0f;

    CameraView  current_camera{};
    SceneNode   nodes[kMaxSceneNodes]{};
    std::size_t node_count           = 0;
};

// Global shared telemetry state accessed by provider and render threads
SceneTelemetryData& get_telemetry_data() noexcept;

// Pure 3D-to-2D perspective projection. Returns false if behind camera (view_z < 1.0f).
[[nodiscard]] bool project_to_screen(
    const Vec3&       world_pos,
    const CameraView& camera,
    float             screen_width,
    float             screen_height,
    ImVec2&           out_screen_pos) noexcept;

// Builds Dear ImGui draw lists for background 2D markers and the 3-section control panel.
void render_hud_frame(float width, float height) noexcept;

// Queries EGL surface dimensions, lazily initializes ImGui GLES3 context, and renders frame.
void on_egl_frame_swap(EGLDisplay dpy, EGLSurface surface) noexcept;

// Forwards Android MotionEvent actions and coordinates to Dear ImGui IO.
void on_touch_event(int action, float x, float y) noexcept;

} // namespace hud

