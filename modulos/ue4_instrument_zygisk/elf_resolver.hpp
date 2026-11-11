// ============================================================================
// File          : elf_resolver.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20 (-fno-exceptions -fno-rtti)
// Purpose       : Lock-safe ELF64 module base & PT_LOAD span discovery via
//                 dl_iterate_phdr without parsing /proc/self/maps text
// ============================================================================
#pragma once

#include "in_process_memory.hpp"
#include "visibility.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    const pid_t remote_pid = memory::InProcessMemory::s_target_pid.load(std::memory_order_acquire);
    if (remote_pid > 0 && remote_pid != ::getpid()) {
        char maps_path[64]{};
        std::snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", static_cast<int>(remote_pid));
        std::FILE* fp = std::fopen(maps_path, "r");
        if (!fp) {
            return std::nullopt;
        }
        char line[512]{};
        char target_buf[128]{};
        const size_t tlen = (target_soname.size() < sizeof(target_buf) - 1) ? target_soname.size() : (sizeof(target_buf) - 1);
        std::memcpy(target_buf, target_soname.data(), tlen);
        target_buf[tlen] = '\0';

        while (std::fgets(line, sizeof(line), fp)) {
            const bool direct_match = (std::strstr(line, target_buf) != nullptr);
            const bool split_apk_match = (target_soname == "libUE4.so") &&
                                         (std::strstr(line, "com.netease.dbdena") != nullptr) &&
                                         (std::strstr(line, ".apk") != nullptr);
            if (!direct_match && !split_apk_match) {
                continue;
            }
            unsigned long long start_v = 0;
            unsigned long long end_v   = 0;
            char perms[8]{};
            if (std::sscanf(line, "%llx-%llx %7s", &start_v, &end_v, perms) != 3 ||
                start_v == 0 || end_v <= start_v || perms[0] != 'r') {
                continue;
            }
            // For split_config.arm64_v8a.apk, libUE4.so's first PT_LOAD segment is > 32 MB
            if (split_apk_match && (end_v - start_v) < 0x02000000ULL) {
                continue;
            }

            Elf64_Ehdr ehdr{};
            if (memory::InProcessMemory::safe_read(
                    static_cast<uintptr_t>(start_v),
                    std::span<uint8_t>{reinterpret_cast<uint8_t*>(&ehdr), sizeof(ehdr)}) != memory::Status::Ok) {
                continue;
            }
            if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
                ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F' ||
                ehdr.e_phoff == 0 || ehdr.e_phnum == 0 || ehdr.e_phnum > 64) {
                continue;
            }

            uintptr_t min_vaddr = UINTPTR_MAX;
            uintptr_t max_vaddr = 0;
            for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
                Elf64_Phdr phdr{};
                const uintptr_t phdr_addr = static_cast<uintptr_t>(start_v) + ehdr.e_phoff +
                                            static_cast<uintptr_t>(i) * sizeof(Elf64_Phdr);
                if (memory::InProcessMemory::safe_read(
                        phdr_addr,
                        std::span<uint8_t>{reinterpret_cast<uint8_t*>(&phdr), sizeof(phdr)}) != memory::Status::Ok) {
                    break;
                }
                if (phdr.p_type != 1 /* PT_LOAD */) {
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

            if (min_vaddr < max_vaddr) {
                const size_t span_size = max_vaddr - min_vaddr;
                if (split_apk_match && span_size < 0x08000000ULL) {
                    continue;
                }
                std::fclose(fp);
                return ModuleSpan{
                    .base_address = static_cast<uintptr_t>(start_v) - min_vaddr,
                    .mapped_size  = span_size
                };
            }
        }
        std::fclose(fp);
        return std::nullopt;
    }

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

