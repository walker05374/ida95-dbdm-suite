// File: ipc_shared_state.hpp
// ============================================================================
// Architecture : ELF64 AArch64 (arm64-v8a)
// Standard     : C++20 (-fno-exceptions -fno-rtti -fvisibility=hidden)
// Purpose      : Headless UI-free telemetry & physics control structs +
//                Abstract UNIX Domain Socket IPC (\0dbdm_telemetry_ipc_v1)
//                bridging the headless Zygisk module (instrument_core) and
//                the standalone Floating Mod Menu APK (com.dbdm.modmenu).
// ============================================================================
#pragma once

#include "imgui_gles3_hud.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hud {

namespace detail {

[[nodiscard]] constexpr bool is_valid_cstr(const char* ptr) noexcept {
    if (ptr == nullptr) {
        return false;
    }
    const auto raw  = reinterpret_cast<std::uintptr_t>(ptr);
    const auto addr = raw & 0x0000FFFFFFFFFFFFULL;
    return (addr >= 0x00010000ULL && addr <= 0x00007FFFFFFFFFFFULL);
}

[[nodiscard]] inline std::size_t safe_len(const char* s, std::size_t max_len = kMaxLabelLen) noexcept {
    if (!is_valid_cstr(s)) {
        return 0;
    }
    std::size_t n = 0;
    while (n < max_len && s[n] != '\0') {
        ++n;
    }
    return n;
}

[[nodiscard]] inline bool icontains_str(const char* haystack, const char* needle) noexcept {
    if (!is_valid_cstr(needle) || needle[0] == '\0') {
        return true;
    }
    if (!is_valid_cstr(haystack) || haystack[0] == '\0') {
        return false;
    }
    const std::size_t n_len = safe_len(needle, kMaxLabelLen);
    const std::size_t h_len = safe_len(haystack, 128);
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

} // namespace detail

// Inline clean human-readable English entity classifier (used by both headless backend and overlay)
inline void format_friendly_node_label(const char* raw_name, char (&out_label)[kMaxLabelLen]) noexcept {
    if (!detail::is_valid_cstr(raw_name) || raw_name[0] == '\0') {
        std::strncpy(out_label, "Unknown", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    // Fast-path for pre-classified canonical category labels
    if (std::strcmp(raw_name, "Killer") == 0 ||
        std::strcmp(raw_name, "Survivor") == 0 ||
        std::strcmp(raw_name, "Generator") == 0 ||
        std::strcmp(raw_name, "Hatch") == 0 ||
        std::strcmp(raw_name, "Chest") == 0 ||
        std::strcmp(raw_name, "Totem") == 0 ||
        std::strcmp(raw_name, "Pallet") == 0 ||
        std::strcmp(raw_name, "Hook") == 0 ||
        std::strcmp(raw_name, "ExitGate") == 0) {
        std::strncpy(out_label, raw_name, kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    if (detail::icontains_str(raw_name, "Default__")        ||
        detail::icontains_str(raw_name, "Window")           ||
        detail::icontains_str(raw_name, "Vault")            ||
        detail::icontains_str(raw_name, "Janela")           ||
        detail::icontains_str(raw_name, "Broken")           ||
        detail::icontains_str(raw_name, "Destroyed")        ||
        detail::icontains_str(raw_name, "Fallen")           ||
        detail::icontains_str(raw_name, "Cleansed")         ||
        detail::icontains_str(raw_name, "Illusion")         ||
        detail::icontains_str(raw_name, "Fake")             ||
        detail::icontains_str(raw_name, "Archetype")        ||
        detail::icontains_str(raw_name, "GEN_VARIABLE")     ||
        detail::icontains_str(raw_name, "LevelGenerator")   ||
        detail::icontains_str(raw_name, "ProceduralLevel")  ||
        detail::icontains_str(raw_name, "GeneratorEntity")  ||
        detail::icontains_str(raw_name, "Portal_Generator") ||
        detail::icontains_str(raw_name, "Portal_MeatHook")  ||
        detail::icontains_str(raw_name, "BigThreeEvent")    ||
        detail::icontains_str(raw_name, "BP_Entity_C")      ||
        detail::icontains_str(raw_name, "SacrificeEnd")     ||
        detail::icontains_str(raw_name, "PlayerStart")      ||
        detail::icontains_str(raw_name, "GameRoleFront")    ||
        detail::icontains_str(raw_name, "SlasherMale_REF")  ||
        detail::icontains_str(raw_name, "BP_Menu_")         ||
        detail::icontains_str(raw_name, "Lobby")            ||
        detail::icontains_str(raw_name, "Looby")            ||
        detail::icontains_str(raw_name, "pos_")             ||
        detail::icontains_str(raw_name, "Menu")             ||
        detail::icontains_str(raw_name, "Controller")       ||
        detail::icontains_str(raw_name, "Component")        ||
        detail::icontains_str(raw_name, "AnimInstance")     ||
        detail::icontains_str(raw_name, "Collectable")      ||
        detail::icontains_str(raw_name, "Medkit")           ||
        detail::icontains_str(raw_name, "Flashlight")       ||
        detail::icontains_str(raw_name, "Toolbox")          ||
        detail::icontains_str(raw_name, "Firecracker")      ||
        detail::icontains_str(raw_name, "MapItem")          ||
        detail::icontains_str(raw_name, "DullKey")          ||
        detail::icontains_str(raw_name, "SkeletonKey")      ||
        detail::icontains_str(raw_name, "BrokenKey")        ||
        detail::icontains_str(raw_name, "Addon")            ||
        detail::icontains_str(raw_name, "Chargeable")       ||
        detail::icontains_str(raw_name, "Inventory")        ||
        detail::icontains_str(raw_name, "BP_Item_")         ||
        detail::icontains_str(raw_name, "NightmareHusk")    ||
        detail::icontains_str(raw_name, "DreamInducer")     ||
        detail::icontains_str(raw_name, "BlastMine")        ||
        detail::icontains_str(raw_name, "Interactable")) {
        std::strncpy(out_label, "Unknown", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    if (detail::icontains_str(raw_name, "Chest") || detail::icontains_str(raw_name, "Searchable")) {
        std::strncpy(out_label, "Chest", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "Generator")) {
        std::strncpy(out_label, "Generator", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "Hatch")) {
        std::strncpy(out_label, "Hatch", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "EscapeDoor") || detail::icontains_str(raw_name, "ExitGate")) {
        std::strncpy(out_label, "ExitGate", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "Totem") || detail::icontains_str(raw_name, "HexSpawner")) {
        std::strncpy(out_label, "Totem", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "SmallMeatLocker") ||
        detail::icontains_str(raw_name, "MeatLocker") ||
        detail::icontains_str(raw_name, "MeatHook") ||
        detail::icontains_str(raw_name, "Hook")) {
        std::strncpy(out_label, "Hook", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "Bookshelf") || detail::icontains_str(raw_name, "Pallet")) {
        std::strncpy(out_label, "Pallet", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if ((detail::icontains_str(raw_name, "Slasher") || detail::icontains_str(raw_name, "Killer")) &&
        !detail::icontains_str(raw_name, "Trap") &&
        !detail::icontains_str(raw_name, "Projectile") &&
        !detail::icontains_str(raw_name, "Power") &&
        !detail::icontains_str(raw_name, "Stalker")) {
        std::strncpy(out_label, "Killer", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }
    if (detail::icontains_str(raw_name, "Camper") || detail::icontains_str(raw_name, "Survivor")) {
        std::strncpy(out_label, "Survivor", kMaxLabelLen - 1);
        out_label[kMaxLabelLen - 1] = '\0';
        return;
    }

    std::strncpy(out_label, "Unknown", kMaxLabelLen - 1);
    out_label[kMaxLabelLen - 1] = '\0';
}

} // namespace hud

namespace ipc {

inline constexpr std::uint32_t kIpcMagicVersion          = 0x44424442U; // "DBDB" (256-node v2 wire layout)
inline constexpr char          kAbstractSocketName[]     = "dbdm_telemetry_ipc_v2";
inline constexpr const char*   kSharedSnapshotFilePath   = "/data/local/tmp/dbdm_telemetry_shm_v2.bin";
inline constexpr const char*   kSharedConfigFilePath     = "/data/local/tmp/dbdm_mod_config.cfg";

enum class PacketType : std::uint32_t {
    QuerySnapshot = 1,
    PushControl   = 2
};

#pragma pack(push, 1)
struct ControlPayload {
    std::uint32_t magic                    = kIpcMagicVersion;
    std::uint32_t packet_type              = static_cast<std::uint32_t>(PacketType::PushControl);
    std::uint8_t  hud_open                 = 1;
    std::uint8_t  draw_2d_node_markers     = 1;
    std::uint8_t  apply_custom_player_name = 0;
    std::uint8_t  apply_custom_kinematics  = 0;
    float         max_render_distance_m    = 250.0f;
    float         walk_speed               = 600.0f;
    float         max_acceleration         = 2048.0f;
    float         jump_velocity            = 420.0f;
    float         time_scale               = 1.0f;
    char          label_filter[hud::kMaxLabelLen]{};
    char          custom_player_name[hud::kMaxLabelLen]{};
};

struct WireNode {
    float x            = 0.0f;
    float y            = 0.0f;
    float z            = 0.0f;
    float distance_m   = 0.0f;
    float progress_pct = -1.0f;
    char  label[hud::kMaxLabelLen]{};
};

struct TelemetrySnapshotPayload {
    std::uint32_t magic                   = kIpcMagicVersion;
    std::uint32_t match_active            = 0;
    std::uint32_t node_count              = 0;
    std::uint32_t active_generators       = 0;
    std::uint32_t skillchecks_handled     = 0;
    float         nearest_gen_progress    = 0.0f;
    std::uint64_t tick_seq                = 0;
    std::uint64_t uworld_ptr              = 0;
    std::uint64_t ulevel_ptr              = 0;
    float         cam_x                   = 0.0f;
    float         cam_y                   = 0.0f;
    float         cam_z                   = 0.0f;
    float         cam_pitch               = 0.0f;
    float         cam_yaw                 = 0.0f;
    float         cam_roll                = 0.0f;
    float         cam_fov                 = 90.0f;
    float         cur_walk_speed          = 600.0f;
    float         cur_max_accel           = 2048.0f;
    float         cur_jump_vel            = 420.0f;
    float         cur_time_scale          = 1.0f;
    char          active_player_name[hud::kMaxLabelLen]{};
    WireNode      nodes[hud::kMaxSceneNodes]{};
};
#pragma pack(pop)

inline socklen_t build_abstract_sockaddr(struct sockaddr_un& addr) noexcept {
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0'; // Abstract namespace prefix on Linux/Android
    constexpr std::size_t kNameLen = sizeof(kAbstractSocketName) - 1;
    std::memcpy(&addr.sun_path[1], kAbstractSocketName, kNameLen);
    return static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + kNameLen);
}

// Populates TelemetrySnapshotPayload from the current in-process SceneTelemetryData
inline void fill_snapshot_from_telemetry(TelemetrySnapshotPayload& out) noexcept {
    hud::SceneTelemetryData& data = hud::get_telemetry_data();
    pthread_mutex_lock(&data.mutex);

    out.magic                = kIpcMagicVersion;
    out.match_active         = data.match_active ? 1U : 0U;
    const std::size_t count  = (data.node_count <= hud::kMaxSceneNodes)
        ? data.node_count
        : 0U;
    out.node_count           = static_cast<std::uint32_t>(count);
    out.active_generators    = data.active_generators;
    out.skillchecks_handled  = data.skillchecks_handled;
    out.nearest_gen_progress = data.nearest_gen_progress;
    out.tick_seq             = data.tick_seq;
    out.uworld_ptr           = data.uworld_ptr;
    out.ulevel_ptr           = data.ulevel_ptr;
    out.cam_x                = data.current_camera.location.x;
    out.cam_y                = data.current_camera.location.y;
    out.cam_z                = data.current_camera.location.z;
    out.cam_pitch            = data.current_camera.rotation.pitch;
    out.cam_yaw              = data.current_camera.rotation.yaw;
    out.cam_roll             = data.current_camera.rotation.roll;
    out.cam_fov              = data.current_camera.fov;
    out.cur_walk_speed       = data.walk_speed;
    out.cur_max_accel        = data.max_acceleration;
    out.cur_jump_vel         = data.jump_velocity;
    out.cur_time_scale       = data.time_scale;

    const char* name_src = (data.apply_custom_player_name && data.custom_player_name[0] != '\0')
        ? data.custom_player_name
        : (data.in_game_player_name[0] != '\0' ? data.in_game_player_name : data.custom_player_name);
    std::strncpy(out.active_player_name, name_src, hud::kMaxLabelLen - 1);
    out.active_player_name[hud::kMaxLabelLen - 1] = '\0';

    for (std::size_t i = 0; i < count; ++i) {
        out.nodes[i].x            = data.nodes[i].world_pos.x;
        out.nodes[i].y            = data.nodes[i].world_pos.y;
        out.nodes[i].z            = data.nodes[i].world_pos.z;
        out.nodes[i].distance_m   = data.nodes[i].distance_m;
        out.nodes[i].progress_pct = data.nodes[i].progress_pct;
        std::memcpy(out.nodes[i].label, data.nodes[i].label, hud::kMaxLabelLen);
        out.nodes[i].label[hud::kMaxLabelLen - 1] = '\0';
    }

    pthread_mutex_unlock(&data.mutex);
}

// Writes the latest TelemetrySnapshotPayload in-place (without truncation) to /data/local/tmp/dbdm_telemetry_shm.bin
inline void write_snapshot_file(const TelemetrySnapshotPayload& snap) noexcept {
    static int  s_shm_fd       = -1;
    static bool s_chmod_done   = false;
    if (s_shm_fd < 0) {
        s_shm_fd = ::open(kSharedSnapshotFilePath, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (s_shm_fd >= 0 && !s_chmod_done) {
            ::fchmod(s_shm_fd, 0666);
            ::chmod(kSharedSnapshotFilePath, 0666);
            s_chmod_done = true;
        }
    }
    if (s_shm_fd >= 0) {
        const ssize_t w = ::pwrite(s_shm_fd, &snap, sizeof(snap), 0);
        if (w != static_cast<ssize_t>(sizeof(snap))) {
            ::close(s_shm_fd);
            s_shm_fd = -1;
        }
    }
}

// Reads the latest TelemetrySnapshotPayload from /data/local/tmp/dbdm_telemetry_shm.bin
[[nodiscard]] inline bool read_snapshot_file(TelemetrySnapshotPayload& out) noexcept {
    const int fd = ::open(kSharedSnapshotFilePath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const ssize_t r = ::pread(fd, &out, sizeof(out), 0);
    ::close(fd);
    return (r == static_cast<ssize_t>(sizeof(out)) && out.magic == kIpcMagicVersion);
}

inline void apply_control_to_telemetry(const ControlPayload& ctrl) noexcept {
    if (ctrl.magic != kIpcMagicVersion) {
        return;
    }
    hud::SceneTelemetryData& data = hud::get_telemetry_data();
    pthread_mutex_lock(&data.mutex);

    data.hud_open                 = (ctrl.hud_open != 0);
    data.draw_2d_node_markers     = (ctrl.draw_2d_node_markers != 0);
    data.apply_custom_player_name = (ctrl.apply_custom_player_name != 0);
    data.apply_custom_kinematics  = (ctrl.apply_custom_kinematics != 0);

    if (ctrl.max_render_distance_m >= 1.0f && ctrl.max_render_distance_m <= 1000.0f) {
        data.max_render_distance_m = ctrl.max_render_distance_m;
    }
    if (ctrl.walk_speed >= 100.0f && ctrl.walk_speed <= 3000.0f) {
        data.walk_speed     = ctrl.walk_speed;
        data.walk_speed_pct = (ctrl.walk_speed <= 300.0f) ? ctrl.walk_speed : (ctrl.walk_speed / 600.0f) * 100.0f;
    }
    if (ctrl.max_acceleration >= 100.0f && ctrl.max_acceleration <= 8192.0f) {
        data.max_acceleration = ctrl.max_acceleration;
        data.accel_pct        = (ctrl.max_acceleration <= 300.0f) ? ctrl.max_acceleration : (ctrl.max_acceleration / 2048.0f) * 100.0f;
    }
    if (ctrl.jump_velocity >= 100.0f && ctrl.jump_velocity <= 2500.0f) {
        data.jump_velocity = ctrl.jump_velocity;
        data.jump_pct      = (ctrl.jump_velocity <= 300.0f) ? ctrl.jump_velocity : (ctrl.jump_velocity / 420.0f) * 100.0f;
    }
    if (ctrl.time_scale >= 0.1f && ctrl.time_scale <= 250.0f) {
        if (ctrl.time_scale >= 10.0f) {
            data.time_scale_pct = ctrl.time_scale;
            data.time_scale     = ctrl.time_scale * 0.01f;
        } else {
            data.time_scale     = ctrl.time_scale;
            data.time_scale_pct = ctrl.time_scale * 100.0f;
        }
    }

    std::strncpy(data.label_filter, ctrl.label_filter, hud::kMaxLabelLen - 1);
    data.label_filter[hud::kMaxLabelLen - 1] = '\0';

    std::strncpy(data.custom_player_name, ctrl.custom_player_name, hud::kMaxLabelLen - 1);
    data.custom_player_name[hud::kMaxLabelLen - 1] = '\0';

    pthread_mutex_unlock(&data.mutex);
}

// Client helper (used by the Standalone Mod Menu APK process) to send ControlPayload
// or request the latest TelemetrySnapshotPayload from the headless Zygisk backend.
// Automatically falls back to the lockless shared snapshot file (/data/local/tmp/dbdm_telemetry_shm.bin)
// if abstract sockets are isolated by a container/VM environment.
[[nodiscard]] inline bool client_exchange_packet(
    const ControlPayload*     optional_ctrl,
    TelemetrySnapshotPayload* optional_out_snapshot) noexcept {

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd >= 0) {
        struct timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 30000; // 30ms fast timeout
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_un addr{};
        const socklen_t addr_len = build_abstract_sockaddr(addr);
        if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), addr_len) == 0) {
            ControlPayload req{};
            if (optional_ctrl != nullptr) {
                req = *optional_ctrl;
                req.magic       = kIpcMagicVersion;
                req.packet_type = static_cast<std::uint32_t>(PacketType::PushControl);
            } else {
                req.magic       = kIpcMagicVersion;
                req.packet_type = static_cast<std::uint32_t>(PacketType::QuerySnapshot);
            }

            const ssize_t sent = ::send(fd, &req, sizeof(req), MSG_NOSIGNAL);
            if (sent == static_cast<ssize_t>(sizeof(req))) {
                bool ok = true;
                if (optional_out_snapshot != nullptr) {
                    std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(optional_out_snapshot);
                    std::size_t total_read = 0;
                    while (total_read < sizeof(TelemetrySnapshotPayload)) {
                        const ssize_t r = ::recv(
                            fd,
                            dst + total_read,
                            sizeof(TelemetrySnapshotPayload) - total_read,
                            0);
                        if (r <= 0) {
                            ok = false;
                            break;
                        }
                        total_read += static_cast<std::size_t>(r);
                    }
                    if (ok && optional_out_snapshot->magic != kIpcMagicVersion) {
                        ok = false;
                    }
                }
                ::close(fd);
                if (ok) {
                    return true;
                }
            }
        }
        ::close(fd);
    }

    // Fallback to shared telemetry file (/data/local/tmp/dbdm_telemetry_shm.bin)
    if (optional_out_snapshot != nullptr) {
        return read_snapshot_file(*optional_out_snapshot);
    }
    return true;
}

} // namespace ipc

