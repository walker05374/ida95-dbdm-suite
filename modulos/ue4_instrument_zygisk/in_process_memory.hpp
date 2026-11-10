// ============================================================================
// File          : in_process_memory.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20 (-fno-exceptions -fno-rtti)
// Purpose       : Fault-safe in-process memory read/write via process_vm_readv
//                 and process_vm_writev with AArch64 TBI/PAC address untagging
// ============================================================================
#pragma once

#include "visibility.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

#if defined(__linux__) || defined(__ANDROID__)
#include <cerrno>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace runtime {
namespace memory {

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    UnmappedOrFault,
    PartialTransfer
};

class INSTRUMENT_LOCAL InProcessMemory {
public:
    // Strip AArch64 Top-Byte Ignore (TBI / MTE) and upper pointer authentication bits,
    // preserving the 48-bit canonical user-space virtual address.
    [[nodiscard]] static constexpr uintptr_t untag_address(uintptr_t addr) noexcept {
        return addr & 0x0000FFFFFFFFFFFFULL;
    }

    [[nodiscard]] static constexpr bool is_canonical_user_va(uintptr_t raw_addr, size_t byte_len = 1U) noexcept {
        if (byte_len == 0U) {
            return false;
        }
        const uintptr_t addr = untag_address(raw_addr);
        if (addr < 0x00010000ULL || addr > 0x00007FFFFFFFFFFFULL) {
            return false;
        }
        const uintptr_t end_addr = addr + (byte_len - 1U);
        if (end_addr < addr || end_addr > 0x00007FFFFFFFFFFFULL) {
            return false;
        }
        return true;
    }

    [[nodiscard]] static Status safe_read(uintptr_t source_addr, std::span<uint8_t> dest) noexcept {
        if (dest.empty()) {
            return Status::InvalidArgument;
        }
        const uintptr_t clean_addr = untag_address(source_addr);
        if (!is_canonical_user_va(clean_addr, dest.size())) {
            return Status::UnmappedOrFault;
        }

#if defined(__linux__) || defined(__ANDROID__)
        struct iovec local_iov {
            .iov_base = dest.data(),
            .iov_len  = dest.size()
        };
        struct iovec remote_iov {
            .iov_base = reinterpret_cast<void*>(clean_addr),
            .iov_len  = dest.size()
        };
        const ssize_t transferred = ::syscall(
            SYS_process_vm_readv,
            ::getpid(),
            &local_iov, 1UL,
            &remote_iov, 1UL,
            0UL
        );
        if (transferred < 0) {
            const int err = errno;
            if (err == ENOSYS || err == EPERM || err == EACCES) {
                const uintptr_t page_start = clean_addr & ~static_cast<uintptr_t>(0xFFFULL);
                const uintptr_t page_end = (clean_addr + dest.size() + 0xFFFULL) & ~static_cast<uintptr_t>(0xFFFULL);
                if (::msync(reinterpret_cast<void*>(page_start), page_end - page_start, MS_ASYNC) == 0) {
                    std::memcpy(dest.data(), reinterpret_cast<const void*>(clean_addr), dest.size());
                    return Status::Ok;
                }
            }
            return Status::UnmappedOrFault;
        }
        if (static_cast<size_t>(transferred) != dest.size()) {
            return Status::PartialTransfer;
        }
        return Status::Ok;
#else
        std::memcpy(dest.data(), reinterpret_cast<const void*>(clean_addr), dest.size());
        return Status::Ok;
#endif
    }

    [[nodiscard]] static Status safe_write(uintptr_t target_addr, std::span<const uint8_t> src) noexcept {
        if (src.empty()) {
            return Status::InvalidArgument;
        }
        const uintptr_t clean_addr = untag_address(target_addr);
        if (!is_canonical_user_va(clean_addr, src.size())) {
            return Status::UnmappedOrFault;
        }

#if defined(__linux__) || defined(__ANDROID__)
        struct iovec local_iov {
            .iov_base = const_cast<uint8_t*>(src.data()),
            .iov_len  = src.size()
        };
        struct iovec remote_iov {
            .iov_base = reinterpret_cast<void*>(clean_addr),
            .iov_len  = src.size()
        };
        const ssize_t written = ::syscall(
            SYS_process_vm_writev,
            ::getpid(),
            &local_iov, 1UL,
            &remote_iov, 1UL,
            0UL
        );
        if (written < 0) {
            const int err = errno;
            if (err == ENOSYS || err == EPERM || err == EACCES) {
                const uintptr_t page_start = clean_addr & ~static_cast<uintptr_t>(0xFFFULL);
                const uintptr_t page_end = (clean_addr + src.size() + 0xFFFULL) & ~static_cast<uintptr_t>(0xFFFULL);
                if (::msync(reinterpret_cast<void*>(page_start), page_end - page_start, MS_ASYNC) == 0) {
                    std::memcpy(reinterpret_cast<void*>(clean_addr), src.data(), src.size());
                    return Status::Ok;
                }
            }
            return Status::UnmappedOrFault;
        }
        if (static_cast<size_t>(written) != src.size()) {
            return Status::PartialTransfer;
        }
        return Status::Ok;
#else
        std::memcpy(reinterpret_cast<void*>(clean_addr), src.data(), src.size());
        return Status::Ok;
#endif
    }
};

} // namespace memory

namespace mem {

template <typename T>
[[nodiscard]] INSTRUMENT_LOCAL inline std::optional<T> read(uintptr_t address) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "mem::read<T> requires a trivially copyable type");
    T value{};
    std::span<uint8_t> dst_bytes{reinterpret_cast<uint8_t*>(&value), sizeof(T)};
    if (memory::InProcessMemory::safe_read(address, dst_bytes) != memory::Status::Ok) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
[[nodiscard]] INSTRUMENT_LOCAL inline memory::Status write_data(uintptr_t address, const T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "mem::write_data<T> requires a trivially copyable type");
    std::span<const uint8_t> src_bytes{reinterpret_cast<const uint8_t*>(&value), sizeof(T)};
    return memory::InProcessMemory::safe_write(address, src_bytes);
}

} // namespace mem
} // namespace runtime

