// File: telemetry_adapter.hpp
#pragma once

#include "imgui_gles3_hud.hpp"
#include "scene_collector_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <span>

namespace hud::adapter {

// UE4 EImmobilizeState (Set Immobilize: none, hooked, trapped, hiding, dead, caged)
enum class ImmobilizeState : std::uint8_t {
    None    = 0,
    Hooked  = 1,
    Trapped = 2,
    Hiding  = 3,
    Dead    = 4,
    Caged   = 5
};

// UE4 EPlayerRole (Set Player Role: camper, slasher, observer)
enum class PlayerRole : std::uint8_t {
    None     = 0,
    Camper   = 1,
    Slasher  = 2,
    Observer = 3
};

// UE4 EMovementMode constants for UCharacterMovementComponent::MovementMode (+0x0174)
enum class MovementMode : std::uint8_t {
    None     = 0,
    Walking  = 1,
    NavWalk  = 2,
    Falling  = 3,
    Swimming = 4,
    Flying   = 5,
    Custom   = 6
};

struct GameplayManipulationConfig {
    // 1. Field of View (FOV) Changer (80.0f - 120.0f)
    bool            apply_fov_override{false};
    float           target_fov{90.0f};

    // 2. Auto Skillcheck & Trap Immunity
    bool            auto_skillcheck{false};
    bool            trap_immunity{false};

    // 3. Fly Cheat & Force Escape
    bool            fly_cheat_enabled{false};
    bool            force_escape{false};

    // 4. Immobilize State & Player Role Setters
    bool            apply_immobilize_state{false};
    ImmobilizeState target_immobilize_state{ImmobilizeState::None};
    bool            apply_player_role{false};
    PlayerRole      target_player_role{PlayerRole::None};
};

[[nodiscard]] inline GameplayManipulationConfig& get_gameplay_config() noexcept {
    static GameplayManipulationConfig instance{};
    return instance;
}

struct RawEntitySample {
    float             x{0.0f};
    float             y{0.0f};
    float             z{0.0f};
    float             dist_m{0.0f};
    const char*       name{nullptr};
    hud::NodeCategory category{hud::NodeCategory::SpecialProp};
    float             yaw_deg{0.0f};
    float             progress_pct{0.0f};
    bool              is_open{false};
    bool              has_skeleton{false};
    hud::Vec3         bone_joints[hud::kSkeletonJointCount]{};
};

struct KinematicsSyncResult {
    bool  is_safe_to_apply{false};
    bool  should_apply_kinematics{false};
    bool  movement_dirty_flag{false};
    float walk_speed_mult{1.0f};
    float accel_mult{1.0f};
    float jump_mult{1.0f};
    float time_scale_mult{1.0f};

    // Scaled target values (for legacy callers)
    float walk_speed{600.0f};
    float max_acceleration{2048.0f};
    float jump_velocity{420.0f};
    float time_scale{1.0f};

    bool  should_apply_player_name{false};
    char  custom_player_name[kMaxLabelLen]{};

    // Advanced Gameplay Manipulation States
    bool            should_apply_fov{false};
    float           target_fov{90.0f};
    bool            auto_skillcheck{false};
    bool            trap_immunity{false};
    bool            fly_cheat_enabled{false};
    bool            force_escape{false};
    bool            should_apply_immobilize{false};
    ImmobilizeState immobilize_state{ImmobilizeState::None};
    bool            should_apply_role{false};
    PlayerRole      player_role{PlayerRole::None};
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

// Strict engine base value range guard: finite and within [50.0f, 5000.0f]
[[nodiscard]] constexpr bool is_valid_engine_base_value(float val) noexcept {
    return std::isfinite(val) && (val >= 50.0f && val <= 5000.0f);
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
    if (std::strcmp(raw_name, "Killer") == 0       ||
        std::strcmp(raw_name, "Assassino") == 0    ||
        std::strcmp(raw_name, "Survivor") == 0     ||
        std::strcmp(raw_name, "Sobrevivente") == 0 ||
        std::strcmp(raw_name, "Generator") == 0    ||
        std::strcmp(raw_name, "Gerador") == 0      ||
        std::strcmp(raw_name, "Hatch") == 0        ||
        std::strcmp(raw_name, "Alçapão") == 0      ||
        std::strcmp(raw_name, "Chest") == 0        ||
        std::strcmp(raw_name, "Baú") == 0          ||
        std::strcmp(raw_name, "Totem") == 0        ||
        std::strcmp(raw_name, "Pallet") == 0       ||
        std::strcmp(raw_name, "Hook") == 0         ||
        std::strcmp(raw_name, "Gancho") == 0       ||
        std::strcmp(raw_name, "ExitGate") == 0     ||
        std::strcmp(raw_name, "Portão de Saída") == 0) {
        return false;
    }
    return icontains_str(raw_name, "Default__")        ||
           icontains_str(raw_name, "Window")           ||
           icontains_str(raw_name, "Vault")            ||
           icontains_str(raw_name, "Janela")           ||
           icontains_str(raw_name, "Broken")           ||
           icontains_str(raw_name, "Destroyed")        ||
           icontains_str(raw_name, "Fallen")           ||
           icontains_str(raw_name, "Cleansed")         ||
           icontains_str(raw_name, "Fake")             ||
           icontains_str(raw_name, "Lobby")            ||
           icontains_str(raw_name, "Looby")            ||
           icontains_str(raw_name, "pos_")             ||
           icontains_str(raw_name, "Archetype")        ||
           icontains_str(raw_name, "GEN_VARIABLE")     ||
           icontains_str(raw_name, "LevelGenerator")   ||
           icontains_str(raw_name, "ProceduralLevel")  ||
           icontains_str(raw_name, "GeneratorEntity")  ||
           icontains_str(raw_name, "Portal_Generator") ||
           icontains_str(raw_name, "Portal_MeatHook")  ||
           icontains_str(raw_name, "BigThreeEvent")    ||
           icontains_str(raw_name, "BP_Entity_C")      ||
           icontains_str(raw_name, "SacrificeEnd")     ||
           icontains_str(raw_name, "PlayerStart")      ||
           icontains_str(raw_name, "GameRoleFront")    ||
           icontains_str(raw_name, "SlasherMale_REF")  ||
           icontains_str(raw_name, "BP_Menu_")         ||
           icontains_str(raw_name, "Menu")             ||
           icontains_str(raw_name, "Controller")       ||
           icontains_str(raw_name, "PlayerState")      ||
           icontains_str(raw_name, "GameState")        ||
           icontains_str(raw_name, "GameMode")         ||
           icontains_str(raw_name, "Camera")           ||
           icontains_str(raw_name, "AnimInstance")     ||
           icontains_str(raw_name, "Component")        ||
           icontains_str(raw_name, "Collectable")      ||
           icontains_str(raw_name, "Medkit")           ||
           icontains_str(raw_name, "Flashlight")       ||
           icontains_str(raw_name, "Toolbox")          ||
           icontains_str(raw_name, "Firecracker")      ||
           icontains_str(raw_name, "MapItem")          ||
           icontains_str(raw_name, "DullKey")          ||
           icontains_str(raw_name, "SkeletonKey")      ||
           icontains_str(raw_name, "BrokenKey")        ||
           icontains_str(raw_name, "Addon")            ||
           icontains_str(raw_name, "Inventory")        ||
           icontains_str(raw_name, "BP_Item_")         ||
           icontains_str(raw_name, "Audio")            ||
           icontains_str(raw_name, "Light")            ||
           icontains_str(raw_name, "Volume")           ||
           icontains_str(raw_name, "Trigger")          ||
           icontains_str(raw_name, "Outline")          ||
           icontains_str(raw_name, "Tracker")          ||
           icontains_str(raw_name, "VFX")              ||
           icontains_str(raw_name, "Particle")         ||
           icontains_str(raw_name, "Decal")            ||
           icontains_str(raw_name, "Power")            ||
           icontains_str(raw_name, "Projectile")       ||
           icontains_str(raw_name, "Trap")             ||
           icontains_str(raw_name, "Stalker")          ||
           icontains_str(raw_name, "Husk")             ||
           icontains_str(raw_name, "Illusion")         ||
           icontains_str(raw_name, "Phantom")          ||
           icontains_str(raw_name, "Dead")             ||
           icontains_str(raw_name, "Corpse")           ||
           icontains_str(raw_name, "Mori")             ||
           icontains_str(raw_name, "Interactable")     ||
           icontains_str(raw_name, "Status")           ||
           icontains_str(raw_name, "Heartbeat")        ||
           icontains_str(raw_name, "BlastMine")        ||
           icontains_str(raw_name, "DreamInducer");
}

// Identifies the true spawned Killer character pawn (e.g. BP_Slasher_Character_XX_C or named Killer)
[[nodiscard]] inline bool is_true_killer_actor(const char* raw_name, hud::NodeCategory cat) noexcept {
    if (cat == hud::NodeCategory::Antagonist) {
        return !raw_name || !is_ignored_auxiliary_actor(raw_name);
    }
    if (!raw_name || is_ignored_auxiliary_actor(raw_name)) {
        return false;
    }
    return classify_node_category(raw_name) == hud::NodeCategory::Antagonist;
}

[[nodiscard]] constexpr int category_priority(hud::NodeCategory cat) noexcept {
    switch (cat) {
        case hud::NodeCategory::Antagonist:  return 0;
        case hud::NodeCategory::Teammate:    return 1;
        case hud::NodeCategory::PowerNode:   return 2;
        case hud::NodeCategory::Portal:      return 3;
        case hud::NodeCategory::Shrine:      return 4;
        case hud::NodeCategory::Container:   return 5;
        case hud::NodeCategory::Barrier:     return 6;
        case hud::NodeCategory::SpecialProp: return 7;
    }
    return 7;
}

// Classifies actor name and resolves both clean label and NodeCategory
inline void classify_sample(
    const RawEntitySample& src,
    hud::NodeCategory&     out_category,
    char                   (&out_label)[kMaxLabelLen]) noexcept {

    out_category = (src.name != nullptr) ? classify_node_category(src.name) : src.category;

    if (!is_valid_user_ptr(src.name) || src.name[0] == '\0') {
        switch (src.category) {
            case hud::NodeCategory::Antagonist:  copy_raw_string(out_label, kMaxLabelLen, "Assassino");       return;
            case hud::NodeCategory::Teammate:    copy_raw_string(out_label, kMaxLabelLen, "Sobrevivente");    return;
            case hud::NodeCategory::PowerNode:   copy_raw_string(out_label, kMaxLabelLen, "Gerador");         return;
            case hud::NodeCategory::Shrine:      copy_raw_string(out_label, kMaxLabelLen, "Totem");           return;
            case hud::NodeCategory::Container:   copy_raw_string(out_label, kMaxLabelLen, "Baú");             return;
            case hud::NodeCategory::Barrier:     copy_raw_string(out_label, kMaxLabelLen, "Pallet");          return;
            case hud::NodeCategory::Portal:      copy_raw_string(out_label, kMaxLabelLen, "Portão de Saída"); return;
            case hud::NodeCategory::SpecialProp: copy_raw_string(out_label, kMaxLabelLen, "Objeto Especial"); return;
        }
    }

    if (is_ignored_auxiliary_actor(src.name)) {
        copy_raw_string(out_label, kMaxLabelLen, "Unknown");
        return;
    }

    const char* raw_name = src.name;

    if (classify_node_category(raw_name) == hud::NodeCategory::Antagonist &&
        !icontains_str(raw_name, "Trap") &&
        !icontains_str(raw_name, "Projectile") &&
        !icontains_str(raw_name, "Power") &&
        !icontains_str(raw_name, "Stalker")) {
        out_category = hud::NodeCategory::Antagonist;
        copy_raw_string(out_label, kMaxLabelLen,
                        (std::strcmp(raw_name, "Killer") == 0) ? "Assassino" : raw_name);
        return;
    }

    if (std::strcmp(raw_name, "Survivor") == 0 ||
        std::strcmp(raw_name, "Sobrevivente") == 0 ||
        icontains_str(raw_name, "Camper") ||
        icontains_str(raw_name, "Survivor")) {
        out_category = hud::NodeCategory::Teammate;
        copy_raw_string(out_label, kMaxLabelLen,
                        (std::strcmp(raw_name, "Survivor") == 0) ? "Sobrevivente" : raw_name);
        return;
    }

    if (std::strcmp(raw_name, "Generator") == 0 ||
        std::strcmp(raw_name, "Gerador") == 0 ||
        icontains_str(raw_name, "Generator")) {
        out_category = hud::NodeCategory::PowerNode;
        copy_raw_string(out_label, kMaxLabelLen, "Gerador");
        return;
    }

    if (std::strcmp(raw_name, "Totem") == 0 ||
        icontains_str(raw_name, "Totem") ||
        icontains_str(raw_name, "HexSpawner")) {
        out_category = hud::NodeCategory::Shrine;
        copy_raw_string(out_label, kMaxLabelLen, "Totem");
        return;
    }

    if (std::strcmp(raw_name, "Chest") == 0 ||
        std::strcmp(raw_name, "Baú") == 0 ||
        icontains_str(raw_name, "Chest") ||
        icontains_str(raw_name, "Searchable")) {
        out_category = hud::NodeCategory::Container;
        copy_raw_string(out_label, kMaxLabelLen, "Baú");
        return;
    }

    if (std::strcmp(raw_name, "Pallet") == 0 ||
        icontains_str(raw_name, "Bookshelf") ||
        icontains_str(raw_name, "Pallet")) {
        out_category = hud::NodeCategory::Barrier;
        copy_raw_string(out_label, kMaxLabelLen, "Pallet");
        return;
    }

    if (std::strcmp(raw_name, "Hatch") == 0 ||
        std::strcmp(raw_name, "Alçapão") == 0 ||
        icontains_str(raw_name, "Hatch")) {
        out_category = hud::NodeCategory::Portal;
        copy_raw_string(out_label, kMaxLabelLen, "Alçapão");
        return;
    }

    if (std::strcmp(raw_name, "ExitGate") == 0 ||
        std::strcmp(raw_name, "Portão de Saída") == 0 ||
        icontains_str(raw_name, "EscapeDoor") ||
        icontains_str(raw_name, "ExitGate")) {
        out_category = hud::NodeCategory::Portal;
        copy_raw_string(out_label, kMaxLabelLen, "Portão de Saída");
        return;
    }

    if (std::strcmp(raw_name, "Hook") == 0 ||
        std::strcmp(raw_name, "Gancho") == 0 ||
        icontains_str(raw_name, "SmallMeatLocker") ||
        icontains_str(raw_name, "MeatLocker") ||
        icontains_str(raw_name, "MeatHook") ||
        icontains_str(raw_name, "Hook")) {
        out_category = hud::NodeCategory::SpecialProp;
        copy_raw_string(out_label, kMaxLabelLen, "Gancho");
        return;
    }

    // If caller explicitly set a category other than SpecialProp, preserve it and its custom label
    if (src.category != hud::NodeCategory::SpecialProp) {
        out_category = src.category;
        copy_raw_string(out_label, kMaxLabelLen, raw_name);
        return;
    }

    copy_raw_string(out_label, kMaxLabelLen, raw_name);
}

} // namespace detail

// Parses string representation of ImmobilizeState ("none", "hooked", "trapped", "hiding", "dead", "caged" or "0".."5")
[[nodiscard]] inline ImmobilizeState parse_immobilize_state(const char* str) noexcept {
    if (!detail::is_valid_user_ptr(str) || str[0] == '\0') {
        return ImmobilizeState::None;
    }
    if (detail::icontains_str(str, "hooked")  || str[0] == '1') return ImmobilizeState::Hooked;
    if (detail::icontains_str(str, "trapped") || str[0] == '2') return ImmobilizeState::Trapped;
    if (detail::icontains_str(str, "hiding")  || str[0] == '3') return ImmobilizeState::Hiding;
    if (detail::icontains_str(str, "dead")    || str[0] == '4') return ImmobilizeState::Dead;
    if (detail::icontains_str(str, "caged")   || str[0] == '5') return ImmobilizeState::Caged;
    return ImmobilizeState::None;
}

// Parses string representation of PlayerRole ("camper", "slasher", "observer" or "1".."3")
[[nodiscard]] inline PlayerRole parse_player_role(const char* str) noexcept {
    if (!detail::is_valid_user_ptr(str) || str[0] == '\0') {
        return PlayerRole::None;
    }
    if (detail::icontains_str(str, "camper")   || detail::icontains_str(str, "survivor") || str[0] == '1') return PlayerRole::Camper;
    if (detail::icontains_str(str, "slasher")  || detail::icontains_str(str, "killer")   || str[0] == '2') return PlayerRole::Slasher;
    if (detail::icontains_str(str, "observer") || detail::icontains_str(str, "spectator")|| str[0] == '3') return PlayerRole::Observer;
    return PlayerRole::None;
}

// Thread-safe configuration setters for advanced gameplay features
inline void set_fov_override(bool enable, float fov_deg) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    GameplayManipulationConfig& cfg = get_gameplay_config();
    cfg.apply_fov_override = enable;
    cfg.target_fov         = std::clamp(detail::sanitize_float(fov_deg, 90.0f), 80.0f, 120.0f);
}

inline void set_auto_skillcheck(bool enable) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    get_gameplay_config().auto_skillcheck = enable;
}

inline void set_trap_immunity(bool enable) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    get_gameplay_config().trap_immunity = enable;
}

inline void set_fly_cheat(bool enable) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    get_gameplay_config().fly_cheat_enabled = enable;
}

inline void set_force_escape(bool enable) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    get_gameplay_config().force_escape = enable;
}

inline void set_immobilize_state(ImmobilizeState state, bool enable = true) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    GameplayManipulationConfig& cfg = get_gameplay_config();
    cfg.apply_immobilize_state  = enable;
    cfg.target_immobilize_state = state;
}

inline void set_player_role(PlayerRole role, bool enable = true) noexcept {
    SceneTelemetryData& data = get_telemetry_data();
    detail::ScopedPthreadLock lock(&data.mutex);
    GameplayManipulationConfig& cfg = get_gameplay_config();
    cfg.apply_player_role  = enable && (role != PlayerRole::None);
    cfg.target_player_role = role;
}

// Unified parser for compact & verbose @gp: IPC control strings:
// Format: "@gp:f=<0|1>:<fov>;s=<0|1>;t=<0|1>;y=<0|1>;e=<0|1>;i=<state>;r=<role>;c=<hexMask>"
inline bool parse_gameplay_ipc_string(
    const char*                 raw_filter,
    SceneTelemetryData&         data,
    GameplayManipulationConfig& gp_cfg) noexcept {

    if (!detail::is_valid_user_ptr(raw_filter) || std::strncmp(raw_filter, "@gp:", 4) != 0) {
        return false;
    }

    data.label_filter[0] = '\0';
    const char* p = raw_filter + 4;
    while (*p != '\0') {
        char kv[64]{};
        std::size_t klen = 0;
        while (*p != '\0' && *p != ';' && klen + 1 < sizeof(kv)) {
            kv[klen++] = *p++;
        }
        if (*p == ';') {
            ++p;
        }
        kv[klen] = '\0';
        char* eq = std::strchr(kv, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char* k = kv;
        const char* v = eq + 1;

        if (std::strcmp(k, "f") == 0) {
            int   fon = 0;
            float fv  = gp_cfg.target_fov;
            if (std::sscanf(v, "%d:%f", &fon, &fv) == 2 && std::isfinite(fv)) {
                gp_cfg.apply_fov_override = (fon != 0);
                gp_cfg.target_fov         = std::clamp(fv, 80.0f, 120.0f);
            } else if (std::sscanf(v, "%d", &fon) == 1) {
                gp_cfg.apply_fov_override = (fon != 0);
            }
        } else if (std::strcmp(k, "fov_on") == 0) {
            gp_cfg.apply_fov_override = (v[0] == '1');
        } else if (std::strcmp(k, "fov") == 0) {
            float fv = gp_cfg.target_fov;
            if (std::sscanf(v, "%f", &fv) == 1 && std::isfinite(fv)) {
                gp_cfg.target_fov = std::clamp(fv, 80.0f, 120.0f);
            }
        } else if (std::strcmp(k, "s") == 0 || std::strcmp(k, "skill") == 0) {
            gp_cfg.auto_skillcheck = (v[0] == '1');
        } else if (std::strcmp(k, "t") == 0 || std::strcmp(k, "trap") == 0) {
            gp_cfg.trap_immunity = (v[0] == '1');
        } else if (std::strcmp(k, "y") == 0 || std::strcmp(k, "fly") == 0) {
            gp_cfg.fly_cheat_enabled = (v[0] == '1');
        } else if (std::strcmp(k, "e") == 0 || std::strcmp(k, "esc") == 0) {
            gp_cfg.force_escape = (v[0] == '1');
        } else if (std::strcmp(k, "i") == 0 || std::strcmp(k, "imm") == 0) {
            if (v[0] == '\0' || std::strcmp(v, "off") == 0) {
                gp_cfg.apply_immobilize_state  = false;
                gp_cfg.target_immobilize_state = ImmobilizeState::None;
            } else {
                gp_cfg.target_immobilize_state = parse_immobilize_state(v);
                gp_cfg.apply_immobilize_state  = true;
            }
        } else if (std::strcmp(k, "r") == 0 || std::strcmp(k, "role") == 0) {
            if (v[0] == '\0' || std::strcmp(v, "none") == 0 || std::strcmp(v, "off") == 0) {
                gp_cfg.apply_player_role  = false;
                gp_cfg.target_player_role = PlayerRole::None;
            } else {
                gp_cfg.target_player_role = parse_player_role(v);
                gp_cfg.apply_player_role  = (gp_cfg.target_player_role != PlayerRole::None);
            }
        } else if (std::strcmp(k, "c") == 0 || std::strcmp(k, "cats") == 0) {
            unsigned int cmask = 0xFFU;
            if (std::sscanf(v, "%x", &cmask) == 1) {
                data.show_antagonist    = (cmask & 0x01U) != 0U;
                data.show_teammates     = (cmask & 0x02U) != 0U;
                data.show_power_nodes   = (cmask & 0x04U) != 0U;
                data.show_shrines       = (cmask & 0x08U) != 0U;
                data.show_containers    = (cmask & 0x10U) != 0U;
                data.show_barriers      = (cmask & 0x20U) != 0U;
                data.show_portals       = (cmask & 0x40U) != 0U;
                data.show_special_props = (cmask & 0x80U) != 0U;
            }
        }
    }
    return true;
}

// Validates current engine state and computes clamped physics targets from KinematicsSyncResult
[[nodiscard]] inline bool compute_clamped_physics_targets(
    const KinematicsSyncResult& sync,
    float                       current_walk,
    float*                      out_walk,
    float*                      out_accel,
    float*                      out_jump,
    float*                      out_time) noexcept {

    if (!sync.is_safe_to_apply ||
        !detail::is_valid_engine_base_value(current_walk)) {
        return false;
    }

    const float walk_Mult  = std::clamp(detail::sanitize_float(sync.walk_speed_mult, 1.0f), 1.0f, 2.5f);
    const float accel_Mult = std::clamp(detail::sanitize_float(sync.accel_mult,      1.0f), 1.0f, 3.0f);
    const float jump_Mult  = std::clamp(detail::sanitize_float(sync.jump_mult,       1.0f), 1.0f, 2.5f);
    const float time_Mult  = std::clamp(detail::sanitize_float(sync.time_scale_mult, 1.0f), 0.5f, 2.5f);

    const float current_accel = (out_accel != nullptr && detail::is_valid_engine_base_value(*out_accel))
        ? *out_accel
        : 2048.0f;
    const float current_jump  = (out_jump != nullptr && detail::is_valid_engine_base_value(*out_jump))
        ? *out_jump
        : 420.0f;

    const float clamped_walk  = std::clamp(current_walk  * walk_Mult,  100.0f, 1800.0f);
    const float clamped_accel = std::clamp(current_accel * accel_Mult, 200.0f, 8192.0f);
    const float clamped_jump  = std::clamp(current_jump  * jump_Mult,  100.0f, 1600.0f);
    const float clamped_time  = std::clamp(1.0f          * time_Mult,  0.5f,   2.5f);

    if (out_walk != nullptr) {
        *out_walk = clamped_walk;
    }
    if (out_accel != nullptr) {
        *out_accel = clamped_accel;
    }
    if (out_jump != nullptr) {
        *out_jump = clamped_jump;
    }
    if (out_time != nullptr) {
        *out_time = clamped_time;
    }

    return true;
}

[[nodiscard]] inline bool compute_clamped_physics_targets(
    const KinematicsSyncResult& sync,
    float                       current_walk,
    float&                      out_walk,
    float&                      out_accel,
    float&                      out_jump,
    float&                      out_time) noexcept {

    return compute_clamped_physics_targets(
        sync, current_walk, &out_walk, &out_accel, &out_jump, &out_time);
}

// Primary synchronization entry point for 60 Hz worker thread (19..22 parameters)
inline KinematicsSyncResult sync_telemetry_to_hud(
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
    const char*            current_player_name,
    bool*                  out_should_apply_player_name = nullptr,
    char*                  out_custom_player_name       = nullptr,
    std::size_t            out_custom_player_name_size  = kMaxLabelLen) noexcept {

    KinematicsSyncResult result{};

    // 1. Sanitize camera state & validate active match state before acquiring lock
    const Vec3 sanitized_cam_loc{
        detail::sanitize_float(cam_x, 0.0f),
        detail::sanitize_float(cam_y, 0.0f),
        detail::sanitize_float(cam_z, 0.0f)
    };
    const EulerAngles sanitized_cam_rot{
        detail::sanitize_float(pitch, 0.0f),
        detail::sanitize_float(yaw, 0.0f),
        detail::sanitize_float(roll, 0.0f)
    };
    const float sanitized_cam_fov = std::clamp(detail::sanitize_float(fov, 90.0f), 10.0f, 170.0f);

    const bool camera_valid =
        std::isfinite(cam_x) && std::isfinite(cam_y) && std::isfinite(cam_z) &&
        (std::abs(sanitized_cam_loc.x) > 1.0f ||
         std::abs(sanitized_cam_loc.y) > 1.0f ||
         std::abs(sanitized_cam_loc.z) > 1.0f);

    constexpr std::size_t kMaxScanEntities = 2048;
    const std::size_t raw_bounded = (detail::is_valid_user_ptr(entities) && entity_count > 0)
        ? std::min(entity_count, kMaxScanEntities)
        : 0;

    // 2. Filter, sort, deduplicate, and compute skeleton joints into local buffer outside the lock
    struct PrioritizedCandidate {
        std::size_t       src_index{0};
        hud::NodeCategory category{hud::NodeCategory::SpecialProp};
        int               priority{7};
        float             dist_m{0.0f};
    };

    PrioritizedCandidate candidates[kMaxScanEntities]{};
    std::size_t          candidate_count   = 0;
    std::size_t          best_killer_index = kMaxScanEntities + 1;
    int                  best_killer_score = -1;
    float                best_killer_dist  = 1e9f;
    std::size_t          generator_count   = 0;
    std::size_t          character_count   = 0;
    std::size_t          objective_count   = 0;

    for (std::size_t i = 0; i < raw_bounded; ++i) {
        const RawEntitySample& src = entities[i];
        if (!is_valid_world_coordinate(src.x, src.y, src.z) || !std::isfinite(src.dist_m)) {
            continue;
        }
        const float sx    = src.x;
        const float sy    = src.y;
        const float sz    = src.z;
        const float sdist = std::max(0.0f, src.dist_m);

        if (std::abs(sx) > 500000.0f || std::abs(sy) > 500000.0f || std::abs(sz) > 500000.0f ||
            sdist < 0.5f || sdist > 1000.0f) {
            continue;
        }

        hud::NodeCategory cat = (src.name != nullptr) ? classify_node_category(src.name) : src.category;
        char              clean_label[kMaxLabelLen]{};
        detail::classify_sample(src, cat, clean_label);

        if (std::strcmp(clean_label, "Unknown") == 0 ||
            detail::icontains_str(src.name, "Destroyed") ||
            detail::icontains_str(src.name, "Cleansed")) {
            continue;
        }

        // Skip completed PowerNodes (progress_pct >= 99.5f)
        if (cat == hud::NodeCategory::PowerNode &&
            std::isfinite(src.progress_pct) &&
            src.progress_pct >= 99.5f) {
            continue;
        }

        // Skip inactive/destroyed or distant (> 150m) Shrines and Barriers
        if (cat == hud::NodeCategory::Shrine || cat == hud::NodeCategory::Barrier) {
            if (!std::isfinite(src.progress_pct) || src.progress_pct < 0.0f || sdist > 150.0f) {
                continue;
            }
        }

        if (cat == hud::NodeCategory::Antagonist && detail::is_true_killer_actor(src.name, cat)) {
            int score = 1;
            if (src.has_skeleton) {
                score = 4;
            } else if (detail::is_valid_user_ptr(src.name) &&
                       (std::strcmp(src.name, "Killer") == 0 ||
                        std::strcmp(src.name, "Assassino") == 0 ||
                        detail::icontains_str(src.name, "Slasher_Character"))) {
                score = 3;
            } else if (detail::is_valid_user_ptr(src.name) && detail::icontains_str(src.name, "BP_Slasher")) {
                score = 2;
            }
            if (score > best_killer_score || (score == best_killer_score && sdist < best_killer_dist)) {
                best_killer_score = score;
                best_killer_dist  = sdist;
                best_killer_index = i;
            }
        } else if (cat == hud::NodeCategory::PowerNode) {
            ++generator_count;
            ++objective_count;
        } else if (cat == hud::NodeCategory::Teammate) {
            ++character_count;
        } else if (cat != hud::NodeCategory::Antagonist) {
            ++objective_count;
        }

        candidates[candidate_count++] = PrioritizedCandidate{
            i,
            cat,
            detail::category_priority(cat),
            sdist
        };
    }

    if (best_killer_index < raw_bounded) {
        ++character_count;
    }

    // Sort candidates so Antagonist (0), Teammate (1), PowerNode (2), Portal (3), Shrine (4)
    // are always placed before Container (5), Barrier (6), and SpecialProp (7), and ordered by distance.
    std::sort(
        candidates,
        candidates + candidate_count,
        [](const PrioritizedCandidate& a, const PrioritizedCandidate& b) noexcept {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.dist_m < b.dist_m;
        });

    const bool walk_speed_valid = detail::is_valid_engine_base_value(current_walk_speed);
    const bool is_match_live =
        camera_valid &&
        (walk_speed_valid || (raw_bounded > 0 && (generator_count >= 1 || objective_count >= 1 || character_count >= 1)));

    SceneNode   local_nodes[kMaxSceneNodes]{};
    std::size_t accepted_count = 0;
    if (is_match_live) {
        bool killer_added = false;
        for (std::size_t c = 0; c < candidate_count && accepted_count < kMaxSceneNodes; ++c) {
            const std::size_t      i   = candidates[c].src_index;
            const RawEntitySample& src = entities[i];

            const float       sx    = src.x;
            const float       sy    = src.y;
            const float       sz    = src.z;
            const float       sdist = candidates[c].dist_m;
            hud::NodeCategory cat   = candidates[c].category;
            char              clean_label[kMaxLabelLen]{};
            detail::classify_sample(src, cat, clean_label);

            if (cat == hud::NodeCategory::Antagonist) {
                if (killer_added || i != best_killer_index) {
                    continue;
                }
                killer_added = true;
            }

            // Deduplicate identical entities at the exact same world coordinates (< 0.30m apart)
            bool is_duplicate = false;
            for (std::size_t j = 0; j < accepted_count; ++j) {
                const float dx = local_nodes[j].world_pos.x - sx;
                const float dy = local_nodes[j].world_pos.y - sy;
                const float dz = local_nodes[j].world_pos.z - sz;
                if ((dx * dx + dy * dy + dz * dz) < (30.0f * 30.0f) &&
                    local_nodes[j].category == cat) {
                    is_duplicate = true;
                    break;
                }
            }
            if (is_duplicate) {
                continue;
            }

            SceneNode& dst   = local_nodes[accepted_count++];
            dst.world_pos    = {sx, sy, sz};
            dst.distance_m   = sdist;
            dst.category     = (src.name != nullptr) ? classify_node_category(src.name) : cat;
            dst.yaw_deg      = detail::sanitize_float(src.yaw_deg, 0.0f);
            dst.progress_pct = std::isfinite(src.progress_pct) ? src.progress_pct : 0.0f;
            dst.is_open      = src.is_open;
            dst.has_skeleton = false;

            detail::copy_raw_string(dst.label, sizeof(dst.label), clean_label);
        }
    }

    // 3. Acquire lock only for swapping local_nodes and updating shared telemetry/kinematics state
    SceneTelemetryData&         data   = get_telemetry_data();
    detail::ScopedPthreadLock   lock(&data.mutex);
    GameplayManipulationConfig& gp_cfg = get_gameplay_config();

    if (std::strncmp(data.label_filter, "@gp:", 4) == 0) {
        (void)parse_gameplay_ipc_string(data.label_filter, data, gp_cfg);
    }

    data.current_camera.location = sanitized_cam_loc;
    data.current_camera.rotation = sanitized_cam_rot;
    data.current_camera.fov      = sanitized_cam_fov;

    if (!camera_valid) {
        data.node_count        = 0;
        data.active_generators = 0;
        data.match_active      = false;
        gp_cfg.force_escape    = false;
    } else {
        if (accepted_count > 0) {
            std::memcpy(data.nodes, local_nodes, accepted_count * sizeof(SceneNode));
        }
        data.node_count        = accepted_count;
        data.active_generators = static_cast<std::uint32_t>(generator_count);
        data.match_active      = is_match_live;
    }
    ++data.tick_seq;

    if (current_player_name && current_player_name[0] != '\0') {
        detail::copy_raw_string(
            data.in_game_player_name, sizeof(data.in_game_player_name), current_player_name);
    }

    // 3. Bidirectional player display name synchronization
    if (data.apply_custom_player_name && data.custom_player_name[0] != '\0') {
        result.should_apply_player_name = true;
        if (out_should_apply_player_name) {
            *out_should_apply_player_name = true;
        }
    } else {
        if (current_player_name && current_player_name[0] != '\0' && !data.apply_custom_player_name) {
            detail::copy_raw_string(
                data.custom_player_name, sizeof(data.custom_player_name), current_player_name);
        }
        result.should_apply_player_name = false;
        if (out_should_apply_player_name) {
            *out_should_apply_player_name = false;
        }
    }
    detail::copy_raw_string(
        result.custom_player_name, sizeof(result.custom_player_name), data.custom_player_name);
    if (out_custom_player_name) {
        detail::copy_raw_string(
            out_custom_player_name, out_custom_player_name_size, data.custom_player_name);
    }

    // 4. Convert percentage sliders (100% - 250% / 300% / 50% - 200%) cleanly into multiplier ratios (1.0f - 2.5f / 3.0f / 0.5f - 2.0f)
    const float clamped_walk_pct  = std::clamp(detail::sanitize_float(data.walk_speed_pct, 100.0f), 100.0f, 250.0f);
    const float clamped_accel_pct = std::clamp(detail::sanitize_float(data.accel_pct, 100.0f),      100.0f, 300.0f);
    const float clamped_jump_pct  = std::clamp(detail::sanitize_float(data.jump_pct, 100.0f),       100.0f, 250.0f);
    const float clamped_time_pct  = std::clamp(detail::sanitize_float(data.time_scale_pct, 100.0f),  50.0f, 200.0f);

    data.walk_speed_pct = clamped_walk_pct;
    data.accel_pct      = clamped_accel_pct;
    data.jump_pct       = clamped_jump_pct;
    data.time_scale_pct = clamped_time_pct;

    const float walk_mult  = clamped_walk_pct  * 0.01f;
    const float accel_mult = clamped_accel_pct * 0.01f;
    const float jump_mult  = clamped_jump_pct  * 0.01f;
    const float time_mult  = clamped_time_pct  * 0.01f;

    // Strict safety guard: is_safe_to_apply is ONLY true when apply_custom_kinematics is enabled,
    // match is live, AND current_walk_speed is a finite number within [50.0f, 5000.0f].
    const bool match_ready      = is_match_live;
    const bool safe_to_apply    =
        data.apply_custom_kinematics &&
        match_ready &&
        walk_speed_valid;

    result.is_safe_to_apply        = safe_to_apply;
    result.should_apply_kinematics = safe_to_apply;
    result.movement_dirty_flag     = safe_to_apply;
    result.walk_speed_mult         = safe_to_apply ? walk_mult  : 1.0f;
    result.accel_mult              = safe_to_apply ? accel_mult : 1.0f;
    result.jump_mult               = safe_to_apply ? jump_mult  : 1.0f;
    result.time_scale_mult         = safe_to_apply ? time_mult  : 1.0f;

    float target_walk  = walk_speed_valid ? current_walk_speed : data.walk_speed;
    float target_accel = detail::is_valid_engine_base_value(current_accel)  ? current_accel  : data.max_acceleration;
    float target_jump  = detail::is_valid_engine_base_value(current_jump_z) ? current_jump_z : data.jump_velocity;
    float target_time  = (std::isfinite(current_time_scale) && current_time_scale > 0.0f)
        ? ((current_time_scale >= 50.0f)
            ? std::clamp(current_time_scale * 0.01f, 0.1f, 5.0f)
            : std::clamp(current_time_scale, 0.1f, 5.0f))
        : data.time_scale;

    const bool clamped_ok = compute_clamped_physics_targets(
        result,
        current_walk_speed,
        &target_walk,
        &target_accel,
        &target_jump,
        &target_time);

    result.is_safe_to_apply        = clamped_ok;
    result.should_apply_kinematics = clamped_ok;
    result.movement_dirty_flag     = clamped_ok || (match_ready && walk_speed_valid && gp_cfg.fly_cheat_enabled);
    data.walk_speed                = target_walk;
    data.max_acceleration          = target_accel;
    data.jump_velocity             = target_jump;
    data.time_scale                = target_time;

    result.walk_speed       = data.walk_speed;
    result.max_acceleration = data.max_acceleration;
    result.jump_velocity    = data.jump_velocity;
    result.time_scale       = data.time_scale;

    if (out_should_apply_kinematics) {
        *out_should_apply_kinematics = clamped_ok;
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

    // 5. Synchronize Advanced Gameplay Manipulation Features (FOV, Auto Skillcheck, Trap Immunity, Fly, Escape, Immobilize, Role)
    gp_cfg.target_fov = std::clamp(detail::sanitize_float(gp_cfg.target_fov, 90.0f), 80.0f, 120.0f);
    if (gp_cfg.apply_fov_override && camera_valid) {
        result.should_apply_fov       = true;
        result.target_fov             = gp_cfg.target_fov;
        data.current_camera.fov       = gp_cfg.target_fov;
    } else {
        result.should_apply_fov       = false;
        result.target_fov             = data.current_camera.fov;
    }

    result.auto_skillcheck         = match_ready && gp_cfg.auto_skillcheck;
    result.trap_immunity           = match_ready && gp_cfg.trap_immunity;
    result.fly_cheat_enabled       = match_ready && walk_speed_valid && gp_cfg.fly_cheat_enabled;
    result.force_escape            = match_ready && walk_speed_valid && gp_cfg.force_escape;
    result.should_apply_immobilize = match_ready && walk_speed_valid && gp_cfg.apply_immobilize_state;
    result.immobilize_state        = gp_cfg.target_immobilize_state;
    result.should_apply_role       = gp_cfg.apply_player_role && (gp_cfg.target_player_role != PlayerRole::None);
    result.player_role             = gp_cfg.target_player_role;

    return result;
}

// Explicit 18-parameter legacy pointer overload (supports 12..18 arguments)
inline KinematicsSyncResult sync_telemetry_to_hud(
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
    float                  current_time_scale          = 1.0f,
    bool*                  out_should_apply_kinematics = nullptr,
    float*                 out_walk_speed              = nullptr,
    float*                 out_accel                   = nullptr,
    float*                 out_jump_z                  = nullptr,
    float*                 out_time_scale              = nullptr) noexcept {

    return sync_telemetry_to_hud(
        cam_x,
        cam_y,
        cam_z,
        pitch,
        yaw,
        roll,
        fov,
        entities,
        entity_count,
        current_walk_speed,
        current_accel,
        current_jump_z,
        current_time_scale,
        out_should_apply_kinematics,
        out_walk_speed,
        out_accel,
        out_jump_z,
        out_time_scale,
        nullptr,
        nullptr,
        nullptr,
        kMaxLabelLen);
}

// Convenience C++20 std::span overload returning KinematicsSyncResult by value
[[nodiscard]] inline KinematicsSyncResult sync_telemetry_to_hud(
    const CameraView&                camera,
    std::span<const RawEntitySample> entities,
    float                            current_walk_speed,
    float                            current_accel,
    float                            current_jump_z,
    float                            current_time_scale  = 1.0f,
    const char*                      current_player_name = nullptr) noexcept {

    return sync_telemetry_to_hud(
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
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        current_player_name,
        nullptr,
        nullptr,
        kMaxLabelLen);
}

} // namespace hud::adapter

namespace hud {
using adapter::ImmobilizeState;
using adapter::PlayerRole;
using adapter::MovementMode;
using adapter::GameplayManipulationConfig;
using adapter::get_gameplay_config;
using adapter::set_fov_override;
using adapter::set_auto_skillcheck;
using adapter::set_trap_immunity;
using adapter::set_fly_cheat;
using adapter::set_force_escape;
using adapter::set_immobilize_state;
using adapter::set_player_role;
using adapter::parse_gameplay_ipc_string;
using adapter::compute_clamped_physics_targets;
} // namespace hud

using hud::adapter::compute_clamped_physics_targets;
