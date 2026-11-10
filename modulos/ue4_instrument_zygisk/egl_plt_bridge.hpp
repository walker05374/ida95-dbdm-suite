// File: egl_plt_bridge.hpp
// ============================================================================
// Architecture : ELF64 AArch64 (arm64-v8a)
// Standard     : C++20 (-fno-exceptions -fno-rtti)
// Purpose      : ELF64 PT_DYNAMIC / DT_JMPREL GOT slot bridge for eglSwapBuffers
// ============================================================================
#pragma once

#include "imgui_gles3_hud.hpp"

#include <EGL/egl.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

namespace egl_bridge {

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surface);

namespace detail {

inline std::atomic<EglSwapBuffersFn> g_orig_eglSwapBuffers{nullptr};
inline std::atomic<bool>             g_bridge_installed{false};

[[nodiscard]] inline std::uintptr_t resolve_dynamic_ptr(
    std::uintptr_t load_bias,
    std::uintptr_t module_base,
    std::uintptr_t raw_val) noexcept {
    if (raw_val == 0) {
        return 0;
    }
    return (raw_val < module_base) ? (load_bias + raw_val) : raw_val;
}

[[nodiscard]] inline bool patch_got_slot(
    std::uintptr_t*  got_slot,
    EglSwapBuffersFn replacement,
    EglSwapBuffersFn fallback_orig) noexcept {

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

    const auto current_val = reinterpret_cast<EglSwapBuffersFn>(*got_slot);
    EglSwapBuffersFn expected = nullptr;
    if (current_val != nullptr && current_val != replacement) {
        g_orig_eglSwapBuffers.compare_exchange_strong(
            expected, current_val, std::memory_order_acq_rel);
    } else if (fallback_orig != nullptr) {
        g_orig_eglSwapBuffers.compare_exchange_strong(
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

[[nodiscard]] inline bool scan_rela_table(
    std::uintptr_t    load_bias,
    const Elf64_Rela* rela_entries,
    std::size_t       rela_byte_size,
    const Elf64_Sym*  symtab,
    const char*       strtab,
    EglSwapBuffersFn  replacement,
    EglSwapBuffersFn  fallback_orig) noexcept {

    if (!rela_entries || rela_byte_size < sizeof(Elf64_Rela) || !symtab || !strtab) {
        return false;
    }

    const std::size_t count = rela_byte_size / sizeof(Elf64_Rela);
    bool patched = false;

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
        if (std::strcmp(sym_name, "eglSwapBuffers") == 0) {
            auto* got_slot = reinterpret_cast<std::uintptr_t*>(load_bias + rela.r_offset);
            if (patch_got_slot(got_slot, replacement, fallback_orig)) {
                patched = true;
            }
        }
    }

    return patched;
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

inline bool install_egl_bridge(std::uintptr_t module_base) noexcept {
    if (module_base == 0) {
        return false;
    }

    if (detail::g_bridge_installed.load(std::memory_order_acquire)) {
        return true;
    }

    // 1. Resolve canonical eglSwapBuffers symbol from libEGL.so via dlopen / dlsym
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

    EglSwapBuffersFn expected_fn = nullptr;
    detail::g_orig_eglSwapBuffers.compare_exchange_strong(
        expected_fn, resolved_egl_swap, std::memory_order_acq_rel);

    // 2. Validate ELF64 header at module_base
    const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(module_base);
    if (std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_phoff == 0 ||
        ehdr->e_phnum == 0) {
        return false;
    }

    const auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(module_base + ehdr->e_phoff);

    // Determine load_bias from minimum PT_LOAD virtual address
    std::uintptr_t min_vaddr = UINTPTR_MAX;
    const Elf64_Phdr* dyn_phdr = nullptr;

    for ( Elf64_Half i = 0; i < ehdr->e_phnum; ++i ) {
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

    // 3. Extract DT_SYMTAB, DT_STRTAB, DT_JMPREL, DT_PLTRELSZ (plus DT_RELA fallback)
    const Elf64_Sym*  symtab     = nullptr;
    const char*       strtab     = nullptr;
    const Elf64_Rela* jmprel     = nullptr;
    std::size_t       pltrelsz   = 0;
    const Elf64_Rela* rela_dyn   = nullptr;
    std::size_t       rela_dynsz = 0;

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
            default:
                break;
        }
    }

    if (!symtab || !strtab) {
        return false;
    }

    // 4. Patch eglSwapBuffers GOT slot in DT_JMPREL (and DT_RELA if bound via GLOB_DAT)
    bool hooked = detail::scan_rela_table(
        load_bias, jmprel, pltrelsz, symtab, strtab, &hook_eglSwapBuffers, resolved_egl_swap);

    if (rela_dyn && rela_dynsz > 0) {
        hooked = detail::scan_rela_table(
            load_bias, rela_dyn, rela_dynsz, symtab, strtab, &hook_eglSwapBuffers, resolved_egl_swap) || hooked;
    }

    if (hooked) {
        detail::g_bridge_installed.store(true, std::memory_order_release);
    }

    return hooked;
}

} // namespace egl_bridge
