// File: imgui_gles3_hud.cpp
#include "imgui_gles3_hud.hpp"
#include "imgui_impl_opengl3.h"

#include <android/input.h>
#include <dlfcn.h>
#include <jni.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string_view>

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
    ImVec2          win_size{520.0f, 640.0f};
    bool            want_capture_mouse{false};
};

TouchInputState   g_touch{};
std::atomic<bool> g_imgui_initialized{false};

// Vertical height offset in Unreal centimeters (~0.85m actor torso/head offset above root pivot)
inline constexpr float kActorVerticalOffsetUnits = 85.0f;
inline constexpr float kNearClipPlaneUnits       = 1.0f;

[[nodiscard]] constexpr float deg_to_rad(float degrees) noexcept {
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

// Normalizes any arbitrary Euler angle (in degrees) into [-180.0f, 180.0f]
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
            jmethodID dm_ctor  = env->GetMethodID(dm_cls, "<init>", "()V");
            jfieldID  w_fid    = env->GetFieldID(dm_cls, "widthPixels", "I");
            jfieldID  h_fid    = env->GetFieldID(dm_cls, "heightPixels", "I");

            if (!clear_jni_exc() && dm_ctor && w_fid && h_fid) {
                // 1. Query Display.getRealMetrics via ActivityThread.currentApplication()
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

                // 2. Query Resources.getSystem().getDisplayMetrics()
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
        // Choose whichever physical metric aspect ratio matches the active EGLSurface aspect ratio best
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

} // namespace

SceneTelemetryData& get_telemetry_data() noexcept {
    static SceneTelemetryData instance{};
    return instance;
}

void format_friendly_node_label(const char* raw_name, char (&out_label)[kMaxLabelLen]) noexcept {
    if (!is_valid_cstr_ptr(raw_name) || raw_name[0] == '\0') {
        std::strncpy(out_label, "Unknown", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // Ignore non-spawned CDOs, lobby actors, sub-components, or handheld items/consumables
    if (icontains_label(raw_name, "Default__")    ||
        icontains_label(raw_name, "Lobby")        ||
        icontains_label(raw_name, "Menu")         ||
        icontains_label(raw_name, "Controller")   ||
        icontains_label(raw_name, "Component")    ||
        icontains_label(raw_name, "AnimInstance") ||
        icontains_label(raw_name, "Collectable")  ||
        icontains_label(raw_name, "Medkit")       ||
        icontains_label(raw_name, "Flashlight")   ||
        icontains_label(raw_name, "Toolbox")      ||
        icontains_label(raw_name, "Firecracker")  ||
        icontains_label(raw_name, "MapItem")      ||
        icontains_label(raw_name, "Key")          ||
        icontains_label(raw_name, "Addon")        ||
        icontains_label(raw_name, "Chargeable")   ||
        icontains_label(raw_name, "Inventory")    ||
        icontains_label(raw_name, "Item")) {
        std::strncpy(out_label, "Unknown", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // 1. Chests / Searchable Boxes (check BEFORE Survivor/Camper so Chests with Camper interactions aren't mislabeled)
    if (icontains_label(raw_name, "Chest") || icontains_label(raw_name, "Searchable")) {
        std::strncpy(out_label, "Chest", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // 2. Generators
    if (icontains_label(raw_name, "Generator")) {
        std::strncpy(out_label, "Generator", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // 3. Hatch / Escape Door
    if (icontains_label(raw_name, "Hatch")) {
        std::strncpy(out_label, "Hatch", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // 4. Totems
    if (icontains_label(raw_name, "Totem")) {
        std::strncpy(out_label, "Totem", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // 5. Killer (Slasher character pawn only; ignore hooks/traps/projectiles/powers)
    if ((icontains_label(raw_name, "Slasher") || icontains_label(raw_name, "Killer")) &&
        !icontains_label(raw_name, "Hook") &&
        !icontains_label(raw_name, "Trap") &&
        !icontains_label(raw_name, "Projectile") &&
        !icontains_label(raw_name, "Power") &&
        !icontains_label(raw_name, "Stalker") &&
        !icontains_label(raw_name, "Interactable")) {
        std::strncpy(out_label, "Killer", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // 6. Survivors (Camper character pawns only)
    if ((icontains_label(raw_name, "Camper") || icontains_label(raw_name, "Survivor")) &&
        !icontains_label(raw_name, "Hook") &&
        !icontains_label(raw_name, "Interactable")) {
        std::strncpy(out_label, "Survivor", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // Unrecognized actors are marked Unknown so strict match filtering can ignore clutter
    std::strncpy(out_label, "Unknown", kMaxLabelLen - 1);
    out_label[kMaxLabelLen - 1] = '\0';
}

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

    // Apply vertical world-space offset so markers anchor at actor torso/head rather than ground root
    const Vec3 elevated_world_pos{
        world_pos.x,
        world_pos.y,
        world_pos.z + kActorVerticalOffsetUnits
    };

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

    const Vec3 delta = elevated_world_pos - camera.location;

    const float view_x = delta.dot(axis_right);
    const float view_y = delta.dot(axis_up);
    const float view_z = delta.dot(axis_forward);

    // Strict near-plane guard: discard any node behind or too close to the camera plane
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

    // Strict viewport frustum guard: prevent markers or text from clamping/sticking to screen edges
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

    // Snapshot nodes and camera state under lock to minimize contention during draw list submission
    CameraView  cam_snapshot{};
    SceneNode   nodes_snapshot[kMaxSceneNodes]{};
    std::size_t count_snapshot      = 0;
    bool        match_live_snapshot = false;
    bool        draw_markers        = true;
    float       max_dist_m          = 250.0f;
    char        filter_snapshot[kMaxLabelLen]{};

    {
        ScopedMutexLock lock(&data.mutex);
        cam_snapshot        = data.current_camera;
        match_live_snapshot = data.match_active;
        count_snapshot      = std::min(data.node_count, kMaxSceneNodes);
        for (std::size_t i = 0; i < count_snapshot; ++i) {
            nodes_snapshot[i] = data.nodes[i];
        }
        draw_markers = data.draw_2d_node_markers;
        max_dist_m   = data.max_render_distance_m;
        std::memcpy(filter_snapshot, data.label_filter, kMaxLabelLen);
        filter_snapshot[kMaxLabelLen - 1] = '\0';
    }

    float touch_x_snapshot = -1000.0f;
    float touch_y_snapshot = -1000.0f;
    {
        ScopedMutexLock touch_lock(&g_touch.mutex);
        touch_x_snapshot = g_touch.x;
        touch_y_snapshot = g_touch.y;
    }

    std::size_t visible_markers_rendered = 0;

    // 1. Background 2D Aura Marker Rendering (ONLY when an active match is in progress)
    if (draw_markers && match_live_snapshot && count_snapshot > 0) {
        ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
        const ImU32 col_shadow   = IM_COL32(0, 0, 0, 210);
        const float center_x     = width * 0.5f;
        const float center_y     = height * 0.5f;

        bool killer_rendered_this_frame = false;

        for (std::size_t i = 0; i < count_snapshot; ++i) {
            const SceneNode& node = nodes_snapshot[i];

            if (!std::isfinite(node.distance_m) || node.distance_m > max_dist_m) {
                continue;
            }

            char clean_label[kMaxLabelLen]{};
            if (std::strcmp(node.label, "Killer") == 0 ||
                std::strcmp(node.label, "Survivor") == 0 ||
                std::strcmp(node.label, "Generator") == 0 ||
                std::strcmp(node.label, "Hatch") == 0 ||
                std::strcmp(node.label, "Chest") == 0 ||
                std::strcmp(node.label, "Totem") == 0) {
                std::strncpy(clean_label, node.label, kMaxLabelLen - 1);
                clean_label[kMaxLabelLen - 1] = '\0';
            } else {
                format_friendly_node_label(node.label, clean_label);
            }

            if (std::strcmp(clean_label, "Unknown") == 0) {
                continue;
            }

            // Guarantee at most ONE Killer aura marker is rendered per frame
            const bool is_killer = (std::strcmp(clean_label, "Killer") == 0);
            if (is_killer) {
                if (killer_rendered_this_frame) {
                    continue;
                }
                killer_rendered_this_frame = true;
            }

            if (!icontains_label(clean_label, filter_snapshot) &&
                !icontains_label(node.label, filter_snapshot)) {
                continue;
            }

            ImVec2 screen_pos{};
            if (!project_to_screen(node.world_pos, cam_snapshot, width, height, screen_pos)) {
                continue;
            }

            // Select Aura palette by entity classification:
            // - Killer:    Crimson Red
            // - Survivor:  Emerald Green / Cyan-Teal
            // - Generator: Golden Yellow
            // - Hatch:     Vibrant Purple
            // - Chest:     Clean Pure White
            // - Totem:     Soft Amber/Orange
            ImU32 col_core       = IM_COL32(52, 152, 219, 235);
            ImU32 col_inner_glow = IM_COL32(52, 152, 219, 95);
            ImU32 col_outer_glow = IM_COL32(52, 152, 219, 38);
            ImU32 col_ring       = IM_COL32(52, 152, 219, 175);
            float base_radius    = 6.0f;

            if (is_killer) {
                col_core       = IM_COL32(255, 45, 55, 245);
                col_inner_glow = IM_COL32(255, 45, 55, 110);
                col_outer_glow = IM_COL32(255, 20, 30, 48);
                col_ring       = IM_COL32(255, 110, 110, 210);
                base_radius    = 7.5f;
            } else if (std::strcmp(clean_label, "Survivor") == 0) {
                col_core       = IM_COL32(46, 224, 130, 240);
                col_inner_glow = IM_COL32(46, 224, 130, 95);
                col_outer_glow = IM_COL32(46, 224, 130, 38);
                col_ring       = IM_COL32(110, 255, 180, 185);
                base_radius    = 6.5f;
            } else if (std::strcmp(clean_label, "Generator") == 0) {
                col_core       = IM_COL32(255, 210, 40, 235);
                col_inner_glow = IM_COL32(255, 210, 40, 85);
                col_outer_glow = IM_COL32(255, 190, 20, 34);
                col_ring       = IM_COL32(255, 230, 110, 170);
                base_radius    = 6.0f;
            } else if (std::strcmp(clean_label, "Hatch") == 0) {
                col_core       = IM_COL32(195, 90, 255, 240);
                col_inner_glow = IM_COL32(195, 90, 255, 100);
                col_outer_glow = IM_COL32(175, 60, 255, 42);
                col_ring       = IM_COL32(225, 150, 255, 195);
                base_radius    = 6.5f;
            } else if (std::strcmp(clean_label, "Chest") == 0) {
                col_core       = IM_COL32(248, 250, 255, 235);
                col_inner_glow = IM_COL32(235, 242, 255, 85);
                col_outer_glow = IM_COL32(215, 230, 255, 32);
                col_ring       = IM_COL32(255, 255, 255, 180);
                base_radius    = 5.5f;
            } else if (std::strcmp(clean_label, "Totem") == 0) {
                col_core       = IM_COL32(255, 145, 50, 230);
                col_inner_glow = IM_COL32(255, 145, 50, 85);
                col_outer_glow = IM_COL32(255, 120, 30, 32);
                col_ring       = IM_COL32(255, 180, 100, 170);
                base_radius    = 5.0f;
            }

            // Draw multi-layered glowing Aura orb (outer halo + inner glow + crisp ring + bright core)
            bg_draw_list->AddCircleFilled(screen_pos, base_radius * 2.5f, col_outer_glow, 24);
            bg_draw_list->AddCircleFilled(screen_pos, base_radius * 1.6f, col_inner_glow, 20);
            bg_draw_list->AddCircle(screen_pos, base_radius + 2.2f, col_ring, 20, 1.5f);
            bg_draw_list->AddCircleFilled(screen_pos, base_radius, col_core, 16);

            // Clean Viewport Text Policy:
            // Show text label (Name + Distance) ONLY when:
            //  - Entity is the Killer, OR
            //  - Entity is in close proximity (<= 32m for Survivors, <= 22m for objects), OR
            //  - The player's crosshair (screen center) or touch cursor is hovering near the aura (<= 75px)
            const float dx_center = screen_pos.x - center_x;
            const float dy_center = screen_pos.y - center_y;
            const bool  near_reticle = ((dx_center * dx_center + dy_center * dy_center) <= (75.0f * 75.0f));

            const float dx_touch = screen_pos.x - touch_x_snapshot;
            const float dy_touch = screen_pos.y - touch_y_snapshot;
            const bool  near_touch = ((dx_touch * dx_touch + dy_touch * dy_touch) <= (75.0f * 75.0f));

            const float proximity_limit = (std::strcmp(clean_label, "Survivor") == 0) ? 32.0f : 22.0f;
            const bool  show_full_label =
                is_killer || (node.distance_m <= proximity_limit) || near_reticle || near_touch;

            if (show_full_label) {
                char formatted_tag[96]{};
                std::snprintf(
                    formatted_tag,
                    sizeof(formatted_tag),
                    "%s %.0fm",
                    clean_label,
                    static_cast<double>(node.distance_m));

                const ImVec2 text_size = ImGui::CalcTextSize(formatted_tag);
                const ImVec2 text_pos{
                    screen_pos.x - (text_size.x * 0.5f),
                    screen_pos.y - (base_radius * 2.3f) - text_size.y
                };
                bg_draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), col_shadow, formatted_tag);
                bg_draw_list->AddText(text_pos, col_core, formatted_tag);
            }

            ++visible_markers_rendered;
        }
    }

    // 2. Control Panel HUD Window
    ScopedMutexLock lock(&data.mutex);
    if (!data.hud_open) {
        ScopedMutexLock touch_lock(&g_touch.mutex);
        g_touch.want_capture_mouse = false;
        g_touch.captured_by_hud    = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(520.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GLES3 Scene Telemetry & Control Panel", &data.hud_open)) {
        // Section 1: Telemetry Status Counters
        if (ImGui::CollapsingHeader("1. Telemetry Status Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("EGL Viewport: %.0f x %.0f (%.1f FPS)",
                        static_cast<double>(width),
                        static_cast<double>(height),
                        static_cast<double>(io.Framerate));
            ImGui::Text("Touch Scale: %.3f x %.3f (Phys: %.0f x %.0f)",
                        static_cast<double>(g_touch.scale_x),
                        static_cast<double>(g_touch.scale_y),
                        static_cast<double>(g_touch.phys_width),
                        static_cast<double>(g_touch.phys_height));
            ImGui::Text("Player Name: %s",
                        data.custom_player_name[0] != '\0' ? data.custom_player_name : "(In-Game Default)");
            ImGui::Text("Match State: %s | Active Nodes: %zu / %zu",
                        match_live_snapshot ? "IN MATCH (ACTIVE)" : "LOBBY / LOADING (STANDBY)",
                        count_snapshot,
                        kMaxSceneNodes);
            ImGui::Text("Projected Auras Visible: %zu", visible_markers_rendered);
            ImGui::Separator();
            ImGui::Text("Camera Pos: (%.1f, %.1f, %.1f)",
                        static_cast<double>(data.current_camera.location.x),
                        static_cast<double>(data.current_camera.location.y),
                        static_cast<double>(data.current_camera.location.z));
            ImGui::Text("Camera Rot: Pitch %.1f | Yaw %.1f | Roll %.1f | FOV %.1f",
                        static_cast<double>(normalize_euler_angle(data.current_camera.rotation.pitch)),
                        static_cast<double>(normalize_euler_angle(data.current_camera.rotation.yaw)),
                        static_cast<double>(normalize_euler_angle(data.current_camera.rotation.roll)),
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

        // Section 3: Viewport Marker & Player Identity Settings
        if (ImGui::CollapsingHeader("3. Viewport Marker Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Draw 2D Node Markers", &data.draw_2d_node_markers);
            ImGui::SliderFloat("Max Render Distance", &data.max_render_distance_m, 1.0f, 1000.0f, "%.1f m");
            ImGui::InputText("Label Filter", data.label_filter, sizeof(data.label_filter));
            ImGui::Separator();
            ImGui::Checkbox("Override Player Name", &data.apply_custom_player_name);
            ImGui::InputText("Player Display Name", data.custom_player_name, sizeof(data.custom_player_name));
        }
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

    // Synchronize physical-to-EGL touch scaling and feed latest mapped touch coordinates every frame
    {
        ScopedMutexLock touch_lock(&g_touch.mutex);
        update_touch_scaling_locked(f_width, f_height);

        if (g_touch.has_event) {
            g_touch.x = (g_touch.raw_x - g_touch.offset_x) * g_touch.scale_x;
            g_touch.y = (g_touch.raw_y - g_touch.offset_y) * g_touch.scale_y;

            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(g_touch.x, g_touch.y);
            io.AddMouseButtonEvent(0, g_touch.is_down);

            // If a rapid tap (DOWN + UP) completed before this frame, hold DOWN for 1 frame
            // and release on the subsequent frame so Dear ImGui widgets register the click.
            if (g_touch.pending_tap_release) {
                g_touch.is_down             = false;
                g_touch.pending_tap_release = false;
            }
        }
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

    // If raw coordinates exceed the currently assumed physical bounds (e.g. before JNI metrics resolved),
    // dynamically expand physical bounds and recompute scale_x / scale_y.
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
        // UP / POINTER_UP / CANCEL
        if (was_down) {
            g_touch.pending_tap_release = true;
        }
        g_touch.is_down = false;
    }

    if (g_imgui_initialized.load(std::memory_order_acquire)) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(mapped_x, mapped_y);
        io.AddMouseButtonEvent(0, is_down);
        if (g_touch.captured_by_hud || inside_hud) {
            io.WantCaptureMouse = true;
        }
    }
}

bool should_consume_touch(int action, float x, float y) noexcept {
    {
        SceneTelemetryData& data = get_telemetry_data();
        ScopedMutexLock data_lock(&data.mutex);
        if (!data.hud_open) {
            return false;
        }
    }

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

} // namespace hud
