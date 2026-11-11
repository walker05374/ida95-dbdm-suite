// ============================================================================
// File          : aob_scanner.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20 (-std=c++20 -O3 -fno-exceptions -fno-rtti)
// Purpose       : Zero-allocation constexpr AOB pattern compiler and SIMD-backed
//                 (std::memchr) pattern scanner for .text / .rodata sections
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace ue4::introspection {

struct PatternByte {
    std::uint8_t value{0};
    bool is_wildcard{false};
};

template <std::size_t MaxCapacity = 128>
struct CompiledPattern {
    std::array<PatternByte, MaxCapacity> bytes{};
    std::size_t length{0};
    std::size_t first_concrete_idx{0};
    bool has_concrete_byte{false};

    [[nodiscard]] static constexpr std::optional<CompiledPattern> compile(std::string_view signature) noexcept {
        CompiledPattern out{};
        std::size_t i = 0;

        auto hex_to_nibble = [](char c) constexpr noexcept -> std::optional<std::uint8_t> {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
            return std::nullopt;
        };

        while (i < signature.size()) {
            while (i < signature.size() && signature[i] == ' ') {
                ++i;
            }
            if (i >= signature.size()) break;
            if (out.length >= MaxCapacity) return std::nullopt;

            if (signature[i] == '?') {
                out.bytes[out.length++] = PatternByte{0, true};
                ++i;
                if (i < signature.size() && signature[i] == '?') {
                    ++i;
                }
            } else {
                if (i + 1 >= signature.size()) return std::nullopt;
                const auto hi = hex_to_nibble(signature[i]);
                const auto lo = hex_to_nibble(signature[i + 1]);
                if (!hi || !lo) return std::nullopt;

                if (!out.has_concrete_byte) {
                    out.first_concrete_idx = out.length;
                    out.has_concrete_byte = true;
                }

                out.bytes[out.length++] = PatternByte{
                    static_cast<std::uint8_t>((*hi << 4) | *lo),
                    false
                };
                i += 2;
            }
        }

        if (out.length == 0) return std::nullopt;
        return out;
    }
};

template <std::size_t MaxCapacity = 128>
[[nodiscard]] inline const std::uint8_t* find_pattern(
    std::span<const std::uint8_t> region,
    const CompiledPattern<MaxCapacity>& pattern
) noexcept {
    if (pattern.length == 0 || region.size() < pattern.length) [[unlikely]] {
        return nullptr;
    }

    if (!pattern.has_concrete_byte) [[unlikely]] {
        return region.data();
    }

    const std::size_t anchor_offset = pattern.first_concrete_idx;
    const std::uint8_t anchor_byte  = pattern.bytes[anchor_offset].value;
    const std::size_t max_start_pos = region.size() - pattern.length;

    std::size_t cursor = 0;

    while (cursor <= max_start_pos) {
        const std::size_t remaining_search_len = (max_start_pos - cursor) + 1;

        const void* hit = std::memchr(
            region.data() + cursor + anchor_offset,
            anchor_byte,
            remaining_search_len
        );

        if (!hit) {
            return nullptr;
        }

        const auto* candidate_base = static_cast<const std::uint8_t*>(hit) - anchor_offset;
        bool full_match = true;

        for (std::size_t i = anchor_offset + 1; i < pattern.length; ++i) {
            if (!pattern.bytes[i].is_wildcard && candidate_base[i] != pattern.bytes[i].value) {
                full_match = false;
                break;
            }
        }

        if (full_match) [[likely]] {
            return candidate_base;
        }

        cursor = static_cast<std::size_t>(candidate_base - region.data()) + 1;
    }

    return nullptr;
}

} // namespace ue4::introspection

