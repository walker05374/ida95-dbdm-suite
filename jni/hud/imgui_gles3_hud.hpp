// File: imgui_gles3_hud.hpp
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <cstddef>
#include <cstdint>

#if __has_include("imgui.h")
#include "imgui.h"
#elif __has_include("third_party/imgui/imgui.h")
#include "third_party/imgui/imgui.h"
#else
struct ImVec2 {
    float x = 0.0f;
    float y = 0.0f;
    constexpr ImVec2() noexcept = default;
    constexpr ImVec2(float _x, float _y) noexcept : x(_x), y(_y) {}
};
#endif

namespace hud {

inline constexpr std::size_t kMaxSceneNodes       = 256;
inline constexpr std::size_t kMaxLabelLen         = 64;
inline constexpr std::size_t kSkeletonJointCount  = 14;
inline constexpr std::size_t kBoundingCornerCount = 8;
inline constexpr float       kCapsuleHalfHeightCm = 45.0f;
inline constexpr float       kCapsuleHalfWidthCm  = 28.0f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

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

// Represents a UE4 FTransform (Rotation quaternion + Translation + Scale3D) for USkeletalMeshComponent (+0x288)
struct BoneTransform {
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    Vec3  translation{};
    Vec3  scale{1.0f, 1.0f, 1.0f};
};

// Transforms a bone position from local component space into absolute world space using ComponentToWorld FTransform
[[nodiscard]] constexpr Vec3 transform_bone_local_to_world(
    const Vec3&          local_pos,
    const BoneTransform& comp_to_world) noexcept {
    const float sx = local_pos.x * comp_to_world.scale.x;
    const float sy = local_pos.y * comp_to_world.scale.y;
    const float sz = local_pos.z * comp_to_world.scale.z;

    // t = 2 * cross(q.xyz, s)
    const float tx = 2.0f * (comp_to_world.qy * sz - comp_to_world.qz * sy);
    const float ty = 2.0f * (comp_to_world.qz * sx - comp_to_world.qx * sz);
    const float tz = 2.0f * (comp_to_world.qx * sy - comp_to_world.qy * sx);

    // v' = s + q.w * t + cross(q.xyz, t)
    const float rx = sx + comp_to_world.qw * tx + (comp_to_world.qy * tz - comp_to_world.qz * ty);
    const float ry = sy + comp_to_world.qw * ty + (comp_to_world.qz * tx - comp_to_world.qx * tz);
    const float rz = sz + comp_to_world.qw * tz + (comp_to_world.qx * ty - comp_to_world.qy * tx);

    return {
        comp_to_world.translation.x + rx,
        comp_to_world.translation.y + ry,
        comp_to_world.translation.z + rz
    };
}

// Validates that a transformed world-space bone coordinate is neither collapsed at the world origin
// nor displaced outside the actor's active capsule hitbox bounds.
[[nodiscard]] constexpr bool is_valid_bone_world_pos(
    const Vec3& bone_world,
    const Vec3& actor_world) noexcept {
    const float abs_bx = (bone_world.x < 0.0f) ? -bone_world.x : bone_world.x;
    const float abs_by = (bone_world.y < 0.0f) ? -bone_world.y : bone_world.y;
    const float abs_bz = (bone_world.z < 0.0f) ? -bone_world.z : bone_world.z;
    if ((abs_bx + abs_by + abs_bz) < 5.0f) {
        return false;
    }
    const float dx = bone_world.x - actor_world.x;
    const float dy = bone_world.y - actor_world.y;
    const float dz = bone_world.z - actor_world.z;
    return (dx * dx + dy * dy + dz * dz) <= (220.0f * 220.0f);
}

struct EulerAngles {
    float pitch = 0.0f; // Degrees
    float yaw   = 0.0f; // Degrees
    float roll  = 0.0f; // Degrees
};

using Rotator = EulerAngles;

struct CameraView {
    Vec3        location{};
    EulerAngles rotation{};
    float       fov = 90.0f; // Horizontal FOV in degrees
};

enum class NodeCategory : std::uint8_t {
    Unknown     = 0,
    Antagonist  = 1,
    Teammate    = 2,
    PowerNode   = 3,
    Portal      = 4,
    Container   = 5,
    Shrine      = 6,
    Barrier     = 7,
    ExitGate    = 8,
    SpecialProp = 9
};

// Bone indices for SceneNode::bone_joints[14]:
//   0 = Head, 1 = Neck, 2 = Spine/Chest, 3 = Pelvis,
//   4 = L_Shoulder, 5 = R_Shoulder, 6 = L_Elbow, 7 = R_Elbow,
//   8 = L_Hand, 9 = R_Hand, 10 = L_Knee, 11 = R_Knee,
//   12 = L_Foot, 13 = R_Foot
enum class BoneIndex : std::uint8_t {
    Head       = 0,
    Neck       = 1,
    Spine      = 2,
    Pelvis     = 3,
    LShoulder  = 4,
    RShoulder  = 5,
    LElbow     = 6,
    RElbow     = 7,
    LHand      = 8,
    RHand      = 9,
    LKnee      = 10,
    RKnee      = 11,
    LFoot      = 12,
    RFoot      = 13
};

struct SceneNode {
    Vec3         world_pos{};
    float        distance_m                      = 0.0f;
    NodeCategory category                        = NodeCategory::Unknown;
    float        yaw_deg                         = 0.0f;
    float        progress_pct                    = -1.0f;
    bool         is_open                         = false;
    bool         has_skeleton                    = false;
    Vec3         bone_joints[kSkeletonJointCount]{};
    char         label[kMaxLabelLen]{};
};

struct SceneTelemetryData {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    // 1. Live Telemetry State
    CameraView    current_camera{};
    SceneNode     nodes[kMaxSceneNodes]{};
    std::size_t   node_count           = 0;
    bool          match_active         = true;
    std::uint64_t uworld_ptr           = 0;
    std::uint64_t ulevel_ptr           = 0;
    std::uint64_t local_pawn           = 0;
    std::uint64_t tick_seq             = 0;
    std::uint32_t active_generators    = 0;
    std::uint32_t skillchecks_handled  = 0;
    float         nearest_gen_progress = 0.0f;

    // 2. Physics / Kinematic Percentage Sliders (PT-BR)
    bool  apply_custom_kinematics = false;
    float walk_speed_pct          = 100.0f; // 100% to 250% ("Velocidade de Movimento (%)")
    float accel_pct               = 100.0f; // 100% to 300% ("Aceleração Máxima (%)")
    float jump_pct                = 100.0f; // 100% to 250% ("Força do Pulo (%)")
    float time_scale_pct          = 100.0f; // 50% to 200%  ("Escala de Tempo (%)")

    // Raw kinematic values synchronized from percentage multipliers (for backend compatibility)
    float walk_speed       = 600.0f;
    float max_acceleration = 2048.0f;
    float jump_velocity    = 420.0f;
    float time_scale       = 1.0f;

    // 3. Viewport 3D Category Filters & Player Identity Settings (PT-BR)
    bool  draw_2d_node_markers     = true;
    float max_render_distance_m    = 250.0f;
    bool  show_antagonist          = true; // "Mostrar Assassino (Esqueleto 3D)"
    bool  show_teammates           = true; // "Mostrar Sobreviventes (Esqueleto 3D)"
    bool  show_power_nodes         = true; // "Mostrar Geradores (% + Silhueta 3D)"
    bool  show_shrines             = true; // "Mostrar Totens"
    bool  show_containers          = true; // "Mostrar Baús e Itens"
    bool  show_barriers            = true; // "Mostrar Pallets"
    bool  show_portals             = true; // "Mostrar Alçapão e Portões"
    bool  show_special_props       = true; // "Mostrar Objetos Especiais"
    char  label_filter[kMaxLabelLen]{};

    bool  apply_custom_player_name = false;
    char  custom_player_name[kMaxLabelLen]{};
    char  in_game_player_name[kMaxLabelLen]{};
    bool  hud_open                 = true;
};

// Global thread-safe telemetry state singleton (inline so headless targets and overlay share the definition cleanly)
[[nodiscard]] inline SceneTelemetryData& get_telemetry_data() noexcept {
    static SceneTelemetryData instance{};
    return instance;
}

// Pure 3D-to-2D perspective projection. Returns false if behind camera (view_z < 1.0f).
[[nodiscard]] bool project_to_screen(
    const Vec3&       world_pos,
    const CameraView& camera,
    float             screen_width,
    float             screen_height,
    ImVec2&           out_screen_pos) noexcept;

// Builds Dear ImGui draw lists for background 3D geometric primitives and the PT-BR control panel.
void render_hud_frame(float width, float height) noexcept;

// Queries EGL surface dimensions, lazily initializes ImGui GLES3 context, and renders frame.
void on_egl_frame_swap(EGLDisplay dpy, EGLSurface surface) noexcept;

// Forwards Android MotionEvent actions and coordinates to Dear ImGui IO.
void on_touch_event(int action, float x, float y) noexcept;

// Forwards Android AKeyEvent actions and keycodes/unicode characters to Dear ImGui IO.
void on_key_event(int action, int key_code, int meta_state, int unicode_char = 0) noexcept;

// Returns true when the touch event lies within the active ImGui HUD window or is captured by an active widget.
[[nodiscard]] bool should_consume_touch(int action, float x, float y) noexcept;

// Requests the Android system soft keyboard (InputMethodManager) to show or hide.
void request_soft_keyboard(bool show) noexcept;

} // namespace hud
