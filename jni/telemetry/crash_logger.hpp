// File: crash_logger.hpp
// ============================================================================
// Architecture : ELF64 AArch64 (arm64-v8a)
// Standard     : C++20 (-fno-exceptions -fno-rtti -fvisibility=hidden)
// Purpose      : Append-only, fsync-per-line crash & action logger with:
//                - Dedicated ring-buffer writer thread (zero match-timing impact)
//                - Async-signal-safe SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE handlers
//                - UE4.27 Crash/Ensure/Assert/Logf inline hooks
//                - Process logcat mirror thread
//                - Mod action correlation (toggle, offset read, hook, patch)
// ============================================================================
#pragma once

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crashlog {

inline constexpr std::size_t kRingCapacity = 1024U;
inline constexpr std::size_t kMaxLineLen   = 512U;
inline constexpr std::size_t kMaxPathLen   = 256U;

struct LogSlot {
    std::uint16_t len{0};
    char          text[kMaxLineLen]{};
};

struct LastActionSnapshot {
    std::atomic<std::uint64_t> seq{0};
    std::uint64_t              ts_ms{0};
    int                        tid{0};
    std::uintptr_t             offset{0};
    char                       tag[24]{};
    char                       func[48]{};
    char                       detail[128]{};
};

struct Ue4HookDescriptor {
    const char*    name;
    std::uintptr_t rva;
    std::uint32_t  orig_insns[4];
    bool           installed;
};

namespace detail {

inline int current_tid() noexcept {
#if defined(__ANDROID__) || defined(__linux__)
    return static_cast<int>(::syscall(SYS_gettid));
#else
    return static_cast<int>(::getpid());
#endif
}

inline std::uint64_t realtime_ms() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec / 1000000L);
}

inline void format_timestamp(char* dst, std::size_t cap) noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_now{};
    ::localtime_r(&ts.tv_sec, &tm_now);
    const int ms = static_cast<int>(ts.tv_nsec / 1000000L);
    std::snprintf(
        dst,
        cap,
        "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm_now.tm_year + 1900,
        tm_now.tm_mon + 1,
        tm_now.tm_mday,
        tm_now.tm_hour,
        tm_now.tm_min,
        tm_now.tm_sec,
        ms);
}

inline void write_all_and_fsync(int fd, const char* data, std::size_t len) noexcept {
    if (fd < 0 || data == nullptr || len == 0) {
        return;
    }
    std::size_t off = 0;
    while (off < len) {
        const ssize_t w = ::write(fd, data + off, len - off);
        if (w > 0) {
            off += static_cast<std::size_t>(w);
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    (void)::fsync(fd);
}

// Async-signal-safe string/hex/dec helpers
class SignalBuf {
public:
    char*       buf;
    std::size_t cap;
    std::size_t pos{0};

    void append_cstr(const char* s) noexcept {
        if (!s) {
            s = "(null)";
        }
        while (*s != '\0' && pos + 1 < cap) {
            buf[pos++] = *s++;
        }
        buf[pos] = '\0';
    }

    void append_char(char c) noexcept {
        if (pos + 1 < cap) {
            buf[pos++] = c;
            buf[pos]   = '\0';
        }
    }

    void append_hex(std::uint64_t val) noexcept {
        append_cstr("0x");
        char tmp[16]{};
        int idx = 0;
        if (val == 0) {
            tmp[idx++] = '0';
        } else {
            while (val != 0 && idx < 16) {
                const unsigned nib = static_cast<unsigned>(val & 0xFULL);
                tmp[idx++] = static_cast<char>(nib < 10U ? ('0' + nib) : ('A' + (nib - 10U)));
                val >>= 4U;
            }
        }
        while (idx > 0) {
            append_char(tmp[--idx]);
        }
    }

    void append_dec(std::int64_t val) noexcept {
        if (val < 0) {
            append_char('-');
            val = -val;
        }
        char tmp[24]{};
        int idx = 0;
        if (val == 0) {
            tmp[idx++] = '0';
        } else {
            std::uint64_t u = static_cast<std::uint64_t>(val);
            while (u != 0 && idx < 24) {
                tmp[idx++] = static_cast<char>('0' + (u % 10ULL));
                u /= 10ULL;
            }
        }
        while (idx > 0) {
            append_char(tmp[--idx]);
        }
    }
};

inline bool is_canonical_readable_ptr(std::uintptr_t addr) noexcept {
    const std::uintptr_t clean = addr & 0x00FFFFFFFFFFFFFFULL;
    return clean >= 0x00010000ULL && clean <= 0x00007FFFFFFFFFFFULL;
}

inline void utf16_to_ascii_safe(const char16_t* src, char* dst, std::size_t dst_cap) noexcept {
    if (dst_cap == 0) {
        return;
    }
    if (!is_canonical_readable_ptr(reinterpret_cast<std::uintptr_t>(src))) {
        dst[0] = '\0';
        return;
    }
    std::size_t out = 0;
    for (std::size_t i = 0; i < 240 && out + 1 < dst_cap; ++i) {
        const char16_t ch = src[i];
        if (ch == u'\0') {
            break;
        }
        if (ch == u'\n' || ch == u'\r') {
            dst[out++] = ' ';
        } else if (ch >= 0x20 && ch <= 0x7E) {
            dst[out++] = static_cast<char>(ch);
        } else {
            dst[out++] = '?';
        }
    }
    dst[out] = '\0';
}

inline const char* signal_name(int sig) noexcept {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGBUS:  return "SIGBUS";
        case SIGILL:  return "SIGILL";
        case SIGFPE:  return "SIGFPE";
        default:      return "SIGUNK";
    }
}

inline bool ensure_dir_recursive(const char* dir_path) noexcept {
    if (!dir_path || dir_path[0] == '\0') {
        return false;
    }
    char tmp[kMaxPathLen]{};
    std::strncpy(tmp, dir_path, sizeof(tmp) - 1);
    const std::size_t len = std::strlen(tmp);
    for (std::size_t i = 1; i <= len; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            const char saved = tmp[i];
            tmp[i] = '\0';
            (void)::mkdir(tmp, 0777);
            (void)::chmod(tmp, 0777);
            tmp[i] = saved;
        }
    }
    struct stat st{};
    return ::stat(dir_path, &st) == 0 && S_ISDIR(st.st_mode);
}

} // namespace detail

class CrashLogger {
public:
    static CrashLogger& instance() noexcept {
        static CrashLogger s_inst;
        return s_inst;
    }

    void set_module_bases(std::uintptr_t ue4_base, std::uintptr_t core_base) noexcept {
        ue4_base_.store(ue4_base, std::memory_order_release);
        core_base_.store(core_base, std::memory_order_release);
    }

    [[nodiscard]] bool is_active() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    [[nodiscard]] const char* active_path() const noexcept {
        return log_path_;
    }

    // Starts logging into a newly created timestamped file inside preferred_dir (falling back to fallback_dir).
    // Always opens with O_APPEND (never truncates).
    bool start_new_session(const char* preferred_dir, const char* fallback_dir, char* out_path, std::size_t out_cap) noexcept {
        if (active_.load(std::memory_order_acquire)) {
            if (out_path && out_cap > 0) {
                std::strncpy(out_path, log_path_, out_cap - 1);
                out_path[out_cap - 1] = '\0';
            }
            return true;
        }

        struct timespec ts{};
        ::clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm_now{};
        ::localtime_r(&ts.tv_sec, &tm_now);

        char fname[96]{};
        std::snprintf(
            fname,
            sizeof(fname),
            "dbdm_crash_%04d%02d%02d_%02d%02d%02d.log",
            tm_now.tm_year + 1900,
            tm_now.tm_mon + 1,
            tm_now.tm_mday,
            tm_now.tm_hour,
            tm_now.tm_min,
            tm_now.tm_sec);

        char chosen_path[kMaxPathLen]{};
        int  fd = -1;

        if (preferred_dir && preferred_dir[0] != '\0' && detail::ensure_dir_recursive(preferred_dir)) {
            std::snprintf(chosen_path, sizeof(chosen_path), "%s/%s", preferred_dir, fname);
            fd = ::open(chosen_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
        }
        if (fd < 0 && fallback_dir && fallback_dir[0] != '\0' && detail::ensure_dir_recursive(fallback_dir)) {
            std::snprintf(chosen_path, sizeof(chosen_path), "%s/%s", fallback_dir, fname);
            fd = ::open(chosen_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
        }
        if (fd < 0) {
            return false;
        }
        (void)::fchmod(fd, 0666);
        (void)::chmod(chosen_path, 0666);

        const bool ok = attach_existing_file(chosen_path, fd, true);
        if (ok && out_path && out_cap > 0) {
            std::strncpy(out_path, log_path_, out_cap - 1);
            out_path[out_cap - 1] = '\0';
        }
        return ok;
    }

    // Attaches to an existing session file path (used by Zygisk in-process / daemon when APK initiates logging).
    bool attach_session_path(const char* path) noexcept {
        if (!path || path[0] == '\0') {
            return false;
        }
        if (active_.load(std::memory_order_acquire) && std::strcmp(log_path_, path) == 0) {
            return true;
        }
        if (active_.load(std::memory_order_acquire)) {
            stop_session();
        }
        const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
        if (fd < 0) {
            return false;
        }
        (void)::fchmod(fd, 0666);
        return attach_existing_file(path, fd, false);
    }

    void stop_session() noexcept {
        if (!active_.load(std::memory_order_acquire)) {
            return;
        }
        log_action("LOG", "stop_session", 0, "Parar logs solicitado (arquivo preservado append-only)");
        if (!active_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        pthread_mutex_lock(&ring_mutex_);
        pthread_cond_broadcast(&ring_cond_);
        pthread_mutex_unlock(&ring_mutex_);

        if (writer_started_.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(writer_thread_, nullptr);
        }
        if (logcat_started_.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(logcat_thread_, nullptr);
        }

        const int fd = log_fd_.exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) {
            (void)::fsync(fd);
            ::close(fd);
        }
    }

    void log_action(const char* tag, const char* func, std::uintptr_t offset, const char* detail_msg) noexcept {
        const std::uint64_t s = last_action_.seq.load(std::memory_order_relaxed);
        last_action_.seq.store(s + 1ULL, std::memory_order_release); // odd = writing
        last_action_.ts_ms  = detail::realtime_ms();
        last_action_.tid    = detail::current_tid();
        last_action_.offset = offset;
        std::strncpy(last_action_.tag, tag ? tag : "ACT", sizeof(last_action_.tag) - 1);
        last_action_.tag[sizeof(last_action_.tag) - 1] = '\0';
        std::strncpy(last_action_.func, func ? func : "-", sizeof(last_action_.func) - 1);
        last_action_.func[sizeof(last_action_.func) - 1] = '\0';
        std::strncpy(last_action_.detail, detail_msg ? detail_msg : "-", sizeof(last_action_.detail) - 1);
        last_action_.detail[sizeof(last_action_.detail) - 1] = '\0';
        last_action_.seq.store(s + 2ULL, std::memory_order_release); // even = ready

        if (!active_.load(std::memory_order_acquire)) {
            return;
        }

        char ts[32]{};
        detail::format_timestamp(ts, sizeof(ts));
        char line[kMaxLineLen]{};
        const int n = std::snprintf(
            line,
            sizeof(line),
            "[%s][tid=%d][%s] func=%s offset=0x%llX patch=%s\n",
            ts,
            last_action_.tid,
            last_action_.tag,
            last_action_.func,
            static_cast<unsigned long long>(offset),
            last_action_.detail);
        if (n > 0) {
            push_line(line, static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sizeof(line) - 1))));
        }
    }

    void log_raw_line(const char* tag, const char* msg) noexcept {
        if (!active_.load(std::memory_order_acquire) || !msg) {
            return;
        }
        char ts[32]{};
        detail::format_timestamp(ts, sizeof(ts));
        char line[kMaxLineLen]{};
        const int n = std::snprintf(
            line,
            sizeof(line),
            "[%s][tid=%d][%s] %s\n",
            ts,
            detail::current_tid(),
            tag ? tag : "INFO",
            msg);
        if (n > 0) {
            push_line(line, static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sizeof(line) - 1))));
        }
    }

    // Installs SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE handlers with sigaltstack
    void install_signal_handlers() noexcept {
        bool expected = false;
        if (!signals_installed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        static char s_altstack[65536]{};
        stack_t ss{};
        ss.ss_sp    = s_altstack;
        ss.ss_size  = sizeof(s_altstack);
        ss.ss_flags = 0;
        (void)::sigaltstack(&ss, nullptr);

        struct sigaction sa{};
        sa.sa_sigaction = &CrashLogger::signal_handler_entry;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;

        constexpr int kSigs[5] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
        for (int i = 0; i < 5; ++i) {
            (void)::sigaction(kSigs[i], &sa, &prev_actions_[i]);
        }
        log_action("HOOK-INSTALL", "install_signal_handlers", 0, "SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE SA_SIGINFO|SA_ONSTACK");
    }

    // Installs inline AArch64 hooks on UE4.27 Crash/Ensure/Assert/Logf functions in libUE4.so
    void install_ue4_crash_hooks(std::uintptr_t ue4_base) noexcept {
        if (ue4_base == 0) {
            return;
        }
        set_module_bases(ue4_base, core_base_.load(std::memory_order_acquire));

        bool expected = false;
        if (!ue4_hooks_installed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        // Allocate 1 RWX page for 6 hook pre-stubs + 6 original-prologue trampolines
        const std::size_t page_sz = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
        void* stub_mem = ::mmap(
            nullptr,
            page_sz,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        if (stub_mem == MAP_FAILED || stub_mem == nullptr) {
            ue4_hooks_installed_.store(false, std::memory_order_release);
            return;
        }
        stub_page_ = static_cast<std::uint8_t*>(stub_mem);

        static constexpr std::array<Ue4HookDescriptor, 6> kHooks{{
            {"StaticFailDebug",            0x0A50BD8CU, {0xA9BB67FCU, 0xA9015FF8U, 0xA90257F6U, 0xA9034FF4U}, false},
            {"LogAssertFailedMessageImpl", 0x0A50C064U, {0xA9BA6FFCU, 0xA90167FAU, 0xA9025FF8U, 0xA90357F6U}, false},
            {"AssertFailedV",              0x0A50C3D8U, {0xF81C0FFCU, 0xA90157F6U, 0xA9024FF4U, 0xA9037BFDU}, false},
            {"LowLevelFatalError",         0x0A50C544U, {0xA9BD57FCU, 0xA9014FF4U, 0xA9027BFDU, 0x910083FDU}, false},
            {"EnsureFailed",               0x0A50C634U, {0xD10583FFU, 0xF90093FCU, 0xA91357F6U, 0xA9144FF4U}, false},
            {"FMsg::Logf_InternalImpl",    0x0A4E3C04U, {0xA9BA6FFCU, 0xA90167FAU, 0xA9025FF8U, 0xA90357F6U}, false},
        }};

        for (std::size_t i = 0; i < kHooks.size(); ++i) {
            const auto& hk = kHooks[i];
            const std::uintptr_t target = ue4_base + hk.rva;

            // Verify first 4 instructions match Build 292949 before patching
            std::uint32_t current[4]{};
            std::memcpy(current, reinterpret_cast<const void*>(target), sizeof(current));
            bool match = true;
            for (int k = 0; k < 4; ++k) {
                if (current[k] != hk.orig_insns[k]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }

            // Layout per hook slot (256 bytes):
            //   +0x000: orig_tramp (32 bytes): 4 orig insns + LDR X16, #8; BR X16; .quad (target+16)
            //   +0x020: pre_stub (160 bytes): save X0..X7, Q0..Q7, X29, X30 -> call on_ue4_hook -> restore -> B orig_tramp
            std::uint8_t* slot_ptr   = stub_page_ + (i * 256U);
            std::uint8_t* orig_tramp = slot_ptr;
            std::uint8_t* pre_stub   = slot_ptr + 32U;

            std::uint32_t* ot = reinterpret_cast<std::uint32_t*>(orig_tramp);
            ot[0] = hk.orig_insns[0];
            ot[1] = hk.orig_insns[1];
            ot[2] = hk.orig_insns[2];
            ot[3] = hk.orig_insns[3];
            ot[4] = 0x58000050U; // LDR X16, #8 (PC+8 = ot+6)
            ot[5] = 0xD61F0200U; // BR  X16
            const std::uint64_t resume_addr = static_cast<std::uint64_t>(target + 16U);
            std::memcpy(&ot[6], &resume_addr, sizeof(resume_addr));

            std::uint32_t* ps = reinterpret_cast<std::uint32_t*>(pre_stub);
            int p = 0;
            ps[p++] = 0xD10383FFU; // SUB SP, SP, #224
            ps[p++] = 0xA9007BFDU; // STP X29, X30, [SP, #0]
            ps[p++] = 0xA90107E0U; // STP X0, X1, [SP, #16]
            ps[p++] = 0xA9020FE2U; // STP X2, X3, [SP, #32]
            ps[p++] = 0xA90317E4U; // STP X4, X5, [SP, #48]
            ps[p++] = 0xA9041FE6U; // STP X6, X7, [SP, #64]
            ps[p++] = 0xAD0287E0U; // STP Q0, Q1, [SP, #80]
            ps[p++] = 0xAD038FE2U; // STP Q2, Q3, [SP, #112]
            ps[p++] = 0xAD0497E4U; // STP Q4, Q5, [SP, #144]
            ps[p++] = 0xAD059FE6U; // STP Q6, Q7, [SP, #176]
            ps[p++] = 0xAA0403E5U; // MOV X5, X4
            ps[p++] = 0xAA0303E4U; // MOV X4, X3
            ps[p++] = 0xAA0203E3U; // MOV X3, X2
            ps[p++] = 0xAA0103E2U; // MOV X2, X1
            ps[p++] = 0xAA0003E1U; // MOV X1, X0
            ps[p++] = 0xD2800000U | (static_cast<std::uint32_t>(i & 0xFFFFU) << 5U); // MOVZ X0, #i
            const int ldr_idx = p;
            ps[p++] = 0x58000010U; // LDR X16, #... (patched below)
            ps[p++] = 0xD63F0200U; // BLR X16
            ps[p++] = 0xAD459FE6U; // LDP Q6, Q7, [SP, #176]
            ps[p++] = 0xAD4497E4U; // LDP Q4, Q5, [SP, #144]
            ps[p++] = 0xAD438FE2U; // LDP Q2, Q3, [SP, #112]
            ps[p++] = 0xAD4287E0U; // LDP Q0, Q1, [SP, #80]
            ps[p++] = 0xA9441FE6U; // LDP X6, X7, [SP, #64]
            ps[p++] = 0xA94317E4U; // LDP X4, X5, [SP, #48]
            ps[p++] = 0xA9420FE2U; // LDP X2, X3, [SP, #32]
            ps[p++] = 0xA94107E0U; // LDP X0, X1, [SP, #16]
            ps[p++] = 0xA9407BFDU; // LDP X29, X30, [SP, #0]
            ps[p++] = 0x910383FFU; // ADD SP, SP, #224
            // B orig_tramp (offset in bytes = -(32 + p*4))
            const std::int32_t b_off_insns = -static_cast<std::int32_t>((32 + p * 4) / 4);
            ps[p++] = 0x14000000U | (static_cast<std::uint32_t>(b_off_insns) & 0x03FFFFFFU);
            if ((p & 1) != 0) {
                ps[p++] = 0xD503201FU; // NOP for 8-byte alignment
            }
            const int lit_idx = p;
            ps[ldr_idx] = 0x58000010U | (static_cast<std::uint32_t>((lit_idx - ldr_idx) & 0x7FFFF) << 5U);
            const std::uint64_t handler_addr = reinterpret_cast<std::uint64_t>(&CrashLogger::on_ue4_hook);
            std::memcpy(&ps[lit_idx], &handler_addr, sizeof(handler_addr));

            __builtin___clear_cache(reinterpret_cast<char*>(slot_ptr), reinterpret_cast<char*>(slot_ptr + 256U));

            // Patch target prologue (16 bytes) -> LDR X16, #8; BR X16; .quad pre_stub
            const std::uintptr_t page_start = target & ~(page_sz - 1ULL);
            const std::uintptr_t page_end   = (target + 16ULL + page_sz - 1ULL) & ~(page_sz - 1ULL);
            if (::mprotect(reinterpret_cast<void*>(page_start), page_end - page_start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
                std::uint32_t detour[4]{};
                detour[0] = 0x58000050U; // LDR X16, #8
                detour[1] = 0xD61F0200U; // BR  X16
                const std::uint64_t stub_addr = reinterpret_cast<std::uint64_t>(pre_stub);
                std::memcpy(&detour[2], &stub_addr, sizeof(stub_addr));
                std::memcpy(reinterpret_cast<void*>(target), detour, sizeof(detour));
                __builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target + 16U));
                (void)::mprotect(reinterpret_cast<void*>(page_start), page_end - page_start, PROT_READ | PROT_EXEC);
                log_action("HOOK-INSTALL", hk.name, hk.rva, "16B AArch64 inline trampoline (LDR X16+BR X16)");
            }
        }
    }

private:
    CrashLogger() = default;

    bool attach_existing_file(const char* path, int fd, bool start_logcat) noexcept {
        std::strncpy(log_path_, path, sizeof(log_path_) - 1);
        log_path_[sizeof(log_path_) - 1] = '\0';
        log_fd_.store(fd, std::memory_order_release);

        ring_head_  = 0;
        ring_tail_  = 0;
        ring_count_ = 0;

        active_.store(true, std::memory_order_release);

        if (!writer_started_.exchange(true, std::memory_order_acq_rel)) {
            if (pthread_create(&writer_thread_, nullptr, &CrashLogger::writer_thread_entry, this) != 0) {
                writer_started_.store(false, std::memory_order_release);
            }
        }

        if (start_logcat && !logcat_started_.exchange(true, std::memory_order_acq_rel)) {
            if (pthread_create(&logcat_thread_, nullptr, &CrashLogger::logcat_thread_entry, this) != 0) {
                logcat_started_.store(false, std::memory_order_release);
            }
        }

        char header[320]{};
        char ts[32]{};
        detail::format_timestamp(ts, sizeof(ts));
        const int n = std::snprintf(
            header,
            sizeof(header),
            "\n[%s][pid=%d][tid=%d][SESSION-START] Append-only crash log active at: %s\n",
            ts,
            static_cast<int>(::getpid()),
            detail::current_tid(),
            log_path_);
        if (n > 0) {
            detail::write_all_and_fsync(fd, header, static_cast<std::size_t>(n));
        }
        return true;
    }

    void push_line(const char* line, std::size_t len) noexcept {
        if (!line || len == 0) {
            return;
        }
        pthread_mutex_lock(&ring_mutex_);
        if (ring_count_ == kRingCapacity) {
            // Drop oldest unread slot to keep ring moving without blocking game threads
            ring_tail_ = (ring_tail_ + 1U) % kRingCapacity;
            --ring_count_;
        }
        LogSlot& slot = ring_[ring_head_];
        const std::size_t copy_len = std::min<std::size_t>(len, kMaxLineLen - 1U);
        std::memcpy(slot.text, line, copy_len);
        slot.text[copy_len] = '\0';
        slot.len = static_cast<std::uint16_t>(copy_len);
        ring_head_ = (ring_head_ + 1U) % kRingCapacity;
        ++ring_count_;
        pthread_cond_signal(&ring_cond_);
        pthread_mutex_unlock(&ring_mutex_);
    }

    static void* writer_thread_entry(void* arg) noexcept {
        pthread_setname_np(pthread_self(), "dbdm_crash_log");
        auto* self = static_cast<CrashLogger*>(arg);
        self->writer_loop();
        return nullptr;
    }

    void writer_loop() noexcept {
        LogSlot local_slot{};
        while (true) {
            pthread_mutex_lock(&ring_mutex_);
            while (ring_count_ == 0U && active_.load(std::memory_order_acquire)) {
                pthread_cond_wait(&ring_cond_, &ring_mutex_);
            }
            if (ring_count_ == 0U && !active_.load(std::memory_order_acquire)) {
                pthread_mutex_unlock(&ring_mutex_);
                break;
            }
            local_slot = ring_[ring_tail_];
            ring_tail_ = (ring_tail_ + 1U) % kRingCapacity;
            --ring_count_;
            pthread_mutex_unlock(&ring_mutex_);

            const int fd = log_fd_.load(std::memory_order_acquire);
            if (fd >= 0 && local_slot.len > 0U) {
                detail::write_all_and_fsync(fd, local_slot.text, local_slot.len);
            }
        }
    }

    static void* logcat_thread_entry(void* arg) noexcept {
        pthread_setname_np(pthread_self(), "dbdm_logcat_mir");
        auto* self = static_cast<CrashLogger*>(arg);
        self->logcat_loop();
        return nullptr;
    }

    void logcat_loop() noexcept {
        char cmd[128]{};
        std::snprintf(
            cmd,
            sizeof(cmd),
            "logcat -v threadtime -T 1 --pid=%d UE4Diag:V FloatingModService:V DEBUG:V libc:V *:S 2>/dev/null",
            static_cast<int>(::getpid()));
        std::FILE* pipe = ::popen(cmd, "r");
        if (!pipe) {
            return;
        }
        const int pfd_num = ::fileno(pipe);
        char line[384]{};
        while (active_.load(std::memory_order_acquire)) {
            if (pfd_num >= 0) {
                struct pollfd pfd{};
                pfd.fd     = pfd_num;
                pfd.events = POLLIN;
                const int prc = ::poll(&pfd, 1, 150);
                if (prc == 0) {
                    continue; // Timeout: recheck active_ flag
                }
                if (prc < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    break;
                }
                if ((pfd.revents & POLLIN) == 0) {
                    break;
                }
            }
            if (std::fgets(line, sizeof(line), pipe) == nullptr) {
                break;
            }
            const std::size_t len = std::strlen(line);
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
            if (line[0] != '\0') {
                log_raw_line("LOGCAT", line);
            }
        }
        ::pclose(pipe);
    }

    static void on_ue4_hook(
        std::uint64_t hook_id,
        std::uint64_t x0,
        std::uint64_t x1,
        std::uint64_t x2,
        std::uint64_t x3,
        std::uint64_t x4) noexcept {

        auto& self = instance();
        const int fd = self.log_fd_.load(std::memory_order_acquire);
        if (fd < 0) {
            return;
        }

        char msg1[240]{};
        char msg2[240]{};
        const char* file = "(unknown)";
        std::int64_t line = 0;
        const char* hook_name = "UE4_HOOK";

        switch (hook_id) {
            case 0: // StaticFailDebug(Error_u16, File, Line, Desc_u16, bIsEnsure, ...)
                hook_name = "StaticFailDebug";
                detail::utf16_to_ascii_safe(reinterpret_cast<const char16_t*>(x0), msg1, sizeof(msg1));
                file = detail::is_canonical_readable_ptr(x1) ? reinterpret_cast<const char*>(x1) : "(null)";
                line = static_cast<std::int32_t>(x2);
                detail::utf16_to_ascii_safe(reinterpret_cast<const char16_t*>(x3), msg2, sizeof(msg2));
                break;
            case 1: // FDebug::LogAssertFailedMessageImpl(Expr, File, Line, PC, Fmt_u16, ...)
                hook_name = "LogAssertFailedMessageImpl";
                if (detail::is_canonical_readable_ptr(x0)) {
                    std::strncpy(msg1, reinterpret_cast<const char*>(x0), sizeof(msg1) - 1);
                }
                file = detail::is_canonical_readable_ptr(x1) ? reinterpret_cast<const char*>(x1) : "(null)";
                line = static_cast<std::int32_t>(x2);
                detail::utf16_to_ascii_safe(reinterpret_cast<const char16_t*>(x4), msg2, sizeof(msg2));
                break;
            case 2: // FDebug::AssertFailedV(Expr, File, Line, Fmt_u16, va_list)
                hook_name = "AssertFailedV";
                if (detail::is_canonical_readable_ptr(x0)) {
                    std::strncpy(msg1, reinterpret_cast<const char*>(x0), sizeof(msg1) - 1);
                }
                file = detail::is_canonical_readable_ptr(x1) ? reinterpret_cast<const char*>(x1) : "(null)";
                line = static_cast<std::int32_t>(x2);
                detail::utf16_to_ascii_safe(reinterpret_cast<const char16_t*>(x3), msg2, sizeof(msg2));
                break;
            case 3: // FDebug::LowLevelFatalError(File, Line, Fmt_u16, ...)
                hook_name = "LowLevelFatalError";
                file = detail::is_canonical_readable_ptr(x0) ? reinterpret_cast<const char*>(x0) : "(null)";
                line = static_cast<std::int32_t>(x1);
                detail::utf16_to_ascii_safe(reinterpret_cast<const char16_t*>(x2), msg1, sizeof(msg1));
                break;
            case 4: // FDebug::EnsureFailed(Expr, Line, File, ...)
                hook_name = "EnsureFailed";
                if (detail::is_canonical_readable_ptr(x0)) {
                    std::strncpy(msg1, reinterpret_cast<const char*>(x0), sizeof(msg1) - 1);
                }
                line = static_cast<std::int32_t>(x1);
                file = detail::is_canonical_readable_ptr(x2) ? reinterpret_cast<const char*>(x2) : "(null)";
                break;
            case 5: { // FMsg::Logf_InternalImpl(File, Line, Category, Verbosity, Fmt_u16, ...)
                const std::uint8_t verb = static_cast<std::uint8_t>(x3 & 0xFFU);
                if (verb > 2U) { // Only Fatal (1) & Error (2)
                    return;
                }
                hook_name = (verb == 1U) ? "FMsg::Fatal" : "FMsg::Error";
                file = detail::is_canonical_readable_ptr(x0) ? reinterpret_cast<const char*>(x0) : "(null)";
                line = static_cast<std::int32_t>(x1);
                detail::utf16_to_ascii_safe(reinterpret_cast<const char16_t*>(x4), msg1, sizeof(msg1));
                break;
            }
            default:
                return;
        }

        char out[768]{};
        detail::SignalBuf sb{out, sizeof(out), 0};
        sb.append_cstr("[UE4-FATAL] hook=");
        sb.append_cstr(hook_name);
        sb.append_cstr(" tid=");
        sb.append_dec(detail::current_tid());
        sb.append_cstr(" file=");
        sb.append_cstr(file);
        sb.append_cstr(" line=");
        sb.append_dec(line);
        sb.append_cstr(" msg=\"");
        sb.append_cstr(msg1);
        if (msg2[0] != '\0') {
            sb.append_cstr(" | ");
            sb.append_cstr(msg2);
        }
        sb.append_cstr("\"\n");
        detail::write_all_and_fsync(fd, sb.buf, sb.pos);
    }

    struct UnwindState {
        detail::SignalBuf* sb;
        std::uintptr_t     ue4_base;
        std::uintptr_t     core_base;
        int                depth;
    };

    static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) noexcept {
        auto* st = static_cast<UnwindState*>(arg);
        if (st->depth >= 16) {
            return _URC_END_OF_STACK;
        }
        const std::uintptr_t pc = static_cast<std::uintptr_t>(_Unwind_GetIP(ctx));
        if (pc == 0) {
            return _URC_END_OF_STACK;
        }
        st->sb->append_cstr(" #");
        st->sb->append_dec(st->depth++);
        st->sb->append_cstr(" ");
        st->sb->append_hex(pc);
        if (st->ue4_base != 0 && pc >= st->ue4_base && (pc - st->ue4_base) < 0x15000000ULL) {
            st->sb->append_cstr("(libUE4+");
            st->sb->append_hex(pc - st->ue4_base);
            st->sb->append_cstr(")");
        } else if (st->core_base != 0 && pc >= st->core_base && (pc - st->core_base) < 0x02000000ULL) {
            st->sb->append_cstr("(libcore+");
            st->sb->append_hex(pc - st->core_base);
            st->sb->append_cstr(")");
        } else {
            Dl_info info{};
            if (::dladdr(reinterpret_cast<const void*>(pc), &info) != 0 && info.dli_fname) {
                const char* slash = std::strrchr(info.dli_fname, '/');
                st->sb->append_cstr("(");
                st->sb->append_cstr(slash ? (slash + 1) : info.dli_fname);
                st->sb->append_cstr("+");
                st->sb->append_hex(pc - reinterpret_cast<std::uintptr_t>(info.dli_fbase));
                st->sb->append_cstr(")");
            }
        }
        return _URC_NO_REASON;
    }

    static void signal_handler_entry(int sig, siginfo_t* info, void* ucontext_ptr) noexcept {
        auto& self = instance();
        int fd = self.log_fd_.load(std::memory_order_acquire);
        bool opened_fallback = false;
        if (fd < 0) {
            fd = ::open("/data/local/tmp/dbdm_last_crash.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
            opened_fallback = (fd >= 0);
        }

        if (fd >= 0) {
            const auto* uc = static_cast<const ucontext_t*>(ucontext_ptr);
            const std::uintptr_t pc = uc ? static_cast<std::uintptr_t>(uc->uc_mcontext.pc) : 0ULL;
            const std::uintptr_t lr = uc ? static_cast<std::uintptr_t>(uc->uc_mcontext.regs[30]) : 0ULL;
            const std::uintptr_t sp = uc ? static_cast<std::uintptr_t>(uc->uc_mcontext.sp) : 0ULL;
            const std::uintptr_t fp = uc ? static_cast<std::uintptr_t>(uc->uc_mcontext.regs[29]) : 0ULL;
            const std::uintptr_t fault = info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0ULL;
            const std::uintptr_t ue4_b = self.ue4_base_.load(std::memory_order_acquire);
            const std::uintptr_t cor_b = self.core_base_.load(std::memory_order_acquire);

            char crash_buf[2048]{};
            detail::SignalBuf sb{crash_buf, sizeof(crash_buf), 0};
            sb.append_cstr("\n[CRASH-SIGNAL] sig=");
            sb.append_dec(sig);
            sb.append_cstr("(");
            sb.append_cstr(detail::signal_name(sig));
            sb.append_cstr(") si_code=");
            sb.append_dec(info ? info->si_code : 0);
            sb.append_cstr(" tid=");
            sb.append_dec(detail::current_tid());
            sb.append_cstr(" fault_addr=");
            sb.append_hex(fault);
            sb.append_cstr(" PC=");
            sb.append_hex(pc);
            if (ue4_b != 0 && pc >= ue4_b && (pc - ue4_b) < 0x15000000ULL) {
                sb.append_cstr("(libUE4+");
                sb.append_hex(pc - ue4_b);
                sb.append_cstr(")");
            } else if (cor_b != 0 && pc >= cor_b && (pc - cor_b) < 0x02000000ULL) {
                sb.append_cstr("(libcore+");
                sb.append_hex(pc - cor_b);
                sb.append_cstr(")");
            }
            sb.append_cstr(" LR=");
            sb.append_hex(lr);
            sb.append_cstr(" SP=");
            sb.append_hex(sp);
            sb.append_cstr(" FP=");
            sb.append_hex(fp);

            sb.append_cstr(" | Backtrace:");
            UnwindState ust{&sb, ue4_b, cor_b, 0};
            (void)_Unwind_Backtrace(&CrashLogger::unwind_cb, &ust);

            sb.append_cstr(" | LastModAction:[tid=");
            sb.append_dec(self.last_action_.tid);
            sb.append_cstr(" tag=");
            sb.append_cstr(self.last_action_.tag);
            sb.append_cstr(" func=");
            sb.append_cstr(self.last_action_.func);
            sb.append_cstr(" off=");
            sb.append_hex(self.last_action_.offset);
            sb.append_cstr(" patch=");
            sb.append_cstr(self.last_action_.detail);
            sb.append_cstr("]");

            if (ue4_b != 0) {
                const auto* gerr_hist = reinterpret_cast<const char16_t*>(ue4_b + 0x10B5CC08ULL);
                if (detail::is_canonical_readable_ptr(reinterpret_cast<std::uintptr_t>(gerr_hist)) && gerr_hist[0] != u'\0') {
                    char hist_ascii[240]{};
                    detail::utf16_to_ascii_safe(gerr_hist, hist_ascii, sizeof(hist_ascii));
                    if (hist_ascii[0] != '\0') {
                        sb.append_cstr(" | GErrorHist=\"");
                        sb.append_cstr(hist_ascii);
                        sb.append_cstr("\"");
                    }
                }
            }
            sb.append_cstr("\n");
            detail::write_all_and_fsync(fd, sb.buf, sb.pos);

            if (opened_fallback) {
                ::close(fd);
            }
        }

        // Chain to previous signal handler
        constexpr int kSigs[5] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
        for (int i = 0; i < 5; ++i) {
            if (kSigs[i] == sig) {
                const auto& prev = self.prev_actions_[i];
                if ((prev.sa_flags & SA_SIGINFO) != 0 && prev.sa_sigaction != nullptr) {
                    prev.sa_sigaction(sig, info, ucontext_ptr);
                    return;
                } else if (prev.sa_handler != nullptr && prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN) {
                    prev.sa_handler(sig);
                    return;
                }
                break;
            }
        }
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }

    std::atomic<bool>           active_{false};
    std::atomic<bool>           writer_started_{false};
    std::atomic<bool>           logcat_started_{false};
    std::atomic<bool>           signals_installed_{false};
    std::atomic<bool>           ue4_hooks_installed_{false};
    std::atomic<int>            log_fd_{-1};
    std::atomic<std::uintptr_t> ue4_base_{0};
    std::atomic<std::uintptr_t> core_base_{0};
    char                        log_path_[kMaxPathLen]{};

    pthread_t                   writer_thread_{};
    pthread_t                   logcat_thread_{};
    pthread_mutex_t             ring_mutex_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t              ring_cond_  = PTHREAD_COND_INITIALIZER;
    std::array<LogSlot, kRingCapacity> ring_{};
    std::size_t                 ring_head_{0};
    std::size_t                 ring_tail_{0};
    std::size_t                 ring_count_{0};

    LastActionSnapshot          last_action_{};
    struct sigaction            prev_actions_[5]{};
    std::uint8_t*               stub_page_{nullptr};
};

} // namespace crashlog
