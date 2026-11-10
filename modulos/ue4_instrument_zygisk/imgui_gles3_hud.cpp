// File: imgui_gles3_hud.cpp
#include "imgui_gles3_hud.hpp"
#include "imgui_impl_opengl3.h"

#include <android/input.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

namespace hud {

namespace {

struct ScopedMutexLock {
    explicit ScopedMutexLock(pthread_mutex_t* mtx) noexcept : mtx_(mtx) {
        pthread_mutex_lock(mtx_);
    }
    ~ScopedMutexLock() noexcept {
        pthread_mutex_unlock(mtx_);
    }
    ScopedMutexLock(const ScopedMutexLock&)            = delete;
    ScopedMutexLock& operator=(const ScopedMutexLock&) = delete;

private:
    pthread_mutex_t* mtx_;
};

bool g_imgui_initialized = false;

[[nodiscard]] constexpr float deg_to_rad(float degrees) noexcept {
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

[[nodiscard]] bool icontains_label(const char* haystack, const char* needle) noexcept {
    if (!needle || needle[0] == '\0') {
        return true;
    }
    if (!haystack || haystack[0] == '\0') {
        return false;
    }

    const std::size_t n_len = std::strlen(needle);
    const std::size_t h_len = std::strlen(haystack);
    if (n_len > h_len) {
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

} // namespace

SceneTelemetryData& get_telemetry_data() noexcept {
    static SceneTelemetryData instance{};
    return instance;
}

bool project_to_screen(
    const Vec3&       world_pos,
    const CameraView& camera,
    float             screen_width,
    float             screen_height,
    ImVec2&           out_screen_pos) noexcept {

    if (screen_width <= 1.0f || screen_height <= 1.0f) {
        return false;
    }

    const float sp = std::sin(deg_to_rad(camera.rotation.pitch));
    const float cp = std::cos(deg_to_rad(camera.rotation.pitch));
    const float sy = std::sin(deg_to_rad(camera.rotation.yaw));
    const float cy = std::cos(deg_to_rad(camera.rotation.yaw));
    const float sr = std::sin(deg_to_rad(camera.rotation.roll));
    const float cr = std::cos(deg_to_rad(camera.rotation.roll));

    // Orthogonal basis vectors from Euler angles (X-forward, Y-right, Z-up convention)
    const Vec3 axis_forward{
        cp * cy,
        cp * sy,
        sp
    };
    const Vec3 axis_right{
        sr * sp * cy - cr * sy,
        sr * sp * sy + cr * cy,
        -sr * cp
    };
    const Vec3 axis_up{
        -(cr * sp * cy + sr * sy),
        cy * sr - cr * sp * sy,
        cr * cp
    };

    const Vec3 delta = world_pos - camera.location;

    const float view_x = delta.dot(axis_right);
    const float view_y = delta.dot(axis_up);
    const float view_z = delta.dot(axis_forward);

    if (view_z < 1.0f) {
        return false;
    }

    const float clamped_fov  = std::clamp(camera.fov, 10.0f, 170.0f);
    const float half_fov_rad = deg_to_rad(clamped_fov * 0.5f);
    const float tan_half_fov = std::tan(half_fov_rad);
    if (tan_half_fov <= 1e-5f) {
        return false;
    }

    const float center_x     = screen_width * 0.5f;
    const float center_y     = screen_height * 0.5f;
    const float focal_length = center_x / tan_half_fov;

    out_screen_pos.x = center_x + (view_x * focal_length) / view_z;
    out_screen_pos.y = center_y - (view_y * focal_length) / view_z;

    return std::isfinite(out_screen_pos.x) && std::isfinite(out_screen_pos.y);
}

void render_hud_frame(float width, float height) noexcept {
    SceneTelemetryData& data = get_telemetry_data();

    // Snapshot nodes and camera state under lock to minimize contention during draw list submission
    CameraView  cam_snapshot{};
    SceneNode   nodes_snapshot[kMaxSceneNodes]{};
    std::size_t count_snapshot = 0;
    bool        draw_markers   = true;
    float       max_dist_m     = 250.0f;
    char        filter_snapshot[kMaxLabelLen]{};

    {
        ScopedMutexLock lock(&data.mutex);
        cam_snapshot   = data.current_camera;
        count_snapshot = std::min(data.node_count, kMaxSceneNodes);
        for (std::size_t i = 0; i < count_snapshot; ++i) {
            nodes_snapshot[i] = data.nodes[i];
        }
        draw_markers = data.draw_2d_node_markers;
        max_dist_m   = data.max_render_distance_m;
        std::memcpy(filter_snapshot, data.label_filter, kMaxLabelLen);
        filter_snapshot[kMaxLabelLen - 1] = '\0';
    }

    std::size_t visible_markers_rendered = 0;

    // 1. Background 2D Canvas Marker Rendering
    if (draw_markers) {
        ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
        const ImU32 col_green  = IM_COL32(46, 204, 113, 235);
        const ImU32 col_yellow = IM_COL32(241, 196, 15, 235);
        const ImU32 col_blue   = IM_COL32(52, 152, 219, 235);
        const ImU32 col_shadow = IM_COL32(0, 0, 0, 200);

        for (std::size_t i = 0; i < count_snapshot; ++i) {
            const SceneNode& node = nodes_snapshot[i];

            if (node.distance_m > max_dist_m) {
                continue;
            }
            if (!icontains_label(node.label, filter_snapshot)) {
                continue;
            }

            ImVec2 screen_pos{};
            if (!project_to_screen(node.world_pos, cam_snapshot, width, height, screen_pos)) {
                continue;
            }

            if (screen_pos.x < -64.0f || screen_pos.x > width + 64.0f ||
                screen_pos.y < -64.0f || screen_pos.y > height + 64.0f) {
                continue;
            }

            ImU32 marker_color = col_blue;
            if (node.distance_m < 25.0f) {
                marker_color = col_green;
            } else if (node.distance_m < 75.0f) {
                marker_color = col_yellow;
            }

            constexpr float kMarkerRadius = 6.0f;
            bg_draw_list->AddCircleFilled(screen_pos, kMarkerRadius + 1.5f, col_shadow, 16);
            bg_draw_list->AddCircleFilled(screen_pos, kMarkerRadius, marker_color, 16);

            char formatted_tag[128]{};
            std::snprintf(
                formatted_tag,
                sizeof(formatted_tag),
                "%.*s [%.1fm]",
                static_cast<int>(kMaxLabelLen - 1),
                node.label,
                static_cast<double>(node.distance_m));

            const ImVec2 text_pos{screen_pos.x + kMarkerRadius + 5.0f, screen_pos.y - kMarkerRadius};
            bg_draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), col_shadow, formatted_tag);
            bg_draw_list->AddText(text_pos, marker_color, formatted_tag);

            ++visible_markers_rendered;
        }
    }

    // 2. Control Panel HUD Window
    ScopedMutexLock lock(&data.mutex);
    if (!data.hud_open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(520.0f, 620.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GLES3 Scene Telemetry & Control Panel", &data.hud_open)) {
        // Section 1: Telemetry Status Counters
        if (ImGui::CollapsingHeader("1. Telemetry Status Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("Viewport: %.0f x %.0f (%.1f FPS)",
                        static_cast<double>(width),
                        static_cast<double>(height),
                        static_cast<double>(io.Framerate));
            ImGui::Text("Total Active Nodes: %zu / %zu", count_snapshot, kMaxSceneNodes);
            ImGui::Text("Projected Markers Visible: %zu", visible_markers_rendered);
            ImGui::Separator();
            ImGui::Text("Camera Pos: (%.1f, %.1f, %.1f)",
                        static_cast<double>(data.current_camera.location.x),
                        static_cast<double>(data.current_camera.location.y),
                        static_cast<double>(data.current_camera.location.z));
            ImGui::Text("Camera Rot: Pitch %.1f | Yaw %.1f | Roll %.1f | FOV %.1f",
                        static_cast<double>(data.current_camera.rotation.pitch),
                        static_cast<double>(data.current_camera.rotation.yaw),
                        static_cast<double>(data.current_camera.rotation.roll),
                        static_cast<double>(data.current_camera.fov));
        }

        // Section 2: Physics Parameter Sliders
        if (ImGui::CollapsingHeader("2. Kinematics & Physics Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Apply Custom Kinematics", &data.apply_custom_kinematics);
            ImGui::SliderFloat("Walk Speed", &data.walk_speed, 100.0f, 3000.0f, "%.0f u/s");
            ImGui::SliderFloat("Max Acceleration", &data.max_acceleration, 512.0f, 8192.0f, "%.0f u/s^2");
            ImGui::SliderFloat("Jump Velocity", &data.jump_velocity, 200.0f, 2500.0f, "%.0f u/s");
            ImGui::SliderFloat("Time Scale", &data.time_scale, 0.1f, 5.0f, "%.2fx");
        }

        // Section 3: Viewport Marker Settings
        if (ImGui::CollapsingHeader("3. Viewport Marker Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Draw 2D Node Markers", &data.draw_2d_node_markers);
            ImGui::SliderFloat("Max Render Distance", &data.max_render_distance_m, 1.0f, 1000.0f, "%.1f m");
            ImGui::InputText("Label Filter", data.label_filter, sizeof(data.label_filter));
        }
    }
    ImGui::End();
}

void on_egl_frame_swap(EGLDisplay dpy, EGLSurface surface) noexcept {
    if (dpy == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) {
        return;
    }

    EGLint width  = 0;
    EGLint height = 0;
    if (!eglQuerySurface(dpy, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(dpy, surface, EGL_HEIGHT, &height) ||
        width <= 0 || height <= 0) {
        return;
    }

    if (!g_imgui_initialized) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io        = ImGui::GetIO();
        io.IniFilename     = nullptr;
        io.LogFilename     = nullptr;
        io.FontGlobalScale = 1.6f;

        ImGui::StyleColorsDark();
        ImGuiStyle& style      = ImGui::GetStyle();
        style.WindowRounding   = 6.0f;
        style.FrameRounding    = 4.0f;
        style.GrabRounding     = 4.0f;
        style.ScrollbarSize    = 20.0f;
        style.ScaleAllSizes(1.8f);

        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_imgui_initialized = true;
    }

    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    render_hud_frame(static_cast<float>(width), static_cast<float>(height));

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void on_touch_event(int action, float x, float y) noexcept {
    if (!g_imgui_initialized) {
        return;
    }

    ImGuiIO& io      = ImGui::GetIO();
    const int masked = action & AMOTION_EVENT_ACTION_MASK;

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            break;

        case AMOTION_EVENT_ACTION_MOVE:
            io.AddMousePosEvent(x, y);
            break;

        default:
            break;
    }
}

} // namespace hud
