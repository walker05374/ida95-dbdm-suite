// ============================================================================
// File          : elf_resolver.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20 (-fno-exceptions -fno-rtti)
// Purpose       : Lock-safe ELF64 module base & PT_LOAD span discovery via
//                 dl_iterate_phdr without parsing /proc/self/maps text
// ============================================================================
#pragma once

#include "visibility.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#if defined(__linux__) || defined(__ANDROID__)
#include <elf.h>
#include <link.h>
#else
struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
};
#endif

namespace runtime::elf {

struct INSTRUMENT_LOCAL ModuleSpan {
    uintptr_t base_address{0};
    size_t    mapped_size{0};
};

[[nodiscard]] INSTRUMENT_LOCAL inline std::optional<ModuleSpan> find_module(std::string_view target_soname) noexcept {
    if (target_soname.empty()) {
        return std::nullopt;
    }

#if defined(__linux__) || defined(__ANDROID__)
    struct PhdrSearchContext {
        std::string_view target;
        ModuleSpan       result{};
        bool             found{false};
    } ctx{target_soname, {}, false};

    auto callback = [](struct dl_phdr_info* info, size_t /*size*/, void* data) noexcept -> int {
        auto* search = static_cast<PhdrSearchContext*>(data);
        if (!info || !info->dlpi_name || info->dlpi_addr == 0) {
            return 0;
        }
        const std::string_view full_path{info->dlpi_name};
        if (!full_path.ends_with(search->target)) {
            return 0;
        }

        uintptr_t min_vaddr = UINTPTR_MAX;
        uintptr_t max_vaddr = 0;

        for (uint16_t i = 0; i < info->dlpi_phnum; ++i) {
            const ElfW(Phdr)& phdr = info->dlpi_phdr[i];
            if (phdr.p_type != PT_LOAD) {
                continue;
            }
            if (phdr.p_vaddr < min_vaddr) {
                min_vaddr = static_cast<uintptr_t>(phdr.p_vaddr);
            }
            const uintptr_t seg_end = static_cast<uintptr_t>(phdr.p_vaddr + phdr.p_memsz);
            if (seg_end > max_vaddr) {
                max_vaddr = seg_end;
            }
        }

        const uintptr_t load_bias = static_cast<uintptr_t>(info->dlpi_addr);
        search->result.base_address = (min_vaddr != UINTPTR_MAX) ? (load_bias + min_vaddr) : load_bias;
        search->result.mapped_size  = (max_vaddr > min_vaddr) ? (max_vaddr - min_vaddr) : 0U;
        search->found = true;
        return 1;
    };

    ::dl_iterate_phdr(callback, &ctx);
    if (ctx.found && ctx.result.base_address != 0) {
        return ctx.result;
    }
#endif
    return std::nullopt;
}

} // namespace runtime::elf

