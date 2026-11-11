// File: egl_plt_bridge.hpp
// ============================================================================
// Architecture : ELF64 AArch64 (arm64-v8a)
// Standard     : C++20 (-fno-exceptions -fno-rtti)
// Purpose      : ELF64 PT_DYNAMIC / DT_JMPREL GOT slot bridge for eglSwapBuffers
//                and Android AInputQueue / AMotionEvent / InputConsumer touch hooks
// ============================================================================
#pragma once

#include "imgui_gles3_hud.hpp"

#include <EGL/egl.h>
#include <android/input.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>

namespace egl_bridge {

using EglSwapBuffersFn              = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surface);
using AInputQueueGetEventFn         = int32_t (*)(AInputQueue* queue, AInputEvent** outEvent);
using AInputQueuePreDispatchEventFn = int32_t (*)(AInputQueue* queue, AInputEvent* event);
using AInputQueueFinishEventFn      = void (*)(AInputQueue* queue, AInputEvent* event, int handled);
using AInputEventGetTypeFn          = int32_t (*)(const AInputEvent* event);
using AMotionEventGetActionFn       = int32_t (*)(const AInputEvent* motion_event);
using AMotionEventGetXFn            = float (*)(const AInputEvent* motion_event, size_t pointer_index);
using AMotionEventGetYFn            = float (*)(const AInputEvent* motion_event, size_t pointer_index);
using AKeyEventGetActionFn          = int32_t (*)(const AInputEvent* key_event);
using AKeyEventGetKeyCodeFn         = int32_t (*)(const AInputEvent* key_event);
using AKeyEventGetMetaStateFn       = int32_t (*)(const AInputEvent* key_event);
using InitializeMotionEventFn       = int32_t (*)(void* this_consumer, void* motion_event, const void* input_msg);

namespace detail {

inline std::atomic<EglSwapBuffersFn>              g_orig_eglSwapBuffers{nullptr};
inline std::atomic<AInputQueueGetEventFn>         g_orig_AInputQueue_getEvent{nullptr};
inline std::atomic<AInputQueuePreDispatchEventFn> g_orig_AInputQueue_preDispatchEvent{nullptr};
inline std::atomic<AInputQueueFinishEventFn>      g_orig_AInputQueue_finishEvent{nullptr};
inline std::atomic<AInputEventGetTypeFn>          g_orig_AInputEvent_getType{nullptr};
inline std::atomic<AMotionEventGetActionFn>       g_orig_AMotionEvent_getAction{nullptr};
inline std::atomic<AMotionEventGetXFn>            g_orig_AMotionEvent_getX{nullptr};
inline std::atomic<AMotionEventGetYFn>            g_orig_AMotionEvent_getY{nullptr};
inline std::atomic<AKeyEventGetActionFn>          g_orig_AKeyEvent_getAction{nullptr};
inline std::atomic<AKeyEventGetKeyCodeFn>         g_orig_AKeyEvent_getKeyCode{nullptr};
inline std::atomic<AKeyEventGetMetaStateFn>       g_orig_AKeyEvent_getMetaState{nullptr};
inline std::atomic<InitializeMotionEventFn>       g_orig_initializeMotionEvent{nullptr};

inline std::atomic<const AInputEvent*>            g_last_motion_event{nullptr};
inline std::atomic<const AInputEvent*>            g_last_key_event{nullptr};
inline std::atomic<bool>                          g_bridge_installed{false};

[[nodiscard]] inline std::uintptr_t resolve_dynamic_ptr(
    std::uintptr_t load_bias,
    std::uintptr_t module_base,
    std::uintptr_t raw_val) noexcept {
    if (raw_val == 0) {
        return 0;
    }
    return (raw_val < module_base) ? (load_bias + raw_val) : raw_val;
}

template <typename FnPtr>
[[nodiscard]] inline bool patch_got_slot_typed(
    std::uintptr_t*            got_slot,
    std::type_identity_t<FnPtr> replacement,
    std::atomic<FnPtr>&        orig_storage,
    std::type_identity_t<FnPtr> fallback_orig) noexcept {

    if (!got_slot || !replacement) {
        return false;
    }

    const long sys_page_size = ::sysconf(_SC_PAGESIZE);
    const std::size_t page_size = (sys_page_size > 0)
        ? static_cast<std::size_t>(sys_page_size)
        : 4096ULL;

    const auto slot_addr  = reinterpret_cast<std::uintptr_t>(got_slot);
    const auto page_start = slot_addr & ~(page_size - 1ULL);

    if (::mprotect(reinterpret_cast<void*>(page_start), page_size, PROT_READ | PROT_WRITE) != 0) {
        return false;
    }

    const auto current_val = reinterpret_cast<FnPtr>(*got_slot);
    FnPtr expected = nullptr;
    if (current_val != nullptr && current_val != replacement) {
        orig_storage.compare_exchange_strong(
            expected, current_val, std::memory_order_acq_rel);
    } else if (fallback_orig != nullptr) {
        orig_storage.compare_exchange_strong(
            expected, fallback_orig, std::memory_order_acq_rel);
    }

    __atomic_store_n(
        got_slot,
        reinterpret_cast<std::uintptr_t>(replacement),
        __ATOMIC_RELEASE);

    __builtin___clear_cache(
        reinterpret_cast<char*>(got_slot),
        reinterpret_cast<char*>(got_slot) + sizeof(std::uintptr_t));

    return true;
}

inline void extract_and_forward_motion_event(
    const AInputEvent* event,
    int32_t&           out_action,
    float&             out_x,
    float&             out_y) noexcept {

    auto get_action = g_orig_AMotionEvent_getAction.load(std::memory_order_acquire);
    auto get_x      = g_orig_AMotionEvent_getX.load(std::memory_order_acquire);
    auto get_y      = g_orig_AMotionEvent_getY.load(std::memory_order_acquire);

    out_action = get_action ? get_action(event) : ::AMotionEvent_getAction(event);

    const int32_t masked = out_action & AMOTION_EVENT_ACTION_MASK;
    std::size_t pointer_idx = 0;
    if (masked == AMOTION_EVENT_ACTION_POINTER_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_UP) {
        pointer_idx = static_cast<std::size_t>(
            (out_action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    }

    out_x = get_x ? get_x(event, pointer_idx) : ::AMotionEvent_getX(event, pointer_idx);
    out_y = get_y ? get_y(event, pointer_idx) : ::AMotionEvent_getY(event, pointer_idx);

    if (g_last_motion_event.exchange(event, std::memory_order_acq_rel) != event ||
        masked == AMOTION_EVENT_ACTION_MOVE) {
        hud::on_touch_event(out_action, out_x, out_y);
    }
}

inline void extract_and_forward_key_event(const AInputEvent* event) noexcept {
    if (!event) {
        return;
    }
    if (g_last_key_event.exchange(event, std::memory_order_acq_rel) == event) {
        return;
    }

    auto get_act  = g_orig_AKeyEvent_getAction.load(std::memory_order_acquire);
    auto get_code = g_orig_AKeyEvent_getKeyCode.load(std::memory_order_acquire);
    auto get_meta = g_orig_AKeyEvent_getMetaState.load(std::memory_order_acquire);

    const int32_t action   = get_act  ? get_act(event)  : ::AKeyEvent_getAction(event);
    const int32_t key_code = get_code ? get_code(event) : ::AKeyEvent_getKeyCode(event);
    const int32_t meta     = get_meta ? get_meta(event) : ::AKeyEvent_getMetaState(event);

    hud::on_key_event(action, key_code, meta, 0);
}

} // namespace detail

inline EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) noexcept {
    hud::on_egl_frame_swap(dpy, surface);

    EglSwapBuffersFn orig = detail::g_orig_eglSwapBuffers.load(std::memory_order_acquire);
    if (!orig) {
        return ::eglSwapBuffers(dpy, surface);
    }
    return orig(dpy, surface);
}

inline int32_t hook_AInputQueue_getEvent(AInputQueue* queue, AInputEvent** outEvent) noexcept {
    auto orig_get = detail::g_orig_AInputQueue_getEvent.load(std::memory_order_acquire);
    if (!orig_get) {
        orig_get = &::AInputQueue_getEvent;
    }

    const int32_t res = orig_get(queue, outEvent);
    if (res >= 0 && outEvent != nullptr && *outEvent != nullptr) {
        auto get_type = detail::g_orig_AInputEvent_getType.load(std::memory_order_acquire);
        const int32_t ev_type = get_type ? get_type(*outEvent) : ::AInputEvent_getType(*outEvent);
        if (ev_type == AINPUT_EVENT_TYPE_MOTION) {
            int32_t action = 0;
            float   x      = 0.0f;
            float   y      = 0.0f;
            detail::extract_and_forward_motion_event(*outEvent, action, x, y);
        } else if (ev_type == AINPUT_EVENT_TYPE_KEY) {
            detail::extract_and_forward_key_event(*outEvent);
        }
    }
    return res;
}

inline int32_t hook_AInputQueue_preDispatchEvent(AInputQueue* queue, AInputEvent* event) noexcept {
    auto orig_pre = detail::g_orig_AInputQueue_preDispatchEvent.load(std::memory_order_acquire);
    if (!orig_pre) {
        orig_pre = &::AInputQueue_preDispatchEvent;
    }

    const int32_t pre_res = orig_pre(queue, event);
    if (pre_res != 0) {
        return pre_res;
    }

    if (event != nullptr) {
        auto get_type = detail::g_orig_AInputEvent_getType.load(std::memory_order_acquire);
        const int32_t ev_type = get_type ? get_type(event) : ::AInputEvent_getType(event);
        if (ev_type == AINPUT_EVENT_TYPE_MOTION) {
            int32_t action = 0;
            float   x      = 0.0f;
            float   y      = 0.0f;
            detail::extract_and_forward_motion_event(event, action, x, y);

            // When touching inside the ImGui HUD window, consume the event here so it does not pass through to the game
            if (hud::should_consume_touch(action, x, y)) {
                auto orig_finish = detail::g_orig_AInputQueue_finishEvent.load(std::memory_order_acquire);
                if (!orig_finish) {
                    orig_finish = &::AInputQueue_finishEvent;
                }
                orig_finish(queue, event, 1);
                return 1;
            }
        } else if (ev_type == AINPUT_EVENT_TYPE_KEY) {
            detail::extract_and_forward_key_event(event);
        }
    }
    return 0;
}

inline int32_t hook_AMotionEvent_getAction(const AInputEvent* motion_event) noexcept {
    auto orig_action = detail::g_orig_AMotionEvent_getAction.load(std::memory_order_acquire);
    if (!orig_action) {
        orig_action = &::AMotionEvent_getAction;
    }

    if (!motion_event) {
        return 0;
    }

    int32_t action = 0;
    float   x      = 0.0f;
    float   y      = 0.0f;
    detail::extract_and_forward_motion_event(motion_event, action, x, y);

    if (hud::should_consume_touch(action, x, y)) {
        const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            return AMOTION_EVENT_ACTION_CANCEL;
        }
    }
    return action;
}

inline int32_t hook_initializeMotionEvent(
    void*       this_consumer,
    void*       motion_event,
    const void* input_msg) noexcept {

    auto orig_init = detail::g_orig_initializeMotionEvent.load(std::memory_order_acquire);
    if (!orig_init) {
        return 0;
    }

    const int32_t res = orig_init(this_consumer, motion_event, input_msg);
    if (res == 0 && motion_event != nullptr) {
        const auto* ev = static_cast<const AInputEvent*>(motion_event);
        int32_t action = 0;
        float   x      = 0.0f;
        float   y      = 0.0f;
        detail::extract_and_forward_motion_event(ev, action, x, y);
    }
    return res;
}

namespace detail {

[[nodiscard]] inline bool scan_rela_table(
    std::uintptr_t    load_bias,
    const Elf64_Rela* rela_entries,
    std::size_t       rela_byte_size,
    const Elf64_Sym*  symtab,
    const char*       strtab,
    EglSwapBuffersFn              fallback_egl_swap,
    AInputQueueGetEventFn         fallback_get_event,
    AInputQueuePreDispatchEventFn fallback_pre_dispatch,
    AMotionEventGetActionFn       fallback_get_action,
    InitializeMotionEventFn       fallback_init_motion) noexcept {

    if (!rela_entries || rela_byte_size < sizeof(Elf64_Rela) || !symtab || !strtab) {
        return false;
    }

    const std::size_t count = rela_byte_size / sizeof(Elf64_Rela);
    bool patched_egl = false;

    for (std::size_t i = 0; i < count; ++i) {
        const Elf64_Rela& rela = rela_entries[i];
        const std::uint32_t sym_idx  = ELF64_R_SYM(rela.r_info);
        const std::uint32_t rel_type = ELF64_R_TYPE(rela.r_info);

        // R_AARCH64_JUMP_SLOT (1026) or R_AARCH64_GLOB_DAT (1025)
        if (rel_type != R_AARCH64_JUMP_SLOT && rel_type != R_AARCH64_GLOB_DAT) {
            continue;
        }
        if (sym_idx == 0) {
            continue;
        }

        const Elf64_Sym& sym = symtab[sym_idx];
        if (sym.st_name == 0) {
            continue;
        }

        const char* sym_name = strtab + sym.st_name;
        auto* got_slot = reinterpret_cast<std::uintptr_t*>(load_bias + rela.r_offset);

        if (std::strcmp(sym_name, "eglSwapBuffers") == 0) {
            if (patch_got_slot_typed(
                    got_slot, &hook_eglSwapBuffers, g_orig_eglSwapBuffers, fallback_egl_swap)) {
                patched_egl = true;
            }
        } else if (std::strcmp(sym_name, "AInputQueue_getEvent") == 0) {
            (void)patch_got_slot_typed(
                got_slot, &hook_AInputQueue_getEvent, g_orig_AInputQueue_getEvent, fallback_get_event);
        } else if (std::strcmp(sym_name, "AInputQueue_preDispatchEvent") == 0) {
            (void)patch_got_slot_typed(
                got_slot, &hook_AInputQueue_preDispatchEvent, g_orig_AInputQueue_preDispatchEvent, fallback_pre_dispatch);
        } else if (std::strcmp(sym_name, "AMotionEvent_getAction") == 0) {
            (void)patch_got_slot_typed(
                got_slot, &hook_AMotionEvent_getAction, g_orig_AMotionEvent_getAction, fallback_get_action);
        } else if (std::strcmp(
                       sym_name,
                       "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE") == 0) {
            (void)patch_got_slot_typed(
                got_slot, &hook_initializeMotionEvent, g_orig_initializeMotionEvent, fallback_init_motion);
        }
    }

    return patched_egl;
}

} // namespace detail

inline bool install_egl_bridge(std::uintptr_t module_base) noexcept {
    if (module_base == 0) {
        return false;
    }

    if (detail::g_bridge_installed.load(std::memory_order_acquire)) {
        return true;
    }

    // 1. Resolve canonical symbols from libEGL.so and libandroid.so via dlopen / dlsym
    void* egl_handle = ::dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    if (!egl_handle) {
        egl_handle = ::dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
    }

    EglSwapBuffersFn resolved_egl_swap = nullptr;
    if (egl_handle) {
        resolved_egl_swap = reinterpret_cast<EglSwapBuffersFn>(
            ::dlsym(egl_handle, "eglSwapBuffers"));
    }
    if (!resolved_egl_swap) {
        resolved_egl_swap = &::eglSwapBuffers;
    }

    void* android_handle = ::dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (!android_handle) {
        android_handle = ::dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
    }

    auto resolve_android_sym = [android_handle]<typename Fn>(const char* name, Fn fallback) noexcept -> Fn {
        if (android_handle) {
            if (void* sym = ::dlsym(android_handle, name)) {
                return reinterpret_cast<Fn>(sym);
            }
        }
        return fallback;
    };

    const auto resolved_get_event = resolve_android_sym("AInputQueue_getEvent", &::AInputQueue_getEvent);
    const auto resolved_pre_disp  = resolve_android_sym("AInputQueue_preDispatchEvent", &::AInputQueue_preDispatchEvent);
    const auto resolved_finish_ev = resolve_android_sym("AInputQueue_finishEvent", &::AInputQueue_finishEvent);
    const auto resolved_get_type  = resolve_android_sym("AInputEvent_getType", &::AInputEvent_getType);
    const auto resolved_get_act   = resolve_android_sym("AMotionEvent_getAction", &::AMotionEvent_getAction);
    const auto resolved_get_x     = resolve_android_sym("AMotionEvent_getX", &::AMotionEvent_getX);
    const auto resolved_get_y     = resolve_android_sym("AMotionEvent_getY", &::AMotionEvent_getY);
    const auto resolved_key_act   = resolve_android_sym("AKeyEvent_getAction", &::AKeyEvent_getAction);
    const auto resolved_key_code  = resolve_android_sym("AKeyEvent_getKeyCode", &::AKeyEvent_getKeyCode);
    const auto resolved_key_meta  = resolve_android_sym("AKeyEvent_getMetaState", &::AKeyEvent_getMetaState);

    detail::g_orig_eglSwapBuffers.store(resolved_egl_swap, std::memory_order_release);
    detail::g_orig_AInputQueue_getEvent.store(resolved_get_event, std::memory_order_release);
    detail::g_orig_AInputQueue_preDispatchEvent.store(resolved_pre_disp, std::memory_order_release);
    detail::g_orig_AInputQueue_finishEvent.store(resolved_finish_ev, std::memory_order_release);
    detail::g_orig_AInputEvent_getType.store(resolved_get_type, std::memory_order_release);
    detail::g_orig_AMotionEvent_getAction.store(resolved_get_act, std::memory_order_release);
    detail::g_orig_AMotionEvent_getX.store(resolved_get_x, std::memory_order_release);
    detail::g_orig_AMotionEvent_getY.store(resolved_get_y, std::memory_order_release);
    detail::g_orig_AKeyEvent_getAction.store(resolved_key_act, std::memory_order_release);
    detail::g_orig_AKeyEvent_getKeyCode.store(resolved_key_code, std::memory_order_release);
    detail::g_orig_AKeyEvent_getMetaState.store(resolved_key_meta, std::memory_order_release);

    void* input_handle = ::dlopen("libinput.so", RTLD_NOW | RTLD_NOLOAD);
    InitializeMotionEventFn resolved_init_motion = nullptr;
    if (input_handle) {
        resolved_init_motion = reinterpret_cast<InitializeMotionEventFn>(
            ::dlsym(input_handle, "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE"));
        if (resolved_init_motion) {
            detail::g_orig_initializeMotionEvent.store(resolved_init_motion, std::memory_order_release);
        }
    }

    // 2. Validate ELF64 header at module_base
    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(module_base);
    if (std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_phoff == 0 ||
        ehdr->e_phnum == 0) {
        return false;
    }

    const auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(module_base + ehdr->e_phoff);

    std::uintptr_t min_vaddr = UINTPTR_MAX;
    const Elf64_Phdr* dyn_phdr = nullptr;

    for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr& phdr = phdrs[i];
        if (phdr.p_type == PT_LOAD && phdr.p_vaddr < min_vaddr) {
            min_vaddr = static_cast<std::uintptr_t>(phdr.p_vaddr);
        } else if (phdr.p_type == PT_DYNAMIC) {
            dyn_phdr = &phdr;
        }
    }

    if (!dyn_phdr || min_vaddr == UINTPTR_MAX) {
        return false;
    }

    const std::uintptr_t load_bias = module_base - min_vaddr;
    const auto* dyn_table = reinterpret_cast<const Elf64_Dyn*>(load_bias + dyn_phdr->p_vaddr);

    // 3. Extract DT_SYMTAB, DT_STRTAB, DT_JMPREL, DT_PLTRELSZ (plus DT_RELA / DT_RELACOUNT)
    const Elf64_Sym*  symtab     = nullptr;
    const char*       strtab     = nullptr;
    const Elf64_Rela* jmprel     = nullptr;
    std::size_t       pltrelsz   = 0;
    const Elf64_Rela* rela_dyn   = nullptr;
    std::size_t       rela_dynsz = 0;
    std::size_t       relacount  = 0;

    for (const Elf64_Dyn* dyn = dyn_table; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
            case DT_SYMTAB:
                symtab = reinterpret_cast<const Elf64_Sym*>(
                    detail::resolve_dynamic_ptr(load_bias, module_base, dyn->d_un.d_ptr));
                break;
            case DT_STRTAB:
                strtab = reinterpret_cast<const char*>(
                    detail::resolve_dynamic_ptr(load_bias, module_base, dyn->d_un.d_ptr));
                break;
            case DT_JMPREL:
                jmprel = reinterpret_cast<const Elf64_Rela*>(
                    detail::resolve_dynamic_ptr(load_bias, module_base, dyn->d_un.d_ptr));
                break;
            case DT_PLTRELSZ:
                pltrelsz = static_cast<std::size_t>(dyn->d_un.d_val);
                break;
            case DT_RELA:
                rela_dyn = reinterpret_cast<const Elf64_Rela*>(
                    detail::resolve_dynamic_ptr(load_bias, module_base, dyn->d_un.d_ptr));
                break;
            case DT_RELASZ:
                rela_dynsz = static_cast<std::size_t>(dyn->d_un.d_val);
                break;
            case DT_RELACOUNT:
                relacount = static_cast<std::size_t>(dyn->d_un.d_val);
                break;
            default:
                break;
        }
    }

    if (!symtab || !strtab) {
        return false;
    }

    // 4. Patch eglSwapBuffers and AInputQueue/AMotionEvent GOT slots in DT_JMPREL
    bool hooked = detail::scan_rela_table(
        load_bias,
        jmprel,
        pltrelsz,
        symtab,
        strtab,
        resolved_egl_swap,
        resolved_get_event,
        resolved_pre_disp,
        resolved_get_act,
        resolved_init_motion);

    // Scan non-relative tail of DT_RELA if present
    if (rela_dyn && rela_dynsz > relacount * sizeof(Elf64_Rela)) {
        const Elf64_Rela* sym_rela     = rela_dyn + relacount;
        const std::size_t sym_rela_sz  = rela_dynsz - (relacount * sizeof(Elf64_Rela));
        hooked = detail::scan_rela_table(
                     load_bias,
                     sym_rela,
                     sym_rela_sz,
                     symtab,
                     strtab,
                     resolved_egl_swap,
                     resolved_get_event,
                     resolved_pre_disp,
                     resolved_get_act,
                     resolved_init_motion) ||
                 hooked;
    }

    if (hooked) {
        detail::g_bridge_installed.store(true, std::memory_order_release);
    }

    return hooked;
}

} // namespace egl_bridge
