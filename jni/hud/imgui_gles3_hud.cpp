// File: imgui_gles3_hud.cpp
#include "imgui_gles3_hud.hpp"
#include "math3d_pipeline.hpp"
#include "imgui_impl_opengl3.h"

#include <android/input.h>
#include <android/keycodes.h>
#include <dlfcn.h>
#include <jni.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <span>

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

struct TouchInputState {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    float           raw_x{0.0f};
    float           raw_y{0.0f};
    float           x{0.0f};
    float           y{0.0f};
    float           egl_width{0.0f};
    float           egl_height{0.0f};
    float           phys_width{0.0f};
    float           phys_height{0.0f};
    float           scale_x{1.0f};
    float           scale_y{1.0f};
    float           offset_x{0.0f};
    float           offset_y{0.0f};
    bool            metrics_resolved{false};
    bool            is_down{false};
    bool            has_event{false};
    bool            pending_tap_release{false};
    bool            captured_by_hud{false};
    ImVec2          win_pos{24.0f, 24.0f};
    ImVec2          win_size{560.0f, 700.0f};
    bool            want_capture_mouse{false};
};

TouchInputState   g_touch{};
std::atomic<bool> g_imgui_initialized{false};
std::atomic<bool> g_last_want_text_input{false};
bool              g_show_touch_keypad{false};
bool              g_keypad_uppercase{false};

struct QueuedKeyEvent {
    int action;
    int key_code;
    int meta_state;
    int unicode_char;
};

struct KeyEventQueue {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    QueuedKeyEvent  events[32]{};
    std::size_t     count{0};
};

KeyEventQueue g_key_queue{};

struct QueuedTouchEvent {
    float raw_x{0.0f};
    float raw_y{0.0f};
    bool  is_down{false};
};

struct TouchEventQueue {
    pthread_mutex_t  mutex = PTHREAD_MUTEX_INITIALIZER;
    QueuedTouchEvent events[64]{};
    std::size_t      count{0};
};

TouchEventQueue g_touch_queue{};

inline constexpr float kNearClipPlaneUnits = 1.0f;

[[nodiscard]] constexpr float deg_to_rad(float degrees) noexcept {
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

[[nodiscard]] inline float normalize_euler_angle(float angle_deg) noexcept {
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

[[nodiscard]] constexpr bool point_in_rect(
    float         px,
    float         py,
    const ImVec2& rect_pos,
    const ImVec2& rect_size) noexcept {
    constexpr float kTouchPadding = 8.0f;
    return (px >= (rect_pos.x - kTouchPadding)) &&
           (px <= (rect_pos.x + rect_size.x + kTouchPadding)) &&
           (py >= (rect_pos.y - kTouchPadding)) &&
           (py <= (rect_pos.y + rect_size.y + kTouchPadding));
}

[[nodiscard]] constexpr bool is_valid_cstr_ptr(const char* ptr) noexcept {
    if (ptr == nullptr) {
        return false;
    }
    const auto raw  = reinterpret_cast<std::uintptr_t>(ptr);
    const auto addr = raw & 0x0000FFFFFFFFFFFFULL;
    return (addr >= 0x00010000ULL && addr <= 0x00007FFFFFFFFFFFULL);
}

[[nodiscard]] inline std::size_t bounded_strnlen(const char* s, std::size_t max_len = kMaxLabelLen) noexcept {
    if (!is_valid_cstr_ptr(s)) {
        return 0;
    }
    std::size_t n = 0;
    while (n < max_len && s[n] != '\0') {
        ++n;
    }
    return n;
}

[[nodiscard]] bool icontains_label(const char* haystack, const char* needle) noexcept {
    if (!is_valid_cstr_ptr(needle) || needle[0] == '\0') {
        return true;
    }
    if (!is_valid_cstr_ptr(haystack) || haystack[0] == '\0') {
        return false;
    }

    const std::size_t n_len = bounded_strnlen(needle, kMaxLabelLen);
    const std::size_t h_len = bounded_strnlen(haystack, 128);
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

[[nodiscard]] inline bool is_nonzero_vec3(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           (std::abs(v.x) > 1e-3f || std::abs(v.y) > 1e-3f || std::abs(v.z) > 1e-3f);
}

// Rotates a local (dx, dy, dz) offset around the Z-axis by yaw_deg and adds it to center
[[nodiscard]] inline Vec3 rotate_local_offset(
    const Vec3& center,
    float       dx,
    float       dy,
    float       dz,
    float       cos_yaw,
    float       sin_yaw) noexcept {
    return Vec3{
        center.x + (dx * cos_yaw - dy * sin_yaw),
        center.y + (dx * sin_yaw + dy * cos_yaw),
        center.z + dz
    };
}

// Draws a centered text tag with a crisp 1px drop shadow below a given 2D anchor point
inline void draw_centered_label_below(
    ImDrawList*   draw_list,
    const ImVec2& anchor_bottom,
    ImU32         text_color,
    const char*   text) noexcept {
    if (!draw_list || !text || text[0] == '\0') {
        return;
    }
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 pos{
        anchor_bottom.x - (text_size.x * 0.5f),
        anchor_bottom.y + 4.0f
    };
    const ImU32 shadow_col = IM_COL32(0, 0, 0, 225);
    draw_list->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow_col, text);
    draw_list->AddText(pos, text_color, text);
}

// Resolves effective NodeCategory from node.category and/or node.label
[[nodiscard]] inline NodeCategory resolve_node_category(const SceneNode& node) noexcept {
    if (icontains_label(node.label, "Killer") ||
        icontains_label(node.label, "Slasher") ||
        icontains_label(node.label, "Assassino")) {
        return NodeCategory::Antagonist;
    }
    if (icontains_label(node.label, "Survivor") ||
        icontains_label(node.label, "Camper") ||
        icontains_label(node.label, "Sobrevivente")) {
        return NodeCategory::Teammate;
    }
    if (icontains_label(node.label, "Generator") ||
        icontains_label(node.label, "Gerador")) {
        return NodeCategory::PowerNode;
    }
    if (icontains_label(node.label, "Totem") ||
        icontains_label(node.label, "HexSpawner")) {
        return NodeCategory::Shrine;
    }
    if (icontains_label(node.label, "Chest") ||
        icontains_label(node.label, "Searchable") ||
        icontains_label(node.label, "Bau") ||
        icontains_label(node.label, "Baú")) {
        return NodeCategory::Container;
    }
    if (icontains_label(node.label, "Pallet") ||
        icontains_label(node.label, "Bookshelf")) {
        return NodeCategory::Barrier;
    }
    if (icontains_label(node.label, "Hatch") ||
        icontains_label(node.label, "ExitGate") ||
        icontains_label(node.label, "EscapeDoor") ||
        icontains_label(node.label, "Alcapao") ||
        icontains_label(node.label, "Alçapão") ||
        icontains_label(node.label, "Portao") ||
        icontains_label(node.label, "Portão")) {
        return NodeCategory::Portal;
    }
    return node.category;
}

// Queries Android DisplayMetrics (both Display.getRealMetrics and Resources.getSystem().getDisplayMetrics)
// to resolve the exact physical window resolution corresponding to AMotionEvent coordinates.
inline bool query_android_display_metrics(
    float  egl_w,
    float  egl_h,
    float& out_phys_w,
    float& out_phys_h) noexcept {

    using GetCreatedJavaVMsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto get_vms = reinterpret_cast<GetCreatedJavaVMsFn>(
        ::dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    if (!get_vms) {
        if (void* art_handle = ::dlopen("libart.so", RTLD_LAZY | RTLD_NOLOAD)) {
            get_vms = reinterpret_cast<GetCreatedJavaVMsFn>(
                ::dlsym(art_handle, "JNI_GetCreatedJavaVMs"));
        }
    }
    if (!get_vms) {
        return false;
    }

    JavaVM* vm       = nullptr;
    jsize   vm_count = 0;
    if (get_vms(&vm, 1, &vm_count) != JNI_OK || !vm || vm_count <= 0) {
        return false;
    }

    JNIEnv* env      = nullptr;
    bool    attached = false;
    const jint env_stat = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (env_stat == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
            return false;
        }
        attached = true;
    } else if (env_stat != JNI_OK || !env) {
        return false;
    }

    auto clear_jni_exc = [env]() noexcept -> bool {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return true;
        }
        return false;
    };

    int real_w = 0;
    int real_h = 0;
    int sys_w  = 0;
    int sys_h  = 0;

    if (env->PushLocalFrame(16) == JNI_OK) {
        jclass dm_cls = env->FindClass("android/util/DisplayMetrics");
        if (!clear_jni_exc() && dm_cls) {
            jmethodID dm_ctor = env->GetMethodID(dm_cls, "<init>", "()V");
            jfieldID  w_fid   = env->GetFieldID(dm_cls, "widthPixels", "I");
            jfieldID  h_fid   = env->GetFieldID(dm_cls, "heightPixels", "I");

            if (!clear_jni_exc() && dm_ctor && w_fid && h_fid) {
                jobject dm_obj = env->NewObject(dm_cls, dm_ctor);
                if (!clear_jni_exc() && dm_obj) {
                    jclass at_cls = env->FindClass("android/app/ActivityThread");
                    if (!clear_jni_exc() && at_cls) {
                        jmethodID cur_app_mid = env->GetStaticMethodID(
                            at_cls, "currentApplication", "()Landroid/app/Application;");
                        if (!clear_jni_exc() && cur_app_mid) {
                            jobject app_obj = env->CallStaticObjectMethod(at_cls, cur_app_mid);
                            if (!clear_jni_exc() && app_obj) {
                                jclass ctx_cls = env->FindClass("android/content/Context");
                                jmethodID get_svc_mid = ctx_cls
                                    ? env->GetMethodID(ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;")
                                    : nullptr;
                                if (!clear_jni_exc() && get_svc_mid) {
                                    jstring win_str = env->NewStringUTF("window");
                                    jobject wm_obj  = env->CallObjectMethod(app_obj, get_svc_mid, win_str);
                                    if (!clear_jni_exc() && wm_obj) {
                                        jclass wm_cls = env->FindClass("android/view/WindowManager");
                                        jmethodID get_disp_mid = wm_cls
                                            ? env->GetMethodID(wm_cls, "getDefaultDisplay", "()Landroid/view/Display;")
                                            : nullptr;
                                        if (!clear_jni_exc() && get_disp_mid) {
                                            jobject disp_obj = env->CallObjectMethod(wm_obj, get_disp_mid);
                                            if (!clear_jni_exc() && disp_obj) {
                                                jclass disp_cls = env->FindClass("android/view/Display");
                                                jmethodID real_mid = disp_cls
                                                    ? env->GetMethodID(disp_cls, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V")
                                                    : nullptr;
                                                if (!clear_jni_exc() && real_mid) {
                                                    env->CallVoidMethod(disp_obj, real_mid, dm_obj);
                                                    if (!clear_jni_exc()) {
                                                        real_w = env->GetIntField(dm_obj, w_fid);
                                                        real_h = env->GetIntField(dm_obj, h_fid);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                jclass res_cls = env->FindClass("android/content/res/Resources");
                if (!clear_jni_exc() && res_cls) {
                    jmethodID get_sys_mid = env->GetStaticMethodID(
                        res_cls, "getSystem", "()Landroid/content/res/Resources;");
                    jmethodID get_dm_mid  = env->GetMethodID(
                        res_cls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
                    if (!clear_jni_exc() && get_sys_mid && get_dm_mid) {
                        jobject res_obj = env->CallStaticObjectMethod(res_cls, get_sys_mid);
                        if (!clear_jni_exc() && res_obj) {
                            jobject sys_dm = env->CallObjectMethod(res_obj, get_dm_mid);
                            if (!clear_jni_exc() && sys_dm) {
                                sys_w = env->GetIntField(sys_dm, w_fid);
                                sys_h = env->GetIntField(sys_dm, h_fid);
                            }
                        }
                    }
                }
            }
        }
        env->PopLocalFrame(nullptr);
    }

    if (attached) {
        vm->DetachCurrentThread();
    }

    const bool is_landscape = (egl_w >= egl_h);
    auto orient_pair = [is_landscape](int w, int h, float& out_w, float& out_h) noexcept {
        if (is_landscape) {
            out_w = static_cast<float>(std::max(w, h));
            out_h = static_cast<float>(std::min(w, h));
        } else {
            out_w = static_cast<float>(std::min(w, h));
            out_h = static_cast<float>(std::max(w, h));
        }
    };

    float cand_real_w = 0.0f, cand_real_h = 0.0f;
    float cand_sys_w  = 0.0f, cand_sys_h  = 0.0f;
    if (real_w > 0 && real_h > 0) {
        orient_pair(real_w, real_h, cand_real_w, cand_real_h);
    }
    if (sys_w > 0 && sys_h > 0) {
        orient_pair(sys_w, sys_h, cand_sys_w, cand_sys_h);
    }

    if (cand_real_w > 0.0f && cand_sys_w > 0.0f && egl_h > 0.0f) {
        const float egl_aspect  = egl_w / egl_h;
        const float real_aspect = cand_real_w / cand_real_h;
        const float sys_aspect  = cand_sys_w / cand_sys_h;
        if (std::abs(sys_aspect - egl_aspect) + 0.01f < std::abs(real_aspect - egl_aspect)) {
            out_phys_w = cand_sys_w;
            out_phys_h = cand_sys_h;
        } else {
            out_phys_w = cand_real_w;
            out_phys_h = cand_real_h;
        }
        return true;
    }

    if (cand_real_w > 0.0f && cand_real_h > 0.0f) {
        out_phys_w = cand_real_w;
        out_phys_h = cand_real_h;
        return true;
    }
    if (cand_sys_w > 0.0f && cand_sys_h > 0.0f) {
        out_phys_w = cand_sys_w;
        out_phys_h = cand_sys_h;
        return true;
    }
    return false;
}

inline void update_touch_scaling_locked(float egl_w, float egl_h) noexcept {
    if (egl_w <= 1.0f || egl_h <= 1.0f) {
        return;
    }

    const bool size_changed =
        (std::abs(g_touch.egl_width - egl_w) > 0.5f) ||
        (std::abs(g_touch.egl_height - egl_h) > 0.5f);

    if (size_changed || !g_touch.metrics_resolved) {
        g_touch.egl_width  = egl_w;
        g_touch.egl_height = egl_h;

        float phys_w = 0.0f;
        float phys_h = 0.0f;
        if (query_android_display_metrics(egl_w, egl_h, phys_w, phys_h) &&
            phys_w > 1.0f && phys_h > 1.0f) {
            g_touch.phys_width       = phys_w;
            g_touch.phys_height      = phys_h;
            g_touch.scale_x          = egl_w / phys_w;
            g_touch.scale_y          = egl_h / phys_h;
            g_touch.metrics_resolved = true;
        } else if (g_touch.phys_width <= 1.0f || g_touch.phys_height <= 1.0f) {
            g_touch.phys_width  = egl_w;
            g_touch.phys_height = egl_h;
            g_touch.scale_x     = 1.0f;
            g_touch.scale_y     = 1.0f;
        }
    }
}

} // namespace

bool project_to_screen(
    const Vec3&       world_pos,
    const CameraView& camera,
    float             screen_width,
    float             screen_height,
    ImVec2&           out_screen_pos) noexcept {

    out_screen_pos = ImVec2(-1.0f, -1.0f);

    if (screen_width <= 1.0f || screen_height <= 1.0f) {
        return false;
    }
    if (!std::isfinite(world_pos.x) || !std::isfinite(world_pos.y) || !std::isfinite(world_pos.z) ||
        !std::isfinite(camera.location.x) || !std::isfinite(camera.location.y) || !std::isfinite(camera.location.z)) {
        return false;
    }

    // Normalize camera Euler angles into [-180, 180] (with pitch clamped to [-89.9, 89.9] to avoid gimbal flip)
    const float pitch_deg = std::clamp(normalize_euler_angle(camera.rotation.pitch), -89.9f, 89.9f);
    const float yaw_deg   = normalize_euler_angle(camera.rotation.yaw);
    const float roll_deg  = normalize_euler_angle(camera.rotation.roll);

    const float pitch_rad = deg_to_rad(pitch_deg);
    const float yaw_rad   = deg_to_rad(yaw_deg);
    const float roll_rad  = deg_to_rad(roll_deg);

    const float sp = std::sin(pitch_rad);
    const float cp = std::cos(pitch_rad);
    const float sy = std::sin(yaw_rad);
    const float cy = std::cos(yaw_rad);
    const float sr = std::sin(roll_rad);
    const float cr = std::cos(roll_rad);

    // Orthonormal camera basis vectors (UE4 FRotationMatrix convention: X-forward, Y-right, Z-up)
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

    // Strict near-plane guard: discard any vertex behind or too close to the camera plane
    if (!std::isfinite(view_z) || view_z < kNearClipPlaneUnits) {
        return false;
    }

    const float raw_fov      = std::isfinite(camera.fov) ? camera.fov : 90.0f;
    const float clamped_fov  = std::clamp(raw_fov, 10.0f, 170.0f);
    const float half_fov_rad = deg_to_rad(clamped_fov * 0.5f);
    const float tan_half_fov = std::tan(half_fov_rad);
    if (tan_half_fov <= 1e-5f) {
        return false;
    }

    const float center_x     = screen_width * 0.5f;
    const float center_y     = screen_height * 0.5f;
    const float focal_length = center_x / tan_half_fov;

    const float proj_x = center_x + (view_x * focal_length) / view_z;
    const float proj_y = center_y - (view_y * focal_length) / view_z;

    if (!std::isfinite(proj_x) || !std::isfinite(proj_y)) {
        return false;
    }

    // Strict viewport frustum guard
    if (proj_x < 0.0f || proj_x > screen_width ||
        proj_y < 0.0f || proj_y > screen_height) {
        return false;
    }

    out_screen_pos.x = proj_x;
    out_screen_pos.y = proj_y;
    return true;
}

void render_hud_frame(float width, float height) noexcept {
    SceneTelemetryData& data = get_telemetry_data();

    // Snapshot nodes, camera, and category visibility under lock using pre-allocated static pool
    CameraView                                  cam_snapshot{};
    static std::array<SceneNode, kMaxSceneNodes> s_nodes_snapshot{};
    std::size_t                                 count_snapshot      = 0;
    bool                                        match_live_snapshot = false;
    bool                                        draw_markers        = true;
    float                                       max_dist_m          = 250.0f;

    bool show_antagonist    = true;
    bool show_teammates     = true;
    bool show_power_nodes   = true;
    bool show_shrines       = true;
    bool show_containers    = true;
    bool show_barriers      = true;
    bool show_portals       = true;
    bool show_special_props = true;

    {
        ScopedMutexLock lock(&data.mutex);
        cam_snapshot        = data.current_camera;
        match_live_snapshot = data.match_active;
        count_snapshot      = std::min(data.node_count, kMaxSceneNodes);
        for (std::size_t i = 0; i < count_snapshot; ++i) {
            s_nodes_snapshot[i] = data.nodes[i];
        }
        draw_markers       = data.draw_2d_node_markers;
        max_dist_m         = data.max_render_distance_m;
        show_antagonist    = data.show_antagonist;
        show_teammates     = data.show_teammates;
        show_power_nodes   = data.show_power_nodes;
        show_shrines       = data.show_shrines;
        show_containers    = data.show_containers;
        show_barriers      = data.show_barriers;
        show_portals       = data.show_portals;
        show_special_props = data.show_special_props;
    }

    std::size_t visible_markers_rendered = 0;

    // 1. Minimalist Outlined Bounding Box (1.5px stroke, min 14x28px, no fill/lines/skeletons) + Label Pipeline
    if (draw_markers && match_live_snapshot && count_snapshot > 0) {
        ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();

        // Color Palette
        const ImU32 col_antagonist   = IM_COL32(255, 42, 42, 255);   // Red #FF2A2A
        const ImU32 col_teammate     = IM_COL32(0, 229, 255, 255);   // Cyan/Blue #00E5FF
        const ImU32 col_power_node   = IM_COL32(46, 204, 113, 255);  // Green #2ECC71
        const ImU32 col_shrine       = IM_COL32(241, 196, 15, 255);  // Yellow #F1C40F
        const ImU32 col_barrier      = IM_COL32(255, 241, 118, 255); // Light Yellow/Gold #FFF176
        const ImU32 col_container    = IM_COL32(255, 152, 0, 255);   // Orange #FF9800
        const ImU32 col_portal       = IM_COL32(224, 64, 251, 255);  // Purple #E040FB
        const ImU32 col_special_prop = IM_COL32(180, 210, 240, 230); // Subtle Light Blue

        bool antagonist_rendered = false;

        for (std::size_t i = 0; i < count_snapshot; ++i) {
            const SceneNode& node = s_nodes_snapshot[i];

            if (!std::isfinite(node.distance_m) || node.distance_m > max_dist_m) {
                continue;
            }

            const NodeCategory cat = resolve_node_category(node);

            // Check category visibility filter
            bool category_enabled = false;
            switch (cat) {
                case NodeCategory::Antagonist:  category_enabled = show_antagonist;    break;
                case NodeCategory::Teammate:    category_enabled = show_teammates;     break;
                case NodeCategory::PowerNode:   category_enabled = show_power_nodes;   break;
                case NodeCategory::Shrine:      category_enabled = show_shrines;       break;
                case NodeCategory::Container:   category_enabled = show_containers;    break;
                case NodeCategory::Barrier:     category_enabled = show_barriers;      break;
                case NodeCategory::Portal:      category_enabled = show_portals;       break;
                case NodeCategory::SpecialProp: category_enabled = show_special_props; break;
            }
            if (!category_enabled) {
                continue;
            }

            if (cat == NodeCategory::Antagonist) {
                if (antagonist_rendered) {
                    continue;
                }
                antagonist_rendered = true;
            }

            const int dist_int = static_cast<int>(std::lround(std::max(0.0f, node.distance_m)));

            ImU32 primary_col = col_special_prop;
            char  tag[128]{};

            if (cat == NodeCategory::Antagonist) {
                primary_col = col_antagonist;
                std::snprintf(tag, sizeof(tag), "assassino [%dm]", dist_int);
            } else if (cat == NodeCategory::Teammate) {
                primary_col = col_teammate;
                const bool has_custom_label =
                    (node.label[0] != '\0' &&
                     std::strcmp(node.label, "Survivor") != 0 &&
                     std::strcmp(node.label, "Sobrevivente") != 0);
                const char* display_name = has_custom_label ? node.label : "sobrevivente";
                std::snprintf(tag, sizeof(tag), "%s [%dm]", display_name, dist_int);
            } else if (cat == NodeCategory::PowerNode) {
                if (std::isfinite(node.progress_pct) && node.progress_pct >= 99.5f) {
                    continue;
                }
                primary_col = col_power_node;
                const float clamped_pct = std::clamp(
                    std::isfinite(node.progress_pct) && node.progress_pct >= 0.0f ? node.progress_pct : 0.0f,
                    0.0f,
                    100.0f);
                std::snprintf(
                    tag,
                    sizeof(tag),
                    "gerador %.0f%% [%dm]",
                    static_cast<double>(clamped_pct),
                    dist_int);
            } else if (cat == NodeCategory::Shrine) {
                if (!std::isfinite(node.progress_pct) ||
                    node.progress_pct < 0.0f ||
                    icontains_label(node.label, "Destroyed") ||
                    icontains_label(node.label, "Cleansed") ||
                    icontains_label(node.label, "Broken")) {
                    continue;
                }
                primary_col = col_shrine;
                std::snprintf(tag, sizeof(tag), "totem [%dm]", dist_int);
            } else if (cat == NodeCategory::Barrier) {
                if (!std::isfinite(node.progress_pct) ||
                    node.progress_pct < 0.0f ||
                    icontains_label(node.label, "Window") ||
                    icontains_label(node.label, "Vault") ||
                    icontains_label(node.label, "Janela") ||
                    icontains_label(node.label, "Broken") ||
                    icontains_label(node.label, "Destroyed") ||
                    icontains_label(node.label, "Fallen") ||
                    icontains_label(node.label, "Illusion") ||
                    icontains_label(node.label, "Fake")) {
                    continue;
                }
                primary_col = col_barrier;
                std::snprintf(tag, sizeof(tag), "pallet [%dm]", dist_int);
            } else if (cat == NodeCategory::Container) {
                primary_col = col_container;
                std::snprintf(tag, sizeof(tag), "bau [%dm]", dist_int);
            } else if (cat == NodeCategory::Portal) {
                primary_col = col_portal;
                const bool is_hatch = icontains_label(node.label, "Hatch") ||
                                      icontains_label(node.label, "Alcapao") ||
                                      icontains_label(node.label, "Alçapão");
                std::snprintf(
                    tag,
                    sizeof(tag),
                    "%s [%dm]",
                    is_hatch ? "alcapao" : "portao",
                    dist_int);
            } else {
                primary_col = col_special_prop;
                const char* prop_name = (node.label[0] != '\0') ? node.label : "objeto";
                std::snprintf(tag, sizeof(tag), "%s [%dm]", prop_name, dist_int);
            }

            ImVec2 screen_center{};
            if (!project_to_screen(node.world_pos, cam_snapshot, width, height, screen_center)) {
                continue;
            }

            ImVec2 screen_foot = screen_center;
            ImVec2 screen_head = screen_center;
            const bool has_foot = project_to_screen(
                Vec3{node.world_pos.x, node.world_pos.y, node.world_pos.z - kCapsuleHalfHeightCm},
                cam_snapshot,
                width,
                height,
                screen_foot);
            const bool has_head = project_to_screen(
                Vec3{node.world_pos.x, node.world_pos.y, node.world_pos.z + kCapsuleHalfHeightCm},
                cam_snapshot,
                width,
                height,
                screen_head);

            const ImVec2 anchor_foot = has_foot ? screen_foot : screen_center;
            const float raw_box_h = (has_foot && has_head)
                ? std::abs(screen_foot.y - screen_head.y)
                : ((width * 0.45f) / std::max(1.0f, node.distance_m));
            const float box_h  = std::clamp(raw_box_h, 28.0f, 450.0f);
            const float box_w  = std::max(14.0f, box_h * 0.5f);
            const float half_w = box_w * 0.5f;

            bg_draw_list->AddRect(
                ImVec2(anchor_foot.x - half_w, anchor_foot.y - box_h),
                ImVec2(anchor_foot.x + half_w, anchor_foot.y),
                primary_col,
                0.0f,
                0,
                1.5f);

            draw_centered_label_below(bg_draw_list, anchor_foot, primary_col, tag);
            ++visible_markers_rendered;
        }
    }

    // 2. Control Panel HUD Window (Fully Localized in Brazilian Portuguese - PT-BR)
    ScopedMutexLock lock(&data.mutex);
    if (!data.hud_open) {
        ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(210.0f, 64.0f), ImGuiCond_Always);
        if (ImGui::Begin(
                "##DBDMFloatingTogglePill",
                nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
            if (ImGui::Button("Abrir Painel 3D", ImVec2(-1.0f, -1.0f))) {
                data.hud_open = true;
            }
        }
        const ImVec2 pill_pos  = ImGui::GetWindowPos();
        const ImVec2 pill_size = ImGui::GetWindowSize();
        ImGui::End();

        ScopedMutexLock touch_lock(&g_touch.mutex);
        g_touch.win_pos            = pill_pos;
        g_touch.win_size           = pill_size;
        g_touch.want_capture_mouse = point_in_rect(g_touch.x, g_touch.y, pill_pos, pill_size);
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(580.0f, 740.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Painel de Telemetria e Visualização 3D (GLES3)", &data.hud_open)) {
        // Section 1: Telemetry Status Counters (PT-BR)
        if (ImGui::CollapsingHeader("1. Status da Telemetria e Câmera", ImGuiTreeNodeFlags_DefaultOpen)) {
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("Viewport EGL: %.0f x %.0f (%.1f FPS)",
                        static_cast<double>(width),
                        static_cast<double>(height),
                        static_cast<double>(io.Framerate));
            ImGui::Text("Escala de Toque: %.3f x %.3f (Físico: %.0f x %.0f)",
                        static_cast<double>(g_touch.scale_x),
                        static_cast<double>(g_touch.scale_y),
                        static_cast<double>(g_touch.phys_width),
                        static_cast<double>(g_touch.phys_height));
            ImGui::Text("Nome do Jogador: %s",
                        data.custom_player_name[0] != '\0' ? data.custom_player_name : "(Padrão do Jogo)");
            ImGui::Text("Estado da Partida: %s | Nós Ativos: %zu / %zu",
                        match_live_snapshot ? "EM PARTIDA (ATIVO)" : "LOBBY / AGUARDANDO",
                        count_snapshot,
                        kMaxSceneNodes);
            ImGui::Text("Elementos 3D Renderizados: %zu", visible_markers_rendered);
            ImGui::Separator();
            ImGui::Text("Posição da Câmera: (%.1f, %.1f, %.1f)",
                        static_cast<double>(data.current_camera.location.x),
                        static_cast<double>(data.current_camera.location.y),
                        static_cast<double>(data.current_camera.location.z));
            ImGui::Text("Rotação da Câmera: Pitch %.1f | Yaw %.1f | Roll %.1f | FOV %.1f",
                        static_cast<double>(normalize_euler_angle(data.current_camera.rotation.pitch)),
                        static_cast<double>(normalize_euler_angle(data.current_camera.rotation.yaw)),
                        static_cast<double>(normalize_euler_angle(data.current_camera.rotation.roll)),
                        static_cast<double>(data.current_camera.fov));
        }

        // Section 2: Percentage Physics Sliders (PT-BR)
        if (ImGui::CollapsingHeader("2. Parâmetros Físicos e Cinemática (%)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Ativar Cinemática Personalizada", &data.apply_custom_kinematics);

            ImGui::SliderFloat("Velocidade de Movimento (%)", &data.walk_speed_pct, 100.0f, 250.0f, "%.0f%%");
            ImGui::SliderFloat("Aceleração Máxima (%)", &data.accel_pct, 100.0f, 300.0f, "%.0f%%");
            ImGui::SliderFloat("Força do Pulo (%)", &data.jump_pct, 100.0f, 250.0f, "%.0f%%");
            ImGui::SliderFloat("Escala de Tempo (%)", &data.time_scale_pct, 50.0f, 200.0f, "%.0f%%");

            // Keep raw physical units synchronized with percentage sliders
            data.walk_speed       = 600.0f  * (data.walk_speed_pct / 100.0f);
            data.max_acceleration = 2048.0f * (data.accel_pct / 100.0f);
            data.jump_velocity    = 420.0f  * (data.jump_pct / 100.0f);
            data.time_scale       = data.time_scale_pct / 100.0f;
        }

        // Section 3: 3D Category Filters & Player Identity (PT-BR)
        if (ImGui::CollapsingHeader("3. Filtros Visuais 3D e Identidade", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Ativar Indicadores e Linhas Guia", &data.draw_2d_node_markers);
            ImGui::SliderFloat("Distância Máxima de Renderização", &data.max_render_distance_m, 10.0f, 1000.0f, "%.0f m");

            ImGui::Separator();
            ImGui::Text("Categorias de Entidades 3D:");
            ImGui::Checkbox("Mostrar Assassino (Esqueleto 3D)", &data.show_antagonist);
            ImGui::Checkbox("Mostrar Sobreviventes (Esqueleto 3D)", &data.show_teammates);
            ImGui::Checkbox("Mostrar Geradores (% + Silhueta 3D)", &data.show_power_nodes);
            ImGui::Checkbox("Mostrar Totens", &data.show_shrines);
            ImGui::Checkbox("Mostrar Baús e Itens", &data.show_containers);
            ImGui::Checkbox("Mostrar Pallets", &data.show_barriers);
            ImGui::Checkbox("Mostrar Alçapão e Portões", &data.show_portals);
            ImGui::Checkbox("Mostrar Objetos Especiais", &data.show_special_props);

            ImGui::Separator();
            ImGui::Checkbox("Substituir Nome do Jogador", &data.apply_custom_player_name);
            ImGui::InputText("Nome de Exibição", data.custom_player_name, sizeof(data.custom_player_name));
            if (ImGui::IsItemActivated() || ImGui::IsItemClicked()) {
                g_show_touch_keypad = true;
                request_soft_keyboard(true);
            }

            ImGui::Spacing();
            if (ImGui::Button("Teclado do Sistema (IME)")) {
                request_soft_keyboard(true);
            }
            ImGui::SameLine();
            if (ImGui::Button(g_show_touch_keypad ? "Ocultar Teclado Virtual" : "Mostrar Teclado Virtual")) {
                g_show_touch_keypad = !g_show_touch_keypad;
            }

            if (g_show_touch_keypad) {
                char*             target_buf = data.custom_player_name;
                const std::size_t max_cap    = sizeof(data.custom_player_name);

                auto append_char = [target_buf, max_cap](char ch) noexcept {
                    const std::size_t len = std::strlen(target_buf);
                    if (len + 1 < max_cap) {
                        target_buf[len]     = ch;
                        target_buf[len + 1] = '\0';
                    }
                };

                ImGui::Separator();
                ImGui::Text("Editando [Nome de Exibição]: %s_", target_buf);

                static constexpr const char* kRowsLower[4] = {
                    "1234567890",
                    "qwertyuiop",
                    "asdfghjkl_",
                    "zxcvbnm-."
                };
                static constexpr const char* kRowsUpper[4] = {
                    "1234567890",
                    "QWERTYUIOP",
                    "ASDFGHJKL_",
                    "ZXCVBNM-."
                };

                const char* const* rows = g_keypad_uppercase ? kRowsUpper : kRowsLower;
                for (int r = 0; r < 4; ++r) {
                    const char* row_str = rows[r];
                    for (int c = 0; row_str[c] != '\0'; ++c) {
                        if (c > 0) {
                            ImGui::SameLine(0.0f, 4.0f);
                        }
                        char btn_lbl[8]{row_str[c], '\0'};
                        if (ImGui::Button(btn_lbl, ImVec2(40.0f, 38.0f))) {
                            append_char(row_str[c]);
                        }
                    }
                }

                if (ImGui::Button(g_keypad_uppercase ? "MAIÚSC: ATIVO" : "MAIÚSC: INATIVO", ImVec2(125.0f, 38.0f))) {
                    g_keypad_uppercase = !g_keypad_uppercase;
                }
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Button("ESPAÇO", ImVec2(105.0f, 38.0f))) {
                    append_char(' ');
                }
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Button("APAGAR", ImVec2(105.0f, 38.0f))) {
                    const std::size_t len = std::strlen(target_buf);
                    if (len > 0) {
                        target_buf[len - 1] = '\0';
                    }
                }
                ImGui::SameLine(0.0f, 6.0f);
                if (ImGui::Button("LIMPAR", ImVec2(90.0f, 38.0f))) {
                    target_buf[0] = '\0';
                }
            }
        }
    }

    const bool want_text_now  = ImGui::GetIO().WantTextInput;
    const bool prev_want_text = g_last_want_text_input.exchange(want_text_now, std::memory_order_acq_rel);
    if (want_text_now && !prev_want_text) {
        request_soft_keyboard(true);
    }

    const ImVec2 cur_win_pos  = ImGui::GetWindowPos();
    const ImVec2 cur_win_size = ImGui::GetWindowSize();
    const bool   any_hovered  = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_AnyWindow |
        ImGuiHoveredFlags_ChildWindows |
        ImGuiHoveredFlags_AllowWhenBlockedByPopup |
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool   any_active   = ImGui::IsAnyItemActive() || ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);

    ImGui::End();

    {
        ScopedMutexLock touch_lock(&g_touch.mutex);
        g_touch.win_pos  = cur_win_pos;
        g_touch.win_size = cur_win_size;
        const bool inside_bounds = point_in_rect(g_touch.x, g_touch.y, cur_win_pos, cur_win_size);
        if (inside_bounds || any_hovered || any_active) {
            ImGui::GetIO().WantCaptureMouse = true;
        }
        g_touch.want_capture_mouse = ImGui::GetIO().WantCaptureMouse || inside_bounds;
    }
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

    const float f_width  = static_cast<float>(width);
    const float f_height = static_cast<float>(height);

    if (!g_imgui_initialized.load(std::memory_order_acquire)) {
        ScopedMutexLock touch_lock(&g_touch.mutex);
        if (!g_imgui_initialized.load(std::memory_order_relaxed)) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            ImGuiIO& io        = ImGui::GetIO();
            io.IniFilename     = nullptr;
            io.LogFilename     = nullptr;
            io.ConfigFlags    |= ImGuiConfigFlags_NavEnableSetMousePos;
            io.FontGlobalScale = 1.6f;

            ImGui::StyleColorsDark();
            ImGuiStyle& style       = ImGui::GetStyle();
            style.WindowRounding    = 6.0f;
            style.FrameRounding     = 4.0f;
            style.GrabRounding      = 4.0f;
            style.ScrollbarSize     = 20.0f;
            style.TouchExtraPadding = ImVec2(6.0f, 6.0f);
            style.ScaleAllSizes(1.8f);

            ImGui_ImplOpenGL3_Init("#version 300 es");
            g_imgui_initialized.store(true, std::memory_order_release);
        }
    }

    ImGuiIO& io                = ImGui::GetIO();
    io.DisplaySize             = ImVec2(f_width, f_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // Synchronize physical-to-EGL touch scaling and drain queued touch events into ImGuiIO
    {
        ScopedMutexLock touch_lock(&g_touch.mutex);
        update_touch_scaling_locked(f_width, f_height);

        ScopedMutexLock queue_lock(&g_touch_queue.mutex);
        if (g_touch_queue.count > 0) {
            for (std::size_t i = 0; i < g_touch_queue.count; ++i) {
                const QueuedTouchEvent& ev = g_touch_queue.events[i];
                const float mapped_x = (ev.raw_x - g_touch.offset_x) * g_touch.scale_x;
                const float mapped_y = (ev.raw_y - g_touch.offset_y) * g_touch.scale_y;
                g_touch.x = mapped_x;
                g_touch.y = mapped_y;

                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(mapped_x, mapped_y);
                io.AddMouseButtonEvent(0, ev.is_down);

                const bool inside_hud = point_in_rect(mapped_x, mapped_y, g_touch.win_pos, g_touch.win_size);
                if (g_touch.captured_by_hud || inside_hud) {
                    io.WantCaptureMouse = true;
                }
            }
            g_touch_queue.count = 0;

            if (g_touch.pending_tap_release) {
                g_touch.is_down             = false;
                g_touch.pending_tap_release = false;
            }
        } else if (g_touch.has_event) {
            g_touch.x = (g_touch.raw_x - g_touch.offset_x) * g_touch.scale_x;
            g_touch.y = (g_touch.raw_y - g_touch.offset_y) * g_touch.scale_y;

            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(g_touch.x, g_touch.y);
            io.AddMouseButtonEvent(0, g_touch.is_down);

            if (g_touch.pending_tap_release) {
                g_touch.is_down             = false;
                g_touch.pending_tap_release = false;
            }
        }
    }

    // Drain any queued Android AKeyEvent / soft-keyboard events into ImGuiIO
    {
        ScopedMutexLock key_lock(&g_key_queue.mutex);
        for (std::size_t i = 0; i < g_key_queue.count; ++i) {
            const QueuedKeyEvent& ev = g_key_queue.events[i];
            const bool is_down = (ev.action == AKEY_EVENT_ACTION_DOWN);
            const bool shift   = (ev.meta_state & AMETA_SHIFT_ON) != 0;

            if (ev.key_code == AKEYCODE_DEL) {
                io.AddKeyEvent(ImGuiKey_Backspace, is_down);
            } else if (ev.key_code == AKEYCODE_ENTER || ev.key_code == AKEYCODE_NUMPAD_ENTER) {
                io.AddKeyEvent(ImGuiKey_Enter, is_down);
            } else if (ev.key_code == AKEYCODE_ESCAPE || ev.key_code == AKEYCODE_BACK) {
                io.AddKeyEvent(ImGuiKey_Escape, is_down);
            } else if (ev.key_code == AKEYCODE_DPAD_LEFT) {
                io.AddKeyEvent(ImGuiKey_LeftArrow, is_down);
            } else if (ev.key_code == AKEYCODE_DPAD_RIGHT) {
                io.AddKeyEvent(ImGuiKey_RightArrow, is_down);
            }

            if (is_down) {
                if (ev.unicode_char > 0) {
                    io.AddInputCharacter(static_cast<unsigned int>(ev.unicode_char));
                } else if (ev.key_code >= AKEYCODE_A && ev.key_code <= AKEYCODE_Z) {
                    const char base = shift ? 'A' : 'a';
                    io.AddInputCharacter(static_cast<unsigned int>(base + (ev.key_code - AKEYCODE_A)));
                } else if (ev.key_code >= AKEYCODE_0 && ev.key_code <= AKEYCODE_9) {
                    io.AddInputCharacter(static_cast<unsigned int>('0' + (ev.key_code - AKEYCODE_0)));
                } else if (ev.key_code == AKEYCODE_SPACE) {
                    io.AddInputCharacter(' ');
                } else if (ev.key_code == AKEYCODE_MINUS) {
                    io.AddInputCharacter(shift ? '_' : '-');
                } else if (ev.key_code == AKEYCODE_PERIOD) {
                    io.AddInputCharacter('.');
                }
            }
        }
        g_key_queue.count = 0;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    render_hud_frame(f_width, f_height);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void on_touch_event(int action, float x, float y) noexcept {
    const int masked  = action & AMOTION_EVENT_ACTION_MASK;
    bool      is_down = false;
    bool      valid   = false;

    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
        case AMOTION_EVENT_ACTION_MOVE:
            is_down = true;
            valid   = true;
            break;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            is_down = false;
            valid   = true;
            break;

        default:
            break;
    }

    if (!valid || !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }

    ScopedMutexLock touch_lock(&g_touch.mutex);

    if (g_touch.egl_width > 1.0f && g_touch.egl_height > 1.0f) {
        if (x > g_touch.phys_width) {
            g_touch.phys_width = x;
            g_touch.scale_x    = g_touch.egl_width / g_touch.phys_width;
        }
        if (y > g_touch.phys_height) {
            g_touch.phys_height = y;
            g_touch.scale_y     = g_touch.egl_height / g_touch.phys_height;
        }
    }

    const float mapped_x = (x - g_touch.offset_x) * g_touch.scale_x;
    const float mapped_y = (y - g_touch.offset_y) * g_touch.scale_y;

    const bool was_down = g_touch.is_down;
    g_touch.raw_x       = x;
    g_touch.raw_y       = y;
    g_touch.x           = mapped_x;
    g_touch.y           = mapped_y;
    g_touch.has_event   = true;

    const bool inside_hud = point_in_rect(mapped_x, mapped_y, g_touch.win_pos, g_touch.win_size);
    if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        g_touch.captured_by_hud     = inside_hud || g_touch.want_capture_mouse;
        g_touch.is_down             = true;
        g_touch.pending_tap_release = false;
    } else if (masked == AMOTION_EVENT_ACTION_MOVE) {
        if (inside_hud) {
            g_touch.captured_by_hud = true;
        }
        g_touch.is_down = true;
    } else {
        if (was_down) {
            g_touch.pending_tap_release = true;
        }
        g_touch.is_down = false;
    }

    ScopedMutexLock queue_lock(&g_touch_queue.mutex);
    if (g_touch_queue.count < 64) {
        g_touch_queue.events[g_touch_queue.count++] = QueuedTouchEvent{
            x,
            y,
            is_down
        };
    }
}

bool should_consume_touch(int action, float x, float y) noexcept {
    ScopedMutexLock touch_lock(&g_touch.mutex);
    const float mapped_x   = (x - g_touch.offset_x) * g_touch.scale_x;
    const float mapped_y   = (y - g_touch.offset_y) * g_touch.scale_y;
    const bool  inside_hud = point_in_rect(mapped_x, mapped_y, g_touch.win_pos, g_touch.win_size);
    const int   masked     = action & AMOTION_EVENT_ACTION_MASK;

    if (masked == AMOTION_EVENT_ACTION_UP ||
        masked == AMOTION_EVENT_ACTION_POINTER_UP ||
        masked == AMOTION_EVENT_ACTION_CANCEL) {
        const bool consume = g_touch.captured_by_hud || inside_hud || g_touch.want_capture_mouse;
        g_touch.captured_by_hud = false;
        return consume;
    }

    return g_touch.captured_by_hud || inside_hud || g_touch.want_capture_mouse;
}

void on_key_event(int action, int key_code, int meta_state, int unicode_char) noexcept {
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
        return;
    }

    ScopedMutexLock key_lock(&g_key_queue.mutex);
    if (g_key_queue.count < 32) {
        g_key_queue.events[g_key_queue.count++] = QueuedKeyEvent{
            action,
            key_code,
            meta_state,
            unicode_char
        };
    }

    if (action == AKEY_EVENT_ACTION_DOWN) {
        SceneTelemetryData& data = get_telemetry_data();
        ScopedMutexLock data_lock(&data.mutex);
        if (!data.hud_open) {
            return;
        }
        char*             target_buf = data.custom_player_name;
        const std::size_t max_cap    = sizeof(data.custom_player_name);

        if (key_code == AKEYCODE_DEL) {
            const std::size_t len = std::strlen(target_buf);
            if (len > 0) {
                target_buf[len - 1] = '\0';
            }
        } else {
            char       ch    = '\0';
            const bool shift = (meta_state & AMETA_SHIFT_ON) != 0;
            if (unicode_char >= 32 && unicode_char < 127) {
                ch = static_cast<char>(unicode_char);
            } else if (key_code >= AKEYCODE_A && key_code <= AKEYCODE_Z) {
                ch = static_cast<char>((shift ? 'A' : 'a') + (key_code - AKEYCODE_A));
            } else if (key_code >= AKEYCODE_0 && key_code <= AKEYCODE_9) {
                ch = static_cast<char>('0' + (key_code - AKEYCODE_0));
            } else if (key_code == AKEYCODE_SPACE) {
                ch = ' ';
            } else if (key_code == AKEYCODE_MINUS) {
                ch = shift ? '_' : '-';
            } else if (key_code == AKEYCODE_PERIOD) {
                ch = '.';
            }
            if (ch != '\0') {
                const std::size_t len = std::strlen(target_buf);
                if (len + 1 < max_cap) {
                    target_buf[len]     = ch;
                    target_buf[len + 1] = '\0';
                }
            }
        }
    }
}

void request_soft_keyboard(bool show) noexcept {
    using GetCreatedJavaVMsFn = jint (*)(JavaVM**, jsize, jsize*);
    auto get_vms = reinterpret_cast<GetCreatedJavaVMsFn>(
        ::dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs"));
    if (!get_vms) {
        if (void* art_handle = ::dlopen("libart.so", RTLD_LAZY | RTLD_NOLOAD)) {
            get_vms = reinterpret_cast<GetCreatedJavaVMsFn>(
                ::dlsym(art_handle, "JNI_GetCreatedJavaVMs"));
        }
    }
    if (!get_vms) {
        return;
    }

    JavaVM* vm       = nullptr;
    jsize   vm_count = 0;
    if (get_vms(&vm, 1, &vm_count) != JNI_OK || !vm || vm_count <= 0) {
        return;
    }

    JNIEnv* env      = nullptr;
    bool    attached = false;
    const jint env_stat = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (env_stat == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
            return;
        }
        attached = true;
    } else if (env_stat != JNI_OK || !env) {
        return;
    }

    auto clear_exc = [env]() noexcept -> bool {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return true;
        }
        return false;
    };

    if (env->PushLocalFrame(16) == JNI_OK) {
        jclass at_cls = env->FindClass("android/app/ActivityThread");
        if (!clear_exc() && at_cls) {
            jmethodID cur_app_mid = env->GetStaticMethodID(
                at_cls, "currentApplication", "()Landroid/app/Application;");
            if (!clear_exc() && cur_app_mid) {
                jobject app_obj = env->CallStaticObjectMethod(at_cls, cur_app_mid);
                if (!clear_exc() && app_obj) {
                    jclass ctx_cls = env->FindClass("android/content/Context");
                    jmethodID get_svc_mid = ctx_cls
                        ? env->GetMethodID(ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;")
                        : nullptr;
                    if (!clear_exc() && get_svc_mid) {
                        jstring imm_str = env->NewStringUTF("input_method");
                        jobject imm_obj = env->CallObjectMethod(app_obj, get_svc_mid, imm_str);
                        if (!clear_exc() && imm_obj) {
                            jclass imm_cls = env->FindClass("android/view/inputmethod/InputMethodManager");
                            jmethodID toggle_mid = imm_cls
                                ? env->GetMethodID(imm_cls, "toggleSoftInput", "(II)V")
                                : nullptr;
                            if (!clear_exc() && toggle_mid) {
                                // SHOW_FORCED = 2, HIDE_IMPLICIT_ONLY = 1
                                env->CallVoidMethod(imm_obj, toggle_mid, show ? 2 : 0, show ? 1 : 0);
                                (void)clear_exc();
                            }
                        }
                    }
                }
            }
        }
        env->PopLocalFrame(nullptr);
    }

    if (attached) {
        vm->DetachCurrentThread();
    }
}

} // namespace hud
