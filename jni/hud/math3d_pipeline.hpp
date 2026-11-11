#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>
#include "imgui.h"

namespace math3d {

struct Vec2 {
    float x{0.0f};
    float y{0.0f};
};

struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& rhs) const noexcept {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& rhs) const noexcept {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }

    [[nodiscard]] constexpr float dot(const Vec3& rhs) const noexcept {
        return (x * rhs.x) + (y * rhs.y) + (z * rhs.z);
    }
};

struct EulerAngles {
    float pitch_deg{0.0f};
    float yaw_deg{0.0f};
    float roll_deg{0.0f};
};

struct CameraState {
    Vec3        position{};
    EulerAngles rotation{};
    float       horizontal_fov_deg{90.0f};
    float       near_clip{1.0f};
};

// ============================================================================
// 1. Yaw Angle Normalization, 2D Planar Rotation Matrix & Z-Rotation Quaternion
// ============================================================================

[[nodiscard]] constexpr float deg_to_rad(float degrees) noexcept {
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

[[nodiscard]] inline float normalize_angle_deg(float angle_deg) noexcept {
    if (!std::isfinite(angle_deg)) {
        return 0.0f;
    }
    float wrapped = std::fmod(angle_deg, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    if (wrapped > 180.0f) {
        wrapped -= 360.0f;
    }
    return wrapped;
}

// 2x2 planar rotation matrix in the XY-plane (Z-up convention)
struct PlanarYawMatrix {
    float cos_yaw{1.0f};
    float sin_yaw{0.0f};

    [[nodiscard]] static PlanarYawMatrix from_yaw_degrees(float yaw_deg) noexcept {
        const float rad = deg_to_rad(normalize_angle_deg(yaw_deg));
        return {std::cos(rad), std::sin(rad)};
    }

    [[nodiscard]] constexpr Vec3 transform_local_to_world(
        const Vec3& local_offset,
        const Vec3& world_origin) const noexcept {
        return {
            world_origin.x + (local_offset.x * cos_yaw - local_offset.y * sin_yaw),
            world_origin.y + (local_offset.x * sin_yaw + local_offset.y * cos_yaw),
            world_origin.z + local_offset.z
        };
    }
};

// Unit quaternion representation for 3D rotations
struct Quaternion {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};

    [[nodiscard]] static Quaternion from_yaw_degrees(float yaw_deg) noexcept {
        const float half_rad = deg_to_rad(normalize_angle_deg(yaw_deg)) * 0.5f;
        return {0.0f, 0.0f, std::sin(half_rad), std::cos(half_rad)};
    }

    [[nodiscard]] constexpr Vec3 rotate(const Vec3& v) const noexcept {
        const float tx = 2.0f * (y * v.z - z * v.y);
        const float ty = 2.0f * (z * v.x - x * v.z);
        const float tz = 2.0f * (x * v.y - y * v.x);
        return {
            v.x + w * tx + (y * tz - z * ty),
            v.y + w * ty + (z * tx - x * tz),
            v.z + w * tz + (x * ty - y * tx)
        };
    }

    [[nodiscard]] constexpr Vec3 transform_local_to_world(
        const Vec3& local_offset,
        const Vec3& world_origin,
        const Vec3& scale = {1.0f, 1.0f, 1.0f}) const noexcept {
        const Vec3 scaled{
            local_offset.x * scale.x,
            local_offset.y * scale.y,
            local_offset.z * scale.z
        };
        return world_origin + rotate(scaled);
    }
};

// ============================================================================
// 2. 8-Corner Upright Bounding Box Generation & World-Space Transformation
// ============================================================================

inline constexpr std::size_t kBoxCornerCount      = 8;
inline constexpr float       kDefaultHalfWidth    = 28.0f;
inline constexpr float       kDefaultHalfHeight   = 45.0f;

[[nodiscard]] inline std::array<Vec3, kBoxCornerCount> compute_world_bounding_box_corners(
    const Vec3& actor_pos,
    float       yaw_deg,
    float       half_width  = kDefaultHalfWidth,
    float       half_height = kDefaultHalfHeight) noexcept {

    const PlanarYawMatrix rot = PlanarYawMatrix::from_yaw_degrees(yaw_deg);

    const std::array<Vec3, kBoxCornerCount> local_corners{{
        {-half_width, -half_width, -half_height},
        { half_width, -half_width, -half_height},
        { half_width,  half_width, -half_height},
        {-half_width,  half_width, -half_height},
        {-half_width, -half_width,  half_height},
        { half_width, -half_width,  half_height},
        { half_width,  half_width,  half_height},
        {-half_width,  half_width,  half_height}
    }};

    std::array<Vec3, kBoxCornerCount> world_corners{};
    for (std::size_t i = 0; i < kBoxCornerCount; ++i) {
        world_corners[i] = rot.transform_local_to_world(local_corners[i], actor_pos);
    }
    return world_corners;
}

// ============================================================================
// 3. Root Foot 3D-to-2D Projection & Dynamic Text Label Alignment
// ============================================================================

struct ProjectedFootAnchor {
    Vec2 screen_foot{};
    Vec2 screen_bottom_mid{};
    Vec2 label_top_left{};
};

[[nodiscard]] inline std::optional<Vec2> project_world_to_screen(
    const Vec3&        world_pos,
    const CameraState& camera,
    float              viewport_width,
    float              viewport_height) noexcept {

    if (viewport_width <= 1.0f || viewport_height <= 1.0f) {
        return std::nullopt;
    }
    if (!std::isfinite(world_pos.x) || !std::isfinite(world_pos.y) || !std::isfinite(world_pos.z)) {
        return std::nullopt;
    }

    const float pitch_rad = deg_to_rad(std::clamp(normalize_angle_deg(camera.rotation.pitch_deg), -89.9f, 89.9f));
    const float yaw_rad   = deg_to_rad(normalize_angle_deg(camera.rotation.yaw_deg));
    const float roll_rad  = deg_to_rad(normalize_angle_deg(camera.rotation.roll_deg));

    const float sp = std::sin(pitch_rad), cp = std::cos(pitch_rad);
    const float sy = std::sin(yaw_rad),   cy = std::cos(yaw_rad);
    const float sr = std::sin(roll_rad),  cr = std::cos(roll_rad);

    const Vec3 axis_forward{cp * cy, cp * sy, sp};
    const Vec3 axis_right{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    const Vec3 axis_up{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};

    const Vec3  delta  = world_pos - camera.position;
    const float view_x = delta.dot(axis_right);
    const float view_y = delta.dot(axis_up);
    const float view_z = delta.dot(axis_forward);

    if (!std::isfinite(view_z) || view_z < camera.near_clip) {
        return std::nullopt;
    }

    const float clamped_fov  = std::clamp(camera.horizontal_fov_deg, 10.0f, 170.0f);
    const float tan_half_fov = std::tan(deg_to_rad(clamped_fov * 0.5f));
    if (tan_half_fov <= 1e-5f) {
        return std::nullopt;
    }

    const float center_x     = viewport_width * 0.5f;
    const float center_y     = viewport_height * 0.5f;
    const float focal_length = center_x / tan_half_fov;

    const float screen_x = center_x + (view_x * focal_length) / view_z;
    const float screen_y = center_y - (view_y * focal_length) / view_z;

    if (!std::isfinite(screen_x) || !std::isfinite(screen_y) ||
        screen_x < 0.0f || screen_x > viewport_width ||
        screen_y < 0.0f || screen_y > viewport_height) {
        return std::nullopt;
    }

    return Vec2{screen_x, screen_y};
}

[[nodiscard]] inline std::optional<ProjectedFootAnchor> compute_foot_anchor_and_label_offset(
    const Vec3&        actor_pos,
    const CameraState& camera,
    float              viewport_width,
    float              viewport_height,
    const Vec2&        measured_text_size,
    float              half_height        = kDefaultHalfHeight,
    float              label_vertical_pad = 4.0f) noexcept {

    const Vec3 world_foot{actor_pos.x, actor_pos.y, actor_pos.z - half_height};

    auto projected = project_world_to_screen(world_foot, camera, viewport_width, viewport_height);
    if (!projected.has_value()) {
        projected = project_world_to_screen(actor_pos, camera, viewport_width, viewport_height);
        if (!projected.has_value()) {
            return std::nullopt;
        }
    }

    ProjectedFootAnchor result{};
    result.screen_foot       = *projected;
    result.screen_bottom_mid = Vec2{viewport_width * 0.5f, viewport_height};
    result.label_top_left    = Vec2{
        projected->x - (measured_text_size.x * 0.5f),
        projected->y + label_vertical_pad
    };
    return result;
}

// ============================================================================
// 4. ImGui Pipeline Integration Wrapper
// ============================================================================

struct LineSegment2D {
    Vec2 start{};
    Vec2 end{};
};

inline constexpr std::size_t kBoxEdgeCount = 12;

struct HudOverlayVertexBatch {
    std::array<Vec2, kBoxCornerCount>        projected_corners{};
    std::array<bool, kBoxCornerCount>        corner_mask{};
    std::size_t                              valid_corner_count{0};
    std::array<LineSegment2D, kBoxEdgeCount> box_edges{};
    std::size_t                              box_edge_count{0};
    bool                                     has_foot_anchor{false};
    LineSegment2D                            tracer_segment{};
    Vec2                                     foot_screen_pos{};
    Vec2                                     label_top_left{};
};

inline constexpr std::array<std::pair<std::size_t, std::size_t>, kBoxEdgeCount> kBoxEdgeIndices{{
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
}};

[[nodiscard]] inline bool apply_math3d_to_hud_pipeline(
    const Vec3&            actor_pos,
    float                  yaw_deg,
    const CameraState&     camera,
    float                  viewport_width,
    float                  viewport_height,
    const Vec2&            measured_text_size,
    HudOverlayVertexBatch& out_batch,
    float                  half_width         = kDefaultHalfWidth,
    float                  half_height        = kDefaultHalfHeight,
    float                  label_vertical_pad = 4.0f) noexcept {
    
    out_batch = HudOverlayVertexBatch{};
    if (viewport_width <= 1.0f || viewport_height <= 1.0f) {
        return false;
    }

    const auto world_corners = compute_world_bounding_box_corners(actor_pos, yaw_deg, half_width, half_height);

    for (std::size_t i = 0; i < kBoxCornerCount; ++i) {
        if (const auto proj = project_world_to_screen(world_corners[i], camera, viewport_width, viewport_height)) {
            out_batch.projected_corners[i] = *proj;
            out_batch.corner_mask[i]       = true;
            ++out_batch.valid_corner_count;
        }
    }

    if (out_batch.valid_corner_count >= 2) {
        for (const auto& [idx_a, idx_b] : kBoxEdgeIndices) {
            if (out_batch.corner_mask[idx_a] && out_batch.corner_mask[idx_b]) {
                out_batch.box_edges[out_batch.box_edge_count++] = LineSegment2D{
                    out_batch.projected_corners[idx_a],
                    out_batch.projected_corners[idx_b]
                };
            }
        }
    }

    if (const auto foot_anchor = compute_foot_anchor_and_label_offset(
            actor_pos, camera, viewport_width, viewport_height, measured_text_size, half_height, label_vertical_pad)) {
        out_batch.has_foot_anchor = true;
        out_batch.foot_screen_pos = foot_anchor->screen_foot;
        out_batch.tracer_segment  = LineSegment2D{
            foot_anchor->screen_bottom_mid,
            foot_anchor->screen_foot
        };
        out_batch.label_top_left  = foot_anchor->label_top_left;
    }

    return (out_batch.box_edge_count > 0) || out_batch.has_foot_anchor;
}

struct ActorHudDescriptor {
    Vec3        actor_pos{};
    float       yaw_deg{0.0f};
    const char* label{nullptr};
    ImU32       color{IM_COL32(255, 255, 255, 255)};
    float       half_width{kDefaultHalfWidth};
    float       half_height{kDefaultHalfHeight};
    bool        draw_tracer{true};
};

inline void integrate_math3d_into_existing_hud(
    std::span<const ActorHudDescriptor> actors,
    const CameraState&                  camera,
    float                               viewport_width,
    float                               viewport_height) noexcept {
    
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list || viewport_width <= 1.0f || viewport_height <= 1.0f) {
        return;
    }

    constexpr ImU32 kShadowColor      = IM_COL32(0, 0, 0, 225);
    constexpr float kBoxEdgeThickness = 1.2f;
    constexpr float kTracerThickness  = 1.5f;

    HudOverlayVertexBatch batch{};
    for (const ActorHudDescriptor& actor : actors) {
        const bool has_label = (actor.label != nullptr && actor.label[0] != '\0');
        const ImVec2 text_bounds = has_label
            ? ImGui::CalcTextSize(actor.label)
            : ImVec2(0.0f, 0.0f);

        if (!apply_math3d_to_hud_pipeline(
                actor.actor_pos,
                actor.yaw_deg,
                camera,
                viewport_width,
                viewport_height,
                Vec2{text_bounds.x, text_bounds.y},
                batch,
                actor.half_width,
                actor.half_height)) {
            continue;
        }

        for (std::size_t i = 0; i < batch.box_edge_count; ++i) {
            const LineSegment2D& edge = batch.box_edges[i];
            draw_list->AddLine(
                ImVec2(edge.start.x, edge.start.y),
                ImVec2(edge.end.x, edge.end.y),
                actor.color,
                kBoxEdgeThickness
            );
        }

        if (batch.has_foot_anchor) {
            if (actor.draw_tracer) {
                draw_list->AddLine(
                    ImVec2(batch.tracer_segment.start.x, batch.tracer_segment.start.y),
                    ImVec2(batch.tracer_segment.end.x, batch.tracer_segment.end.y),
                    actor.color,
                    kTracerThickness
                );
            }
            if (has_label) {
                const ImVec2 pos(batch.label_top_left.x, batch.label_top_left.y);
                draw_list->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), kShadowColor, actor.label);
                draw_list->AddText(pos, actor.color, actor.label);
            }
        }
    }
}

} // namespace math3d