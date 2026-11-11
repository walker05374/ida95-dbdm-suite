#include "ue4_aarch64_unified_diagnostics.hpp"
#include <cstdio>

void ExecuteDiagnosticCycle(
    uintptr_t explicitModuleBase,
    uintptr_t localPlayerCharacterPtr,
    const ue4::aarch64::FVector3f& observerOrigin,
    float deltaSeconds,
    float worldTimeSeconds) noexcept
{
    using namespace ue4::aarch64;

    // ------------------------------------------------------------------------
    // STEP 1: Resolver Base do Módulo, GWorld (0x10D69998) e PersistentLevel
    // ------------------------------------------------------------------------
    const uintptr_t moduleBase = (explicitModuleBase != 0)
        ? explicitModuleBase
        : ModuleLocator::FindModuleBase("libUE4.so").value_or(0);

    const auto worldCtxOpt = WorldInitializer::InitializeFromModuleBase(moduleBase);
    if (!worldCtxOpt) {
        return;
    }

    // ------------------------------------------------------------------------
    // STEP 2: Inicializar Decodificador GNamePool (0x10B7C100)
    // ------------------------------------------------------------------------
    const auto namePoolOpt = GNamePoolResolver::Initialize(moduleBase);
    if (!namePoolOpt) {
        return;
    }

    // ------------------------------------------------------------------------
    // STEP 3: Filtrar Atores e Extrair Telemetria Espacial (+0x140 / +0x134)
    // ------------------------------------------------------------------------
    static EntitySpatialTelemetryFilter<512> spatialFilter{};
    const size_t matchedCount = spatialFilter.CollectTelemetry(
        *worldCtxOpt,
        *namePoolOpt,
        observerOrigin,
        15000.0f // Raio de 150m em unidades UE4
    );

    for (const FEntityTelemetrySample& entity : spatialFilter.ViewSamples()) {
        const std::string_view catStr = ActorNameClassifier::CategoryToString(entity.Category);
        const std::string_view rawStr = namePoolOpt->ResolveView(entity.ComparisonIndex).value_or("<invalid>");
        std::printf(
            "[Step3][%-9.*s] Ptr=0x%llX | Pos=(%.1f, %.1f, %.1f) | Dist=%.1fm | Name=%.*s\n",
            static_cast<int>(catStr.size()), catStr.data(),
            static_cast<unsigned long long>(entity.ActorPtr),
            static_cast<double>(entity.WorldLocation.x),
            static_cast<double>(entity.WorldLocation.y),
            static_cast<double>(entity.WorldLocation.z),
            static_cast<double>(entity.DistanceMeters),
            static_cast<int>(rawStr.size()), rawStr.data()
        );
    }
    (void)matchedCount;

    // ------------------------------------------------------------------------
    // STEP 4: Ajuste Cinemático e Simulação de Tarefas Carregáveis
    // ------------------------------------------------------------------------
    static CharacterKinematicController kinematicCtrl{};
    if (!kinematicCtrl.Snapshot().bCaptured && localPlayerCharacterPtr != 0) {
        (void)kinematicCtrl.CaptureBaseline(localPlayerCharacterPtr);
    }

    if (kinematicCtrl.Snapshot().bCaptured) {
        const FKinematicOverrideConfig tuneCfg{
            .SpeedMultiplier        = 1.10f,
            .AccelerationMultiplier = 1.00f,
            .VelocityScale          = 1.00f,
            .MaxAllowedSpeedCeiling = 600.0f
        };
        (void)kinematicCtrl.ApplyKinematicTuning(tuneCfg);
    }

    static ChargeableTaskSimulator<16, 256> taskSimulator{};
    static bool bGeneratorTaskRegistered = false;
    if (!bGeneratorTaskRegistered) {
        bGeneratorTaskRegistered = taskSimulator.RegisterTask(FChargeableTaskState{
            .TargetActorPtr     = 0,
            .TaskId             = 1U,
            .ActionType         = ETaskActionType::GeneratorRepair,
            .CurrentCharges     = 0.0f,
            .MaxChargesRequired = 90.0f,
            .BaseRatePerSecond  = 1.0f,
            .RateMultiplier     = 1.25f,
            .bActive            = true,
            .bCompleted         = false
        });
    }

    if (taskSimulator.TickAllTasks(deltaSeconds, worldTimeSeconds, false) > 0) {
        if (const auto evOpt = taskSimulator.LatestEvent()) {
            std::printf(
                "[Step4][Task #%u] Charges=%.2f/90.0 (%.1f%%) | Seq=%llu\n",
                evOpt->TaskId,
                static_cast<double>(evOpt->CurrentCharges),
                static_cast<double>(evOpt->NormalizedPercent),
                static_cast<unsigned long long>(evOpt->SequenceNumber)
            );
        }
    }
}
