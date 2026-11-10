// ============================================================================
// File          : lib_main.cpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20 (-std=c++20 -O3 -fno-exceptions -fno-rtti -fPIC)
// Purpose       : Native .so lifecycle entrypoint (.init_array / .fini_array / JNI_OnLoad)
// ============================================================================

#include "ue4_runtime_pipeline.hpp"

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#define DIAG_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO,  "UE4Diag", fmt __VA_OPT__(,) __VA_ARGS__)
#define DIAG_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "UE4Diag", fmt __VA_OPT__(,) __VA_ARGS__)
#else
#include <cstdio>
#define DIAG_LOGI(fmt, ...) std::printf("[UE4Diag][INFO] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#define DIAG_LOGE(fmt, ...) std::printf("[UE4Diag][ERR]  " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#endif

namespace {

// Controle atômico para evitar inicialização duplicada entre .init_array e JNI_OnLoad
std::atomic<bool> g_ModuleInitialized{false};

void InitializeDiagnosticsSuite() noexcept {
    bool expected = false;
    if (!g_ModuleInitialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    auto& orchestrator = ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance();
    if (orchestrator.StartAsyncWorker()) {
        DIAG_LOGI("Telemetry worker initialized (target=libUE4.so, arch=arm64-v8a).");
    } else {
        DIAG_LOGE("Failed to spawn telemetry worker thread.");
    }
}

void ShutdownDiagnosticsSuite() noexcept {
    if (!g_ModuleInitialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    auto& orchestrator = ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance();
    orchestrator.StopWorker();
    DIAG_LOGI("Telemetry worker stopped and baseline state restored.");
}

} // namespace

// ============================================================================
// ELF64 .init_array / .fini_array Hooks
// ============================================================================

__attribute__((constructor(101), used, visibility("default")))
static void OnSharedLibraryInit() noexcept {
    InitializeDiagnosticsSuite();
}

__attribute__((destructor(101), used, visibility("default")))
static void OnSharedLibraryFini() noexcept {
    ShutdownDiagnosticsSuite();
}

// ============================================================================
// C ABI / JNI Export Table (Compatível com System.loadLibrary ou dlopen)
// ============================================================================

#if defined(__ANDROID__)
extern "C" __attribute__((visibility("default")))
jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    if (!vm) {
        return JNI_ERR;
    }
    InitializeDiagnosticsSuite();
    return JNI_VERSION_1_6;
}

extern "C" __attribute__((visibility("default")))
void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    ShutdownDiagnosticsSuite();
}
#endif

// API C opcional para consulta externa do snapshot mais recente
extern "C" __attribute__((visibility("default")))
const ue4::aarch64::runtime::FDiagnosticFrameSnapshot* UE4Diag_GetLatestSnapshot() noexcept {
    return &ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance().ReadLatestSnapshot();
}

extern "C" __attribute__((visibility("default")))
void UE4Diag_SetObserverState(uintptr_t localCharacterPtr, float x, float y, float z) noexcept {
    ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance().UpdateObserverState(
        localCharacterPtr,
        ue4::aarch64::FVector3f{x, y, z}
    );
}

extern "C" __attribute__((visibility("default")))
void UE4Diag_SetCustomPlayerName(const char* newName) noexcept {
    if (!newName) {
        return;
    }
    const size_t len = std::strlen(newName);
    if (len == 0) {
        return;
    }
    ue4::aarch64::runtime::RuntimePipelineOrchestrator::Instance().SetCustomDisplayNameOverride(
        std::string_view(newName, len)
    );
}
