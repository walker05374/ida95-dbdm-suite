// ============================================================================
// Architecture : ELF64 AArch64 (ARM64) | Unreal Engine 4.27 (libUE4.so)
// Standard     : C++20
// Compiler Flags: -std=c++20 -O3 -fno-exceptions -fno-rtti -fPIC -shared -pthread
// Module       : Step 5 — Lifecycle Entrypoint, Level-Transition Guard & Lock-Free Worker
// ============================================================================

#pragma once

#include "ue4_aarch64_unified_diagnostics.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <ctime>

#if defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

namespace ue4::aarch64::runtime {

// ============================================================================
// 1. Snapshot Lock-Free Duplo-Bufferizado (Zero-Contention Render/Game Read)
// ============================================================================

struct alignas(64) FDiagnosticFrameSnapshot {
    uint64_t                                 FrameSequence{0};
    uintptr_t                                ActiveUWorld{0};
    uintptr_t                                ActivePersistentLevel{0};
    float                                    TimestampSeconds{0.0f};
    size_t                                   EntityCount{0};
    std::array<FEntityTelemetrySample, 512>  Entities{};
    FTaskProgressTelemetryEvent              LatestTaskEvent{};
    bool                                     bHasTaskEvent{false};
};

// ============================================================================
// 2. Utilitário de Relógio Monotônico POSIX (Nanosegundos -> Segundos)
// ============================================================================

struct MonotonicClock {
    [[nodiscard]] static uint64_t NowNs() noexcept {
#if defined(__linux__) || defined(__ANDROID__)
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL) +
               static_cast<uint64_t>(ts.tv_nsec);
#else
        return 0ULL;
#endif
    }

    static void SleepMicroseconds(uint32_t usec) noexcept {
#if defined(__linux__) || defined(__ANDROID__)
        struct timespec req{};
        req.tv_sec  = static_cast<time_t>(usec / 1000000U);
        req.tv_nsec = static_cast<long>((usec % 1000000U) * 1000U);
        clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
#else
        (void)usec;
#endif
    }
};

// ============================================================================
// 3. Orquestrador de Ciclo de Vida e Pipeline de Telemetria (Step 5)
// ============================================================================

class RuntimePipelineOrchestrator {
public:
    static constexpr uint32_t kModulePollIntervalUs  = 100000U; // 100ms enquanto aguarda libUE4.so
    static constexpr uint32_t kLevelTransitionWaitUs = 50000U;  // 50ms durante telas de loading
    static constexpr uint32_t kTargetTickIntervalUs  = 16667U;  // ~60 Hz (16.67ms)

    [[nodiscard]] static RuntimePipelineOrchestrator& Instance() noexcept {
        static RuntimePipelineOrchestrator instance{};
        return instance;
    }

    // Inicia a thread dedicada de telemetria (idempotente)
    bool StartAsyncWorker() noexcept {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false; // Já em execução
        }

#if defined(__linux__) || defined(__ANDROID__)
        pthread_attr_t attr{};
        if (pthread_attr_init(&attr) != 0) {
            running_.store(false, std::memory_order_release);
            return false;
        }
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        const int rc = pthread_create(&worker_thread_, &attr, &WorkerThreadEntry, this);
        pthread_attr_destroy(&attr);

        if (rc != 0) {
            running_.store(false, std::memory_order_release);
            return false;
        }
        return true;
#else
        running_.store(false, std::memory_order_release);
        return false;
#endif
    }

    void StopWorker() noexcept {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        // Restaura baseline cinemático e nome original caso estivessem sob ajuste
        (void)kinematic_ctrl_.RestoreBaseline();
        (void)name_ctrl_.RestoreOriginalDisplayName();
    }

    // Define um override opcional para o nome de exibição local (PlayerNamePrivate) de forma thread-safe (Double-Buffered)
    void SetCustomDisplayNameOverride(std::string_view customAsciiName) noexcept {
        const uint32_t writeSlot = (active_name_slot_.load(std::memory_order_relaxed) ^ 1U) & 1U;
        FStackName& slotBuf = staged_name_bufs_[writeSlot];

        const size_t copyLen = customAsciiName.size() < FStackName::kMaxCapacity
                                   ? customAsciiName.size()
                                   : FStackName::kMaxCapacity;
        if (copyLen > 0) {
            std::memcpy(slotBuf.Buffer.data(), customAsciiName.data(), copyLen);
        }
        slotBuf.Buffer[copyLen] = '\0';
        slotBuf.Length = static_cast<uint8_t>(copyLen);

        active_name_slot_.store(writeSlot, std::memory_order_release);
        name_override_dirty_.store(copyLen > 0, std::memory_order_release);
    }

    // Configura parâmetros de observador e alvo local de forma atômica
    void UpdateObserverState(uintptr_t localCharacterPtr, const FVector3f& origin) noexcept {
        local_character_ptr_.store(localCharacterPtr, std::memory_order_release);
        observer_x_.store(origin.x, std::memory_order_relaxed);
        observer_y_.store(origin.y, std::memory_order_relaxed);
        observer_z_.store(origin.z, std::memory_order_release);
    }

    // Leitura lock-free do último snapshot publicado pela thread de diagnóstico
    [[nodiscard]] const FDiagnosticFrameSnapshot& ReadLatestSnapshot() const noexcept {
        const uint32_t idx = published_index_.load(std::memory_order_acquire) & 1U;
        return snapshots_[idx];
    }

    // Executa um único passo determinístico do pipeline (pode ser chamado via Tick Hook ou Worker)
    void StepPipelineTick(float deltaSeconds, float worldTimeSeconds) noexcept {
        // 1. Garante que a base do libUE4.so e o GNamePool já foram resolvidos
        uintptr_t baseAddr = module_base_.load(std::memory_order_acquire);
        if (baseAddr == 0) {
            const auto foundBase = ModuleLocator::FindModuleBase("libUE4.so");
            if (!foundBase) {
                return;
            }
            baseAddr = *foundBase;
            module_base_.store(baseAddr, std::memory_order_release);
        }

        if (!name_pool_.has_value()) {
            name_pool_ = GNamePoolResolver::Initialize(baseAddr);
            if (!name_pool_.has_value()) {
                return; // GNamePool ainda não inicializado pelo motor
            }
        }

        // 2. Valida UWorld e PersistentLevel (Step 1)
        const auto worldCtxOpt = WorldInitializer::InitializeFromModuleBase(baseAddr);
        if (!worldCtxOpt) {
            // Tela de carregamento ou transição de mapa: invalida estado de personagem local
            HandleLevelTransition(0, 0);
            return;
        }

        const FWorldLevelContext& worldCtx = *worldCtxOpt;
        if (worldCtx.UWorldPtr != last_uworld_ ||
            worldCtx.PersistentLevelPtr != last_persistent_level_)
        {
            HandleLevelTransition(worldCtx.UWorldPtr, worldCtx.PersistentLevelPtr);
            return; // Aguarda o próximo ciclo para estabilizar o novo PersistentLevel
        }

        // 3. Coleta e classifica entidades no PersistentLevel (Steps 2 & 3)
        const FVector3f observerOrigin{
            observer_x_.load(std::memory_order_relaxed),
            observer_y_.load(std::memory_order_relaxed),
            observer_z_.load(std::memory_order_acquire)
        };

        const size_t entityCount = spatial_filter_.CollectTelemetry(
            worldCtx,
            *name_pool_,
            observerOrigin,
            15000.0f
        );

        // 4. Atualiza ajuste cinemático, override de PlayerNamePrivate e simulador de tarefas (Step 4 & 4B)
        const uintptr_t localChar = local_character_ptr_.load(std::memory_order_acquire);
        if (localChar != 0) {
            if (!kinematic_ctrl_.Snapshot().bCaptured ||
                kinematic_ctrl_.Snapshot().CharacterPtr != localChar)
            {
                (void)kinematic_ctrl_.CaptureBaseline(localChar);
            }
            if (kinematic_ctrl_.Snapshot().bCaptured) {
                (void)kinematic_ctrl_.ApplyKinematicTuning(kinematic_cfg_);
            }
        }

        // Executa o override de PlayerNamePrivate uma vez por sessão/mapa (ou quando reconfigurado via C-ABI)
        const uint32_t nameSlot = active_name_slot_.load(std::memory_order_acquire) & 1U;
        const FStackName& targetName = staged_name_bufs_[nameSlot];
        if (targetName.Length > 0) {
            if (const auto playerStateOpt = LocalPlayerNameController::ResolveLocalPlayerState(worldCtx.UWorldPtr, localChar)) {
                const bool forceReapply = name_override_dirty_.exchange(false, std::memory_order_acq_rel);
                if (forceReapply || !name_ctrl_.IsAppliedForCurrentState(*playerStateOpt)) {
                    (void)name_ctrl_.ApplyDisplayNameOverride(*playerStateOpt, targetName.View());
                }
            }
        }

        (void)task_simulator_.TickAllTasks(deltaSeconds, worldTimeSeconds, false);

        // 5. Publica os resultados no back-buffer e alterna o índice atômico (Lock-Free Flip)
        const uint32_t writeIdx = (published_index_.load(std::memory_order_relaxed) ^ 1U) & 1U;
        FDiagnosticFrameSnapshot& backBuf = snapshots_[writeIdx];

        backBuf.FrameSequence         = ++frame_sequence_;
        backBuf.ActiveUWorld          = worldCtx.UWorldPtr;
        backBuf.ActivePersistentLevel = worldCtx.PersistentLevelPtr;
        backBuf.TimestampSeconds      = worldTimeSeconds;
        backBuf.EntityCount           = entityCount;

        const auto samplesView = spatial_filter_.ViewSamples();
        for (size_t i = 0; i < samplesView.size(); ++i) {
            backBuf.Entities[i] = samplesView[i];
        }

        if (const auto latestEv = task_simulator_.LatestEvent()) {
            backBuf.LatestTaskEvent = *latestEv;
            backBuf.bHasTaskEvent   = true;
        } else {
            backBuf.bHasTaskEvent   = false;
        }

        published_index_.store(writeIdx, std::memory_order_release);
    }

private:
    RuntimePipelineOrchestrator() noexcept = default;

    void HandleLevelTransition(uintptr_t newWorld, uintptr_t newLevel) noexcept {
        last_uworld_           = newWorld;
        last_persistent_level_ = newLevel;
        local_character_ptr_.store(0, std::memory_order_release);
        kinematic_ctrl_ = CharacterKinematicController{};
        name_ctrl_.ResetState();
    }

    static void* WorkerThreadEntry(void* arg) noexcept {
        auto* self = static_cast<RuntimePipelineOrchestrator*>(arg);
#if defined(__linux__) || defined(__ANDROID__)
        pthread_setname_np(pthread_self(), "ue4_diag_worker");
#endif
        self->WorkerLoop();
        return nullptr;
    }

    void WorkerLoop() noexcept {
        const uint64_t startNs = MonotonicClock::NowNs();
        uint64_t prevTickNs    = startNs;

        while (running_.load(std::memory_order_acquire)) {
            // Fase A: Aguarda libUE4.so carregar no espaço de endereçamento
            if (module_base_.load(std::memory_order_relaxed) == 0) {
                const auto found = ModuleLocator::FindModuleBase("libUE4.so");
                if (!found) {
                    MonotonicClock::SleepMicroseconds(kModulePollIntervalUs);
                    prevTickNs = MonotonicClock::NowNs();
                    continue;
                }
                module_base_.store(*found, std::memory_order_release);
            }

            // Fase B: Calcula deltaSeconds monotônico
            const uint64_t nowNs = MonotonicClock::NowNs();
            float deltaSec = static_cast<float>(nowNs - prevTickNs) * 1e-9f;
            prevTickNs = nowNs;

            if (deltaSec <= 0.0f || deltaSec > 0.25f) {
                deltaSec = 0.016667f;
            }
            const float worldTimeSec = static_cast<float>(nowNs - startNs) * 1e-9f;

            // Fase C: Executa os 4 passos do pipeline
            StepPipelineTick(deltaSec, worldTimeSec);

            // Fase D: Cadência controlada (~60 Hz ou pausa de transição se UWorld == 0)
            if (last_uworld_ == 0) {
                MonotonicClock::SleepMicroseconds(kLevelTransitionWaitUs);
            } else {
                MonotonicClock::SleepMicroseconds(kTargetTickIntervalUs);
            }
        }
    }

    std::atomic<bool>      running_{false};
    std::atomic<uintptr_t> module_base_{0};
    std::atomic<uintptr_t> local_character_ptr_{0};
    std::atomic<float>     observer_x_{0.0f};
    std::atomic<float>     observer_y_{0.0f};
    std::atomic<float>     observer_z_{0.0f};
    std::atomic<uint32_t>  published_index_{0};

#if defined(__linux__) || defined(__ANDROID__)
    pthread_t              worker_thread_{};
#endif

    uintptr_t              last_uworld_{0};
    uintptr_t              last_persistent_level_{0};
    uint64_t               frame_sequence_{0};

    std::optional<GNamePoolResolver>   name_pool_{std::nullopt};
    EntitySpatialTelemetryFilter<512>  spatial_filter_{};
    CharacterKinematicController       kinematic_ctrl_{};
    FKinematicOverrideConfig           kinematic_cfg_{
        .SpeedMultiplier        = 1.10f,
        .AccelerationMultiplier = 1.00f,
        .VelocityScale          = 1.00f,
        .MaxAllowedSpeedCeiling = 600.0f
    };
    LocalPlayerNameController          name_ctrl_{};
    std::array<FStackName, 2>          staged_name_bufs_{};
    std::atomic<uint32_t>              active_name_slot_{0};
    std::atomic<bool>                  name_override_dirty_{false};
    ChargeableTaskSimulator<16, 256>   task_simulator_{};

    std::array<FDiagnosticFrameSnapshot, 2> snapshots_{};
};

// ============================================================================
// 4. Utilitário Opcional de Hook de VTable (In-Process UObject Tick Hook)
// ============================================================================

class VTableTickHook {
public:
    using ProcessEventFn = void(*)(void* uObject, void* uFunction, void* parms) noexcept;

    [[nodiscard]] static bool InstallVTableSlotHook(
        uintptr_t objectInstance,
        size_t vtableIndex,
        void* replacementFn,
        void** outOriginalFn) noexcept
    {
#if defined(__linux__) || defined(__ANDROID__)
        if (!InProcessMemory::IsValidPtr<uintptr_t>(objectInstance) || !replacementFn || !outOriginalFn) {
            return false;
        }

        const auto vtableBaseOpt = InProcessMemory::Read<uintptr_t>(objectInstance);
        if (!vtableBaseOpt || !InProcessMemory::IsValidPtr<uintptr_t>(*vtableBaseOpt)) {
            return false;
        }

        const uintptr_t slotAddr = *vtableBaseOpt + (vtableIndex * sizeof(uintptr_t));
        const auto currentFnOpt  = InProcessMemory::Read< uintptr_t >(slotAddr);
        if (!currentFnOpt || !InProcessMemory::IsValidPtr<void>(*currentFnOpt)) {
            return false;
        }

        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0) {
            return false;
        }
        const uintptr_t pageMask = ~(static_cast<uintptr_t>(pageSize) - 1ULL);
        const uintptr_t pageAddr = InProcessMemory::UntagAddress(slotAddr) & pageMask;

        if (mprotect(reinterpret_cast<void*>(pageAddr), static_cast<size_t>(pageSize),
                     PROT_READ | PROT_WRITE) != 0)
        {
            return false;
        }

        *outOriginalFn = reinterpret_cast<void*>(*currentFnOpt);
        const uintptr_t newFnAddr = reinterpret_cast<uintptr_t>(replacementFn);
        const bool wrote = InProcessMemory::Write<uintptr_t>(slotAddr, newFnAddr);

        (void)mprotect(reinterpret_cast<void*>(pageAddr), static_cast<size_t>(pageSize),
                       PROT_READ);
        __builtin___clear_cache(
            reinterpret_cast<char*>(slotAddr),
            reinterpret_cast<char*>(slotAddr + sizeof(uintptr_t))
        );
        return wrote;
#else
        (void)objectInstance;
        (void)vtableIndex;
        (void)replacementFn;
        (void)outOriginalFn;
        return false;
#endif
    }
};

} // namespace ue4::aarch64::runtime

// ============================================================================
// 5. ELF64 Constructor / Destructor Lifecycle Entrypoints (.init_array / .fini_array)
// ============================================================================

__attribute__((constructor))
static void OnLibraryLoaded() noexcept {
    (void)ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance().StartAsyncWorker();
}

__attribute__((destructor))
static void OnLibraryUnloaded() noexcept {
    ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance().StopWorker();
}
