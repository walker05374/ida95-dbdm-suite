// ============================================================================
// Architecture : ELF64 AArch64 (ARM64) | Unreal Engine 4.27 (libUE4.so)
// Standard     : C++20
// Compiler Flags: -std=c++20 -O3 -fno-exceptions -fno-rtti -Wall -Wextra
// Description  : Unified 4-Step In-Process Instrumentation & Diagnostic Suite
// ============================================================================

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#if defined(__linux__) || defined(__ANDROID__)
#include <link.h>
#endif

namespace ue4::aarch64 {

// ============================================================================
// TABELA GLOBAL DE OFFSETS E ÂNCORAS ESTÁTICAS (UE 4.27 ELF64)
// ============================================================================

namespace Offsets {
    // Âncoras Globais (RVAs relativos à base do libUE4.so — Grau A Rev. 7)
    inline constexpr uintptr_t GWorld                        = 0x10D69998ULL; // GOT 0x0F668440
    inline constexpr uintptr_t GEngine                       = 0x10D65798ULL; // GOT 0x0F6682A8
    inline constexpr uintptr_t GUObjectArray                 = 0x10BF7920ULL; // GOT 0x0F6685C8 (ObjObjects em 0x10BF7930)
    inline constexpr uintptr_t GNamePool                     = 0x10B7C100ULL;
    inline constexpr uintptr_t bNamePoolInitialized          = 0x10BCF0C0ULL;

    // UObject Base (sizeof(UObjectBase) = 0x30, WITH_CASE_PRESERVING_NAME = 1)
    inline constexpr uintptr_t UObject_ObjectFlags           = 0x0008ULL; // int32_t
    inline constexpr uintptr_t UObject_InternalIndex         = 0x000CULL; // int32_t
    inline constexpr uintptr_t UObject_ClassPrivate          = 0x0010ULL; // UClass*
    inline constexpr uintptr_t UObject_NamePrivate           = 0x0018ULL; // FName (ComparisonIndex +0x18, DisplayIndex +0x1C, Number +0x20)
    inline constexpr uintptr_t UObject_OuterPrivate          = 0x0028ULL; // UObject*

    // FNamePool Internal Layout (UE 4.27 com WITH_CASE_PRESERVING_NAME = 1 — Grau A T12)
    inline constexpr uintptr_t FNamePool_Lock                = 0x0000ULL; // FRWLock (0x38 bytes)
    inline constexpr uintptr_t FNamePool_CurrentBlock        = 0x0038ULL; // uint32_t
    inline constexpr uintptr_t FNamePool_CurrentByteCursor   = 0x003CULL; // uint32_t
    inline constexpr uintptr_t FNamePool_Blocks              = 0x0040ULL; // uint8_t* Blocks[8192] (0x10B7C140)
    inline constexpr uintptr_t FNameEntry_ComparisonId       = 0x0000ULL; // uint32_t (4 bytes)
    inline constexpr uintptr_t FNameEntry_Header             = 0x0004ULL; // uint16_t (bIsWide = bit 0, Len = Header >> 1)
    inline constexpr uintptr_t FNameEntry_StringData         = 0x0006ULL; // char AnsiName[Len] / char16_t WideName[Len]

    // UWorld -> ULevel -> TArray<AActor*>
    inline constexpr uintptr_t UWorld_PersistentLevel        = 0x0038ULL; // ULevel* (LDR X0, [X0, #0x38] @ 0x0C78A6C0)
    inline constexpr uintptr_t ULevel_ActorsData             = 0x00A0ULL; // AActor**
    inline constexpr uintptr_t ULevel_ActorsCount            = 0x00A8ULL; // int32_t
    inline constexpr uintptr_t ULevel_ActorsMax              = 0x00ACULL; // int32_t

    // AActor -> USceneComponent -> Transform
    inline constexpr uintptr_t AActor_CustomTimeDilation     = 0x0098ULL; // float
    inline constexpr uintptr_t AActor_RootComponent          = 0x0140ULL; // USceneComponent*
    inline constexpr uintptr_t USceneComp_RelativeLocation   = 0x0134ULL; // FVector3f (12 bytes)
    inline constexpr uintptr_t USceneComp_RelativeRotation   = 0x0140ULL; // FRotator (12 bytes)

    // ACharacter -> UCharacterMovementComponent
    inline constexpr uintptr_t ACharacter_CharacterMovement  = 0x0290ULL; // UCharacterMovementComponent* / Mesh
    inline constexpr uintptr_t UMovementComp_Velocity        = 0x0140ULL; // FVector3f (12 bytes)
    inline constexpr uintptr_t UCharMove_MaxWalkSpeed        = 0x018CULL; // float
    inline constexpr uintptr_t UCharMove_MaxWalkSpeedCrouched= 0x0190ULL; // float
    inline constexpr uintptr_t UCharMove_MaxAcceleration     = 0x0198ULL; // float

    // Chargeable Component Progress
    inline constexpr uintptr_t UChargeable_CurrentCharge     = 0x0338ULL; // float

    // UWorld -> UGameInstance -> ULocalPlayer -> APlayerController -> APlayerState -> PlayerNamePrivate (Grau A T13)
    inline constexpr uintptr_t UWorld_OwningGameInstance     = 0x0410ULL; // UGameInstance* (LDR X0, [X0, #0x410] @ 0x0C1BFDFC)
    inline constexpr uintptr_t UGameInstance_LocalPlayers    = 0x0040ULL; // TArray<ULocalPlayer*> Data (FClassParams 0x0C85FE90)
    inline constexpr uintptr_t UGameInstance_LocalPlayersNum = 0x0048ULL; // int32_t ArrayNum
    inline constexpr uintptr_t UPlayer_PlayerController      = 0x0038ULL; // APlayerController* (FClassParams 0x0C99FE40)
    inline constexpr uintptr_t APlayerController_Pawn        = 0x02B8ULL; // APawn* AcknowledgedPawn (FClassParams 0x0F3B84D8)
    inline constexpr uintptr_t APlayerController_CameraMgr   = 0x02D0ULL; // APlayerCameraManager* (LDR X0, [X0, #0x2D0] @ 0x0C1C019C)
    inline constexpr uintptr_t AController_PlayerState       = 0x0238ULL; // APlayerState*
    inline constexpr uintptr_t APawn_PlayerState             = 0x0248ULL; // APlayerState* (fallback via LocalPawn)
    inline constexpr uintptr_t APlayerState_PlayerNamePrivate= 0x0310ULL; // FString (TArray<char16_t>)
}

// ============================================================================
// STEP 1: ACESSOR DE MEMÓRIA IN-PROCESS E RESOLUÇÃO DO GWORLD / PERSISTENTLEVEL
// ============================================================================

struct InProcessMemory {
    // Remove tags TBI (Top-Byte Ignore) / MTE e bits PAC superiores do AArch64,
    // preservando o endereço virtual canônico de 48 bits (bits 0..47).
    [[nodiscard]] static constexpr uintptr_t UntagAddress(uintptr_t addr) noexcept {
        return addr & 0x0000FFFFFFFFFFFFULL;
    }

    // Valida limites canônicos de user-space (48-bit VA) e alinhamento natural do tipo T.
    template <typename T = void>
    [[nodiscard]] static constexpr bool IsValidPtr(uintptr_t rawAddr) noexcept {
        const uintptr_t addr = UntagAddress(rawAddr);
        if (addr < 0x00010000ULL || addr > 0x00007FFFFFFFFFFFULL) [[unlikely]] {
            return false;
        }
        if constexpr (!std::is_void_v<T>) {
            if constexpr (alignof(T) > 1U) {
                if ((addr & (alignof(T) - 1U)) != 0U) [[unlikely]] {
                    return false;
                }
            }
        }
        return true;
    }

    // Leitura direta in-process segura.
    template <typename T>
    [[nodiscard]] static std::optional<T> Read(uintptr_t rawAddr) noexcept {
        if (!IsValidPtr<T>(rawAddr)) [[unlikely]] {
            return std::nullopt;
        }
        T val{};
        std::memcpy(&val, reinterpret_cast<const void*>(rawAddr), sizeof(T));
        return val;
    }

    // Escrita direta in-process segura.
    template <typename T>
    [[nodiscard]] static bool Write(uintptr_t rawAddr, const T& value) noexcept {
        if (!IsValidPtr<T>(rawAddr)) [[unlikely]] {
            return false;
        }
        std::memcpy(reinterpret_cast<void*>(rawAddr), &value, sizeof(T));
        return true;
    }

    // Visão zero-copy sobre um array contíguo na memória do próprio processo.
    template <typename T>
    [[nodiscard]] static std::optional<std::span<const T>> ViewArray(
        uintptr_t rawDataAddr,
        size_t count) noexcept
    {
        if (count == 0) {
            return std::span<const T>{};
        }
        if (!IsValidPtr<T>(rawDataAddr)) [[unlikely]] {
            return std::nullopt;
        }
        const uintptr_t byteSize = count * sizeof(T);
        const uintptr_t untaggedStart = UntagAddress(rawDataAddr);
        if (untaggedStart + byteSize < untaggedStart ||
            !IsValidPtr<void>(untaggedStart + byteSize - 1U)) [[unlikely]]
        {
            return std::nullopt;
        }
        return std::span<const T>(reinterpret_cast<const T*>(rawDataAddr), count);
    }
};

class ModuleLocator {
public:
    [[nodiscard]] static std::optional<uintptr_t> FindModuleBase(
        std::string_view moduleName = "libUE4.so") noexcept
    {
#if defined(__linux__) || defined(__ANDROID__)
        struct SearchQuery {
            std::string_view Target;
            uintptr_t FoundBase{0};
        } query{moduleName, 0};

        auto callback = [](struct dl_phdr_info* info, size_t /*size*/, void* data) noexcept -> int {
            auto* q = static_cast<SearchQuery*>(data);
            if (!info || !info->dlpi_name || info->dlpi_addr == 0) {
                return 0;
            }
            const std::string_view path(info->dlpi_name);
            if (path.ends_with(q->Target)) {
                q->FoundBase = static_cast<uintptr_t>(info->dlpi_addr);
                return 1;
            }
            return 0;
        };

        dl_iterate_phdr(callback, &query);
        if (InProcessMemory::IsValidPtr<uint32_t>(query.FoundBase)) {
            return query.FoundBase;
        }
#else
        (void)moduleName;
#endif
        return std::nullopt;
    }
};

struct alignas(64) FWorldLevelContext {
    uintptr_t ModuleBase{0};
    uintptr_t UWorldPtr{0};
    uintptr_t PersistentLevelPtr{0};
    uintptr_t ActorArrayData{0};
    int32_t   ActorCount{0};
    int32_t   ActorMax{0};
    std::span<const uintptr_t> ActorPointers{};
};

class WorldInitializer {
public:
    static constexpr int32_t kMaxSaneActors = 65536;

    [[nodiscard]] static std::optional<FWorldLevelContext> InitializeFromModuleBase(
        uintptr_t moduleBase) noexcept
    {
        if (!InProcessMemory::IsValidPtr<uintptr_t>(moduleBase)) [[unlikely]] {
            return std::nullopt;
        }

        // 1. Dereferencia GWorld em moduleBase + 0x10D69998
        const uintptr_t gWorldAddr = moduleBase + Offsets::GWorld;
        const auto uWorldOpt = InProcessMemory::Read<uintptr_t>(gWorldAddr);
        if (!uWorldOpt || !InProcessMemory::IsValidPtr<uintptr_t>(*uWorldOpt)) {
            return std::nullopt;
        }
        const uintptr_t uWorld = *uWorldOpt;

        // 2. Navega para PersistentLevel em UWorld + 0x0038
        const auto persistentLevelOpt = InProcessMemory::Read<uintptr_t>(
            uWorld + Offsets::UWorld_PersistentLevel
        );
        if (!persistentLevelOpt || !InProcessMemory::IsValidPtr<uintptr_t>(*persistentLevelOpt)) {
            return std::nullopt;
        }
        const uintptr_t persistentLevel = *persistentLevelOpt;

        // 3. Extrai TArray<AActor*>: Data (+0x00A0), Count (+0x00A8), Max (+0x00AC)
        const auto actorsDataOpt  = InProcessMemory::Read<uintptr_t>(persistentLevel + Offsets::ULevel_ActorsData);
        const auto actorsCountOpt = InProcessMemory::Read<int32_t>(persistentLevel + Offsets::ULevel_ActorsCount);
        const auto actorsMaxOpt   = InProcessMemory::Read<int32_t>(persistentLevel + Offsets::ULevel_ActorsMax);

        if (!actorsDataOpt || !actorsCountOpt || !actorsMaxOpt) [[unlikely]] {
            return std::nullopt;
        }

        const int32_t count = *actorsCountOpt;
        const int32_t max   = *actorsMaxOpt;

        if (count < 0 || max < count || max > kMaxSaneActors) [[unlikely]] {
            return std::nullopt;
        }

        const auto spanOpt = InProcessMemory::ViewArray<uintptr_t>(
            *actorsDataOpt,
            static_cast<size_t>(count)
        );
        if (!spanOpt) [[unlikely]] {
            return std::nullopt;
        }

        return FWorldLevelContext{
            .ModuleBase         = moduleBase,
            .UWorldPtr          = uWorld,
            .PersistentLevelPtr = persistentLevel,
            .ActorArrayData     = *actorsDataOpt,
            .ActorCount         = count,
            .ActorMax           = max,
            .ActorPointers      = *spanOpt
        };
    }
};

// ============================================================================
// STEP 2: SUBSISTEMA DE RESOLUÇÃO DE NOMES (GNAMEPOOL - 0x10B7C100, Stride=4)
// ============================================================================

struct FName {
    uint32_t ComparisonIndex{0}; // +0x00 (em UObject +0x18)
    uint32_t DisplayIndex{0};    // +0x04 (em UObject +0x1C — usado por FName::ToString)
    uint32_t Number{0};          // +0x08 (em UObject +0x20)
};
static_assert(sizeof(FName) == 12, "UE 4.27 FName (WITH_CASE_PRESERVING_NAME=1) must be 12 bytes");

struct FNameEntryHeader {
    uint16_t Raw{0};

    [[nodiscard]] constexpr bool IsWide() const noexcept {
        return (Raw & 0x0001U) != 0U;
    }

    [[nodiscard]] constexpr uint16_t Length() const noexcept {
        return static_cast<uint16_t>(Raw >> 1U); // LDRH W8, [X21, #0x4]; LSR W1, W8, #1 @ 0x0A59F86C
    }
};
static_assert(sizeof(FNameEntryHeader) == 2, "UE 4.27 FNameEntryHeader must be 2 bytes");

struct alignas(64) FStackName {
    static constexpr size_t kMaxCapacity = 126;
    std::array<char, kMaxCapacity + 1> Buffer{};
    uint8_t Length{0};

    [[nodiscard]] constexpr std::string_view View() const noexcept {
        return std::string_view(Buffer.data(), Length);
    }

    [[nodiscard]] constexpr const char* CStr() const noexcept {
        return Buffer.data();
    }
};

class GNamePoolResolver {
public:
    static constexpr uint32_t kBlockBitShift = 16U;
    static constexpr uint32_t kOffsetMask    = (1U << kBlockBitShift) - 1U; // 0xFFFFU
    static constexpr uint32_t kStride        = 4U;                          // WITH_CASE_PRESERVING_NAME = 1
    static constexpr uint32_t kMaxBlocks     = 8192U;
    static constexpr uint32_t kMaxBlockBytes = kStride * (1U << kBlockBitShift); // 262144 bytes
    static constexpr uint16_t kMaxNameLength = 1023U;

    [[nodiscard]] static std::optional<GNamePoolResolver> Initialize(uintptr_t moduleBase) noexcept {
        if (!InProcessMemory::IsValidPtr<uintptr_t>(moduleBase)) [[unlikely]] {
            return std::nullopt;
        }

        const uintptr_t poolAddr = moduleBase + Offsets::GNamePool;
        if (!InProcessMemory::IsValidPtr<uintptr_t>(poolAddr)) [[unlikely]] {
            return std::nullopt;
        }

        const auto currentBlockOpt = InProcessMemory::Read<uint32_t>(
            poolAddr + Offsets::FNamePool_CurrentBlock
        );
        if (!currentBlockOpt || *currentBlockOpt >= kMaxBlocks) [[unlikely]] {
            return std::nullopt;
        }

        const auto block0Opt = InProcessMemory::Read<uintptr_t>(
            poolAddr + Offsets::FNamePool_Blocks
        );
        if (!block0Opt || !InProcessMemory::IsValidPtr<uint8_t>(*block0Opt)) [[unlikely]] {
            return std::nullopt;
        }

        return GNamePoolResolver(poolAddr);
    }

    // Resolução zero-copy diretamente sobre o bloco FNamePool na memória
    [[nodiscard]] std::optional<std::string_view> ResolveView(uint32_t nameEntryId) const noexcept {
        const uint32_t blockIdx   = nameEntryId >> kBlockBitShift;
        const uint32_t byteOffset = (nameEntryId & kOffsetMask) * kStride;

        const auto currentBlockOpt = InProcessMemory::Read<uint32_t>(
            pool_base_ + Offsets::FNamePool_CurrentBlock
        );
        if (!currentBlockOpt || blockIdx > *currentBlockOpt || blockIdx >= kMaxBlocks) [[unlikely]] {
            return std::nullopt;
        }

        if (byteOffset + Offsets::FNameEntry_StringData >= kMaxBlockBytes) [[unlikely]] {
            return std::nullopt;
        }

        const uintptr_t blockSlotAddr = pool_base_
                                      + Offsets::FNamePool_Blocks
                                      + (static_cast<uintptr_t>(blockIdx) * sizeof(uintptr_t));

        const auto blockPtrOpt = InProcessMemory::Read<uintptr_t>(blockSlotAddr);
        if (!blockPtrOpt || !InProcessMemory::IsValidPtr<FNameEntryHeader>(*blockPtrOpt)) [[unlikely]] {
            return std::nullopt;
        }

        const uintptr_t entryAddr = *blockPtrOpt + byteOffset;
        const auto headerOpt = InProcessMemory::Read<FNameEntryHeader>(entryAddr + Offsets::FNameEntry_Header);
        if (!headerOpt) [[unlikely]] {
            return std::nullopt;
        }

        const FNameEntryHeader hdr = *headerOpt;
        const uint16_t len = hdr.Length();

        // Filtra strings wide (UTF-16) e entradas vazias/inválidas
        if (hdr.IsWide() || len == 0 || len > kMaxNameLength) {
            return std::nullopt;
        }

        if (byteOffset + Offsets::FNameEntry_StringData + len > kMaxBlockBytes) [[unlikely]] {
            return std::nullopt;
        }

        const uintptr_t strAddr = entryAddr + Offsets::FNameEntry_StringData;
        const auto charSpanOpt = InProcessMemory::ViewArray<char>(strAddr, len);
        if (!charSpanOpt) [[unlikely]] {
            return std::nullopt;
        }

        return std::string_view(charSpanOpt->data(), charSpanOpt->size());
    }

    [[nodiscard]] std::optional<FStackName> ResolveCopy(uint32_t comparisonIndex) const noexcept {
        const auto viewOpt = ResolveView(comparisonIndex);
        if (!viewOpt) {
            return std::nullopt;
        }

        FStackName out{};
        const size_t copyLen = viewOpt->size() < FStackName::kMaxCapacity
                                   ? viewOpt->size()
                                   : FStackName::kMaxCapacity;

        std::memcpy(out.Buffer.data(), viewOpt->data(), copyLen);
        out.Buffer[copyLen] = '\0';
        out.Length = static_cast<uint8_t>(copyLen);
        return out;
    }

    [[nodiscard]] static constexpr uint32_t HashFnv1a(std::string_view str) noexcept {
        uint32_t hash = 0x811C9DC5U;
        for (char c : str) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x01000193U;
        }
        return hash;
    }

private:
    explicit constexpr GNamePoolResolver(uintptr_t poolBase) noexcept
        : pool_base_(poolBase) {}

    uintptr_t pool_base_{0};
};

// ============================================================================
// STEP 3: CLASSIFICAÇÃO DE ENTIDADES E FILTRO DE TELEMETRIA ESPACIAL
// ============================================================================

struct FVector3f {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    [[nodiscard]] constexpr bool IsFinite() const noexcept {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    [[nodiscard]] constexpr FVector3f operator-(const FVector3f& rhs) const noexcept {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }

    [[nodiscard]] constexpr float SizeSquared() const noexcept {
        return (x * x) + (y * y) + (z * z);
    }

    [[nodiscard]] float DistanceTo(const FVector3f& origin) const noexcept {
        return std::sqrt((*this - origin).SizeSquared());
    }
};
static_assert(sizeof(FVector3f) == 12, "UE 4.27 FVector3f must be 12 bytes");

enum class EGameplayActorType : uint8_t {
    Unknown   = 0,
    Survivor  = 1, // Camper / Player
    Killer    = 2, // Slasher
    Totem     = 3, // Totem
    Hatch     = 4, // Hatch
    Generator = 5, // Generator
    Pallet    = 6  // Pallet / Bookshelf
};

class ActorNameClassifier {
public:
    [[nodiscard]] static constexpr bool Contains(std::string_view haystack, std::string_view needle) noexcept {
        return haystack.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] static constexpr EGameplayActorType Classify(std::string_view actorName) noexcept {
        if (actorName.empty()) [[unlikely]] {
            return EGameplayActorType::Unknown;
        }
        if (Contains(actorName, "BP_Camper") || Contains(actorName, "Camper") || Contains(actorName, "Survivor")) {
            return EGameplayActorType::Survivor;
        }
        if (Contains(actorName, "BP_Slasher") || Contains(actorName, "Slasher") || Contains(actorName, "Killer")) {
            return EGameplayActorType::Killer;
        }
        if (Contains(actorName, "Generator")) {
            return EGameplayActorType::Generator;
        }
        if (Contains(actorName, "Totem")) {
            return EGameplayActorType::Totem;
        }
        if (Contains(actorName, "Hatch")) {
            return EGameplayActorType::Hatch;
        }
        if (Contains(actorName, "Pallet") || Contains(actorName, "Bookshelf")) {
            return EGameplayActorType::Pallet;
        }
        return EGameplayActorType::Unknown;
    }

    [[nodiscard]] static constexpr std::string_view CategoryToString(EGameplayActorType type) noexcept {
        switch (type) {
            case EGameplayActorType::Survivor:  return "Survivor";
            case EGameplayActorType::Killer:    return "Killer";
            case EGameplayActorType::Totem:     return "Totem";
            case EGameplayActorType::Hatch:     return "Hatch";
            case EGameplayActorType::Generator: return "Generator";
            case EGameplayActorType::Pallet:    return "Pallet";
            default:                            return "Unknown";
        }
    }
};

struct alignas(64) FEntityTelemetrySample {
    uintptr_t          ActorPtr{0};
    uintptr_t          RootComponentPtr{0};
    uint32_t           ComparisonIndex{0};
    EGameplayActorType Category{EGameplayActorType::Unknown};
    FVector3f          WorldLocation{};
    float              DistanceMeters{0.0f};
};
static_assert(sizeof(FEntityTelemetrySample) == 64, "FEntityTelemetrySample must be 64-byte cacheline aligned");

template <size_t MaxTrackedEntities = 512>
class alignas(64) EntitySpatialTelemetryFilter {
public:
    static constexpr float kUeUnitsToMeters = 0.01f;

    [[nodiscard]] static std::optional<FVector3f> ReadActorWorldLocation(
        uintptr_t actorPtr,
        uintptr_t* outRootComp = nullptr) noexcept
    {
        if (!InProcessMemory::IsValidPtr<uintptr_t>(actorPtr)) [[unlikely]] {
            return std::nullopt;
        }

        const auto rootOpt = InProcessMemory::Read<uintptr_t>(actorPtr + Offsets::AActor_RootComponent);
        if (!rootOpt || !InProcessMemory::IsValidPtr<uint32_t>(*rootOpt)) {
            return std::nullopt;
        }

        const uintptr_t rootComp = *rootOpt;
        if (outRootComp) {
            *outRootComp = rootComp;
        }

        const auto locOpt = InProcessMemory::Read<FVector3f>(rootComp + Offsets::USceneComp_RelativeLocation);
        if (!locOpt || !locOpt->IsFinite()) [[unlikely]] {
            return std::nullopt;
        }

        return *locOpt;
    }

    [[nodiscard]] size_t CollectTelemetry(
        const FWorldLevelContext& worldCtx,
        const GNamePoolResolver& namePool,
        const FVector3f& observerOrigin,
        float maxDistanceUnits = 15000.0f) noexcept
    {
        count_ = 0;
        if (worldCtx.ActorPointers.empty() || maxDistanceUnits <= 0.0f) {
            return 0;
        }

        const float maxDistSq = maxDistanceUnits * maxDistanceUnits;

        for (const uintptr_t actorPtr : worldCtx.ActorPointers) {
            if (!InProcessMemory::IsValidPtr<uintptr_t>(actorPtr)) {
                continue;
            }

            const auto fnameOpt = InProcessMemory::Read<FName>(actorPtr + Offsets::UObject_NamePrivate);
            if (!fnameOpt) {
                continue;
            }

            const uint32_t nameIdx = (fnameOpt->DisplayIndex != 0U)
                ? fnameOpt->DisplayIndex
                : fnameOpt->ComparisonIndex;
            const auto nameViewOpt = namePool.ResolveView(nameIdx);
            if (!nameViewOpt) {
                continue;
            }

            const EGameplayActorType category = ActorNameClassifier::Classify(*nameViewOpt);
            if (category == EGameplayActorType::Unknown) {
                continue;
            }

            uintptr_t rootCompPtr = 0;
            const auto locOpt = ReadActorWorldLocation(actorPtr, &rootCompPtr);
            if (!locOpt) {
                continue;
            }

            const FVector3f delta = *locOpt - observerOrigin;
            const float distSq = delta.SizeSquared();
            if (distSq > maxDistSq) {
                continue;
            }

            if (count_ >= MaxTrackedEntities) [[unlikely]] {
                break;
            }

            samples_[count_++] = FEntityTelemetrySample{
                .ActorPtr         = actorPtr,
                .RootComponentPtr = rootCompPtr,
                .ComparisonIndex  = fnameOpt->ComparisonIndex,
                .Category         = category,
                .WorldLocation    = *locOpt,
                .DistanceMeters   = std::sqrt(distSq) * kUeUnitsToMeters
            };
        }

        return count_;
    }

    [[nodiscard]] std::span<const FEntityTelemetrySample> ViewSamples() const noexcept {
        return std::span<const FEntityTelemetrySample>(samples_.data(), count_);
    }

private:
    std::array<FEntityTelemetrySample, MaxTrackedEntities> samples_{};
    size_t count_{0};
};

// ============================================================================
// STEP 4: AJUSTE CINEMÁTICO LOCAL E SIMULADOR DE PROGRESSO DE TAREFAS
// ============================================================================

enum class KinematicStatus : uint8_t {
    Ok = 0,
    InvalidActorPtr,
    NullMovementComponent,
    NonFiniteParameter,
    OutOfSafetyBounds,
    WriteFailed
};

struct alignas(32) FKinematicSnapshot {
    uintptr_t CharacterPtr{0};
    uintptr_t MovementComponentPtr{0};
    float     OriginalMaxWalkSpeed{0.0f};
    float     OriginalMaxWalkSpeedCrouched{0.0f};
    float     OriginalMaxAcceleration{0.0f};
    float     OriginalCustomTimeDilation{1.0f};
    bool      bCaptured{false};
};

struct FKinematicOverrideConfig {
    float SpeedMultiplier{1.0f};
    float AccelerationMultiplier{1.0f};
    float VelocityScale{1.0f};
    float MaxAllowedSpeedCeiling{1200.0f};
};

class CharacterKinematicController {
public:
    static constexpr float kMinMultiplier = 0.10f;
    static constexpr float kMaxMultiplier = 3.00f;
    static constexpr float kMaxHardSpeed  = 2400.0f;

    [[nodiscard]] KinematicStatus CaptureBaseline(uintptr_t characterPtr) noexcept {
        if (!InProcessMemory::IsValidPtr<uintptr_t>(characterPtr)) [[unlikely]] {
            return KinematicStatus::InvalidActorPtr;
        }

        const auto moveCompOpt = InProcessMemory::Read<uintptr_t>(
            characterPtr + Offsets::ACharacter_CharacterMovement
        );
        if (!moveCompOpt || !InProcessMemory::IsValidPtr<uintptr_t>(*moveCompOpt)) {
            return KinematicStatus::NullMovementComponent;
        }
        const uintptr_t moveComp = *moveCompOpt;

        const auto walkSpeedOpt   = InProcessMemory::Read<float>(moveComp + Offsets::UCharMove_MaxWalkSpeed);
        const auto crouchSpeedOpt = InProcessMemory::Read<float>(moveComp + Offsets::UCharMove_MaxWalkSpeedCrouched);
        const auto maxAccelOpt    = InProcessMemory::Read<float>(moveComp + Offsets::UCharMove_MaxAcceleration);
        const auto timeDilOpt     = InProcessMemory::Read<float>(characterPtr + Offsets::AActor_CustomTimeDilation);

        if (!walkSpeedOpt || !crouchSpeedOpt || !maxAccelOpt || !timeDilOpt) [[unlikely]] {
            return KinematicStatus::NullMovementComponent;
        }

        if (!std::isfinite(*walkSpeedOpt) || !std::isfinite(*maxAccelOpt) || *walkSpeedOpt <= 0.0f) [[unlikely]] {
            return KinematicStatus::NonFiniteParameter;
        }

        snapshot_ = FKinematicSnapshot{
            .CharacterPtr                 = characterPtr,
            .MovementComponentPtr         = moveComp,
            .OriginalMaxWalkSpeed         = *walkSpeedOpt,
            .OriginalMaxWalkSpeedCrouched = *crouchSpeedOpt,
            .OriginalMaxAcceleration      = *maxAccelOpt,
            .OriginalCustomTimeDilation   = *timeDilOpt,
            .bCaptured                    = true
        };
        return KinematicStatus::Ok;
    }

    [[nodiscard]] KinematicStatus ApplyKinematicTuning(const FKinematicOverrideConfig& cfg) const noexcept {
        if (!snapshot_.bCaptured || !InProcessMemory::IsValidPtr<uintptr_t>(snapshot_.MovementComponentPtr)) [[unlikely]] {
            return KinematicStatus::NullMovementComponent;
        }

        if (!std::isfinite(cfg.SpeedMultiplier) ||
            !std::isfinite(cfg.AccelerationMultiplier) ||
            !std::isfinite(cfg.VelocityScale) ||
            !std::isfinite(cfg.MaxAllowedSpeedCeiling)) [[unlikely]]
        {
            return KinematicStatus::NonFiniteParameter;
        }

        if (cfg.SpeedMultiplier < kMinMultiplier || cfg.SpeedMultiplier > kMaxMultiplier ||
            cfg.MaxAllowedSpeedCeiling <= 0.0f   || cfg.MaxAllowedSpeedCeiling > kMaxHardSpeed) [[unlikely]]
        {
            return KinematicStatus::OutOfSafetyBounds;
        }

        const uintptr_t mc = snapshot_.MovementComponentPtr;

        const float tunedWalkSpeed = std::fmin(
            snapshot_.OriginalMaxWalkSpeed * cfg.SpeedMultiplier,
            cfg.MaxAllowedSpeedCeiling
        );
        const float tunedCrouchSpeed = std::fmin(
            snapshot_.OriginalMaxWalkSpeedCrouched * cfg.SpeedMultiplier,
            cfg.MaxAllowedSpeedCeiling
        );
        const float tunedAccel = snapshot_.OriginalMaxAcceleration * cfg.AccelerationMultiplier;

        if (!InProcessMemory::Write<float>(mc + Offsets::UCharMove_MaxWalkSpeed, tunedWalkSpeed) ||
            !InProcessMemory::Write<float>(mc + Offsets::UCharMove_MaxWalkSpeedCrouched, tunedCrouchSpeed) ||
            !InProcessMemory::Write<float>(mc + Offsets::UCharMove_MaxAcceleration, tunedAccel)) [[unlikely]]
        {
            return KinematicStatus::WriteFailed;
        }

        if (cfg.VelocityScale != 1.0f) {
            const auto velOpt = InProcessMemory::Read<FVector3f>(mc + Offsets::UMovementComp_Velocity);
            if (velOpt && velOpt->IsFinite()) {
                FVector3f scaledVel{
                    velOpt->x * cfg.VelocityScale,
                    velOpt->y * cfg.VelocityScale,
                    velOpt->z
                };

                const float horizSpeedSq = (scaledVel.x * scaledVel.x) + (scaledVel.y * scaledVel.y);
                const float maxCeilingSq = cfg.MaxAllowedSpeedCeiling * cfg.MaxAllowedSpeedCeiling;
                if (horizSpeedSq > maxCeilingSq && horizSpeedSq > 0.0f) {
                    const float invMag = cfg.MaxAllowedSpeedCeiling / std::sqrt(horizSpeedSq);
                    scaledVel.x *= invMag;
                    scaledVel.y *= invMag;
                }

                if (!InProcessMemory::Write<FVector3f>(mc + Offsets::UMovementComp_Velocity, scaledVel)) [[unlikely]] {
                    return KinematicStatus::WriteFailed;
                }
            }
        }

        return KinematicStatus::Ok;
    }

    [[nodiscard]] KinematicStatus RestoreBaseline() const noexcept {
        if (!snapshot_.bCaptured || !InProcessMemory::IsValidPtr<uintptr_t>(snapshot_.MovementComponentPtr)) {
            return KinematicStatus::NullMovementComponent;
        }

        const uintptr_t mc = snapshot_.MovementComponentPtr;
        const bool ok =
            InProcessMemory::Write<float>(mc + Offsets::UCharMove_MaxWalkSpeed, snapshot_.OriginalMaxWalkSpeed) &&
            InProcessMemory::Write<float>(mc + Offsets::UCharMove_MaxWalkSpeedCrouched, snapshot_.OriginalMaxWalkSpeedCrouched) &&
            InProcessMemory::Write<float>(mc + Offsets::UCharMove_MaxAcceleration, snapshot_.OriginalMaxAcceleration) &&
            InProcessMemory::Write<float>(snapshot_.CharacterPtr + Offsets::AActor_CustomTimeDilation, snapshot_.OriginalCustomTimeDilation);

        return ok ? KinematicStatus::Ok : KinematicStatus::WriteFailed;
    }

    [[nodiscard]] constexpr const FKinematicSnapshot& Snapshot() const noexcept {
        return snapshot_;
    }

private:
    FKinematicSnapshot snapshot_{};
};

enum class ETaskActionType : uint8_t {
    GeneratorRepair = 0,
    HealOther       = 1,
    TotemCleanse    = 2,
    ExitGateOpen    = 3
};

struct alignas(32) FChargeableTaskState {
    uintptr_t       TargetActorPtr{0};
    uint32_t        TaskId{0};
    ETaskActionType ActionType{ETaskActionType::GeneratorRepair};
    float           CurrentCharges{0.0f};
    float           MaxChargesRequired{90.0f};
    float           BaseRatePerSecond{1.0f};
    float           RateMultiplier{1.0f};
    bool            bActive{false};
    bool            bCompleted{false};

    [[nodiscard]] constexpr float NormalizedProgress() const noexcept {
        if (MaxChargesRequired <= 0.0f) [[unlikely]] {
            return 0.0f;
        }
        const float ratio = CurrentCharges / MaxChargesRequired;
        return ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    }
};

struct alignas(32) FTaskProgressTelemetryEvent {
    uint64_t        SequenceNumber{0};
    uintptr_t       TargetActorPtr{0};
    uint32_t        TaskId{0};
    ETaskActionType ActionType{ETaskActionType::GeneratorRepair};
    float           CurrentCharges{0.0f};
    float           NormalizedPercent{0.0f};
    float           DeltaApplied{0.0f};
    float           TimestampSeconds{0.0f};
};

template <size_t MaxActiveTasks = 16, size_t RingBufferCapacity = 256>
class alignas(64) ChargeableTaskSimulator {
public:
    static_assert((RingBufferCapacity & (RingBufferCapacity - 1)) == 0,
                  "RingBufferCapacity must be a power of two");

    static constexpr float kMaxValidDeltaTime = 0.25f;

    [[nodiscard]] bool RegisterTask(const FChargeableTaskState& initialState) noexcept {
        if (!std::isfinite(initialState.MaxChargesRequired) || initialState.MaxChargesRequired <= 0.0f) [[unlikely]] {
            return false;
        }

        for (size_t i = 0; i < task_count_; ++i) {
            if (tasks_[i].TaskId == initialState.TaskId && tasks_[i].TargetActorPtr == initialState.TargetActorPtr) {
                tasks_[i] = initialState;
                return true;
            }
        }

        if (task_count_ >= MaxActiveTasks) [[unlikely]] {
            return false;
        }
        tasks_[task_count_++] = initialState;
        return true;
    }

    [[nodiscard]] size_t TickAllTasks(
        float deltaSeconds,
        float worldTimeSeconds,
        bool syncToTargetMemory = false) noexcept
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f || deltaSeconds > kMaxValidDeltaTime) [[unlikely]] {
            return 0;
        }

        size_t updatedCount = 0;

        for (size_t i = 0; i < task_count_; ++i) {
            FChargeableTaskState& task = tasks_[i];
            if (!task.bActive || task.bCompleted) {
                continue;
            }

            const float effectiveRate = task.BaseRatePerSecond * task.RateMultiplier;
            if (!std::isfinite(effectiveRate) || effectiveRate <= 0.0f) [[unlikely]] {
                continue;
            }

            const float chargeDelta = effectiveRate * deltaSeconds;
            task.CurrentCharges += chargeDelta;

            if (task.CurrentCharges >= task.MaxChargesRequired) {
                task.CurrentCharges = task.MaxChargesRequired;
                task.bCompleted     = true;
                task.bActive        = false;
            }

            if (syncToTargetMemory && InProcessMemory::IsValidPtr<float>(task.TargetActorPtr)) {
                (void)InProcessMemory::Write<float>(
                    task.TargetActorPtr + Offsets::UChargeable_CurrentCharge,
                    task.CurrentCharges
                );
            }

            PushRingEvent(FTaskProgressTelemetryEvent{
                .SequenceNumber    = ++sequence_id_,
                .TargetActorPtr    = task.TargetActorPtr,
                .TaskId            = task.TaskId,
                .ActionType        = task.ActionType,
                .CurrentCharges    = task.CurrentCharges,
                .NormalizedPercent = task.NormalizedProgress() * 100.0f,
                .DeltaApplied      = chargeDelta,
                .TimestampSeconds  = worldTimeSeconds
            });

            ++updatedCount;
        }

        return updatedCount;
    }

    [[nodiscard]] std::optional<FTaskProgressTelemetryEvent> LatestEvent() const noexcept {
        if (write_cursor_ == 0) {
            return std::nullopt;
        }
        const size_t idx = (write_cursor_ - 1U) & (RingBufferCapacity - 1U);
        return ring_[idx];
    }

    [[nodiscard]] std::span<const FChargeableTaskState> ViewTasks() const noexcept {
        return std::span<const FChargeableTaskState>(tasks_.data(), task_count_);
    }

    [[nodiscard]] constexpr size_t TotalEventsRecorded() const noexcept {
        return write_cursor_;
    }

private:
    void PushRingEvent(const FTaskProgressTelemetryEvent& ev) noexcept {
        const size_t slot = write_cursor_ & (RingBufferCapacity - 1U);
        ring_[slot] = ev;
        ++write_cursor_;
    }

    std::array<FChargeableTaskState, MaxActiveTasks> tasks_{};
    size_t task_count_{0};

    std::array<FTaskProgressTelemetryEvent, RingBufferCapacity> ring_{};
    size_t write_cursor_{0};
    uint64_t sequence_id_{0};
};

// ============================================================================
// STEP 4B: RESOLUÇÃO DE APLAYERSTATE E OVERRIDE SEGURO DE PLAYERNAMEPRIVATE
// ============================================================================

// Layout binário do FString (TArray<char16_t>) na Unreal Engine 4.27 (64-bit)
struct alignas(8) FStringViewLayout {
    uintptr_t DataPtr{0}; // char16_t* (UTF-16LE buffer)
    int32_t   ArrayNum{0}; // Inclui o null-terminator (u'\0')
    int32_t   ArrayMax{0}; // Capacidade alocada em elementos char16_t
};
static_assert(sizeof(FStringViewLayout) == 16, "UE 4.27 64-bit FString must be 16 bytes");

enum class NameOverrideStatus : uint8_t {
    Ok = 0,
    InvalidWorldOrPawn,
    PlayerStateNotFound,
    EmptyOrInvalidTargetString,
    CapacityTooSmall,
    MemoryWriteFailed
};

class LocalPlayerNameController {
public:
    static constexpr size_t kMaxBackupChars = 64;

    // Resolve o APlayerState* local via UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> PlayerState
    // ou diretamente via APawn::PlayerState (+0x0248) caso o ponteiro do Pawn local seja fornecido.
    [[nodiscard]] static std::optional<uintptr_t> ResolveLocalPlayerState(
        uintptr_t uWorldPtr,
        uintptr_t fallbackLocalPawnPtr = 0) noexcept
    {
        if (InProcessMemory::IsValidPtr<uintptr_t>(uWorldPtr)) {
            const auto gameInstOpt = InProcessMemory::Read<uintptr_t>(uWorldPtr + Offsets::UWorld_OwningGameInstance);
            if (gameInstOpt && InProcessMemory::IsValidPtr<uintptr_t>(*gameInstOpt)) {
                const uintptr_t gameInst = *gameInstOpt;
                const auto localPlayersDataOpt = InProcessMemory::Read<uintptr_t>(gameInst + Offsets::UGameInstance_LocalPlayers);
                const auto localPlayersNumOpt  = InProcessMemory::Read<int32_t>(gameInst + Offsets::UGameInstance_LocalPlayersNum);

                if (localPlayersDataOpt && localPlayersNumOpt && *localPlayersNumOpt > 0 &&
                    InProcessMemory::IsValidPtr<uintptr_t>(*localPlayersDataOpt))
                {
                    const auto localPlayer0Opt = InProcessMemory::Read<uintptr_t>(*localPlayersDataOpt);
                    if (localPlayer0Opt && InProcessMemory::IsValidPtr<uintptr_t>(*localPlayer0Opt)) {
                        const auto playerCtrlOpt = InProcessMemory::Read<uintptr_t>(
                            *localPlayer0Opt + Offsets::UPlayer_PlayerController
                        );
                        if (playerCtrlOpt && InProcessMemory::IsValidPtr<uintptr_t>(*playerCtrlOpt)) {
                            const auto playerStateOpt = InProcessMemory::Read<uintptr_t>(
                                *playerCtrlOpt + Offsets::AController_PlayerState
                            );
                            if (playerStateOpt && InProcessMemory::IsValidPtr<uintptr_t>(*playerStateOpt)) {
                                return *playerStateOpt;
                            }
                        }
                    }
                }
            }
        }

        // Fallback direto via APawn::PlayerState (+0x0248)
        if (InProcessMemory::IsValidPtr<uintptr_t>(fallbackLocalPawnPtr)) {
            const auto pawnStateOpt = InProcessMemory::Read<uintptr_t>(
                fallbackLocalPawnPtr + Offsets::APawn_PlayerState
            );
            if (pawnStateOpt && InProcessMemory::IsValidPtr<uintptr_t>(*pawnStateOpt)) {
                return *pawnStateOpt;
            }
        }

        return std::nullopt;
    }

    // Sobrescreve in-place o buffer UTF-16 de APlayerState::PlayerNamePrivate (+0x0310)
    // respeitando estritamente o limite ArrayMax já alocado pelo motor (Zero-Heap-Corruption).
    [[nodiscard]] NameOverrideStatus ApplyDisplayNameOverride(
        uintptr_t playerStatePtr,
        std::string_view newAsciiName) noexcept
    {
        if (!InProcessMemory::IsValidPtr<uintptr_t>(playerStatePtr)) [[unlikely]] {
            return NameOverrideStatus::PlayerStateNotFound;
        }
        if (newAsciiName.empty()) [[unlikely]] {
            return NameOverrideStatus::EmptyOrInvalidTargetString;
        }

        const uintptr_t fstringAddr = playerStatePtr + Offsets::APlayerState_PlayerNamePrivate;
        const auto fstrOpt = InProcessMemory::Read<FStringViewLayout>(fstringAddr);
        if (!fstrOpt) [[unlikely]] {
            return NameOverrideStatus::PlayerStateNotFound;
        }

        const FStringViewLayout currentStr = *fstrOpt;
        if (!InProcessMemory::IsValidPtr<char16_t>(currentStr.DataPtr) ||
            currentStr.ArrayMax <= 1 ||
            currentStr.ArrayNum < 0 ||
            currentStr.ArrayNum > currentStr.ArrayMax) [[unlikely]]
        {
            return NameOverrideStatus::CapacityTooSmall;
        }

        // Faz backup do nome original na primeira modificação deste APlayerState
        if (!b_backup_valid_ || last_player_state_ptr_ != playerStatePtr) {
            CaptureOriginalNameBackup(playerStatePtr, currentStr);
        }

        // Limita o comprimento ao espaço existente em ArrayMax (reservando 1 slot para u'\0')
        const size_t maxWritableChars = static_cast<size_t>(currentStr.ArrayMax - 1);
        const size_t writeCount = newAsciiName.size() < maxWritableChars
                                      ? newAsciiName.size()
                                      : maxWritableChars;

        if (writeCount == 0) [[unlikely]] {
            return NameOverrideStatus::CapacityTooSmall;
        }

        // Converte ASCII/Latin-1 para UTF-16LE em buffer de pilha alinhado
        std::array<char16_t, kMaxBackupChars> utf16Scratch{};
        const size_t clampedCount = writeCount < (kMaxBackupChars - 1U)
                                        ? writeCount
                                        : (kMaxBackupChars - 1U);

        for (size_t i = 0; i < clampedCount; ++i) {
            utf16Scratch[i] = static_cast<char16_t>(static_cast<uint8_t>(newAsciiName[i]));
        }
        utf16Scratch[clampedCount] = u'\0';

        // Valida todo o intervalo de bytes do buffer UTF-16 antes da escrita
        const size_t totalUnitsWithNull = clampedCount + 1U;
        if (!InProcessMemory::ViewArray<char16_t>(currentStr.DataPtr, totalUnitsWithNull)) [[unlikely]] {
            return NameOverrideStatus::MemoryWriteFailed;
        }

        std::memcpy(
            reinterpret_cast<void*>(currentStr.DataPtr),
            utf16Scratch.data(),
            totalUnitsWithNull * sizeof(char16_t)
        );

        // Atualiza FString::ArrayNum (+0x08 dentro do FString) mantendo ArrayMax intacto
        const int32_t newArrayNum = static_cast<int32_t>(totalUnitsWithNull);
        if (!InProcessMemory::Write<int32_t>(fstringAddr + offsetof(FStringViewLayout, ArrayNum), newArrayNum)) [[unlikely]] {
            return NameOverrideStatus::MemoryWriteFailed;
        }

        b_applied_for_state_ = true;
        return NameOverrideStatus::Ok;
    }

    // Restaura o PlayerNamePrivate original caso um backup válido tenha sido capturado
    [[nodiscard]] bool RestoreOriginalDisplayName() noexcept {
        if (!b_backup_valid_ || !InProcessMemory::IsValidPtr<uintptr_t>(last_player_state_ptr_)) {
            return false;
        }

        const uintptr_t fstringAddr = last_player_state_ptr_ + Offsets::APlayerState_PlayerNamePrivate;
        const auto fstrOpt = InProcessMemory::Read<FStringViewLayout>(fstringAddr);
        if (!fstrOpt || fstrOpt->DataPtr != backup_data_ptr_ || fstrOpt->ArrayMax < backup_array_num_) {
            return false;
        }

        if (backup_array_num_ > 0) {
            std::memcpy(
                reinterpret_cast<void*>(backup_data_ptr_),
                backup_utf16_.data(),
                static_cast<size_t>(backup_array_num_) * sizeof(char16_t)
            );
        }

        const bool ok = InProcessMemory::Write<int32_t>(
            fstringAddr + offsetof(FStringViewLayout, ArrayNum),
            backup_array_num_
        );
        b_applied_for_state_ = false;
        return ok;
    }

    void ResetState() noexcept {
        last_player_state_ptr_ = 0;
        backup_data_ptr_       = 0;
        backup_array_num_      = 0;
        b_backup_valid_        = false;
        b_applied_for_state_   = false;
    }

    [[nodiscard]] constexpr bool IsAppliedForCurrentState(uintptr_t playerStatePtr) const noexcept {
        return b_applied_for_state_ && (last_player_state_ptr_ == playerStatePtr);
    }

private:
    void CaptureOriginalNameBackup(uintptr_t playerStatePtr, const FStringViewLayout& str) noexcept {
        last_player_state_ptr_ = playerStatePtr;
        backup_data_ptr_       = str.DataPtr;
        b_applied_for_state_   = false;

        const int32_t clampedNum = (str.ArrayNum > 0 && static_cast<size_t>(str.ArrayNum) <= kMaxBackupChars)
                                       ? str.ArrayNum
                                       : 0;
        if (clampedNum > 0) {
            if (const auto spanOpt = InProcessMemory::ViewArray<char16_t>(str.DataPtr, static_cast<size_t>(clampedNum))) {
                std::memcpy(backup_utf16_.data(), spanOpt->data(), static_cast<size_t>(clampedNum) * sizeof(char16_t));
                backup_array_num_ = clampedNum;
                b_backup_valid_   = true;
                return;
            }
        }
        backup_array_num_ = 0;
        b_backup_valid_   = false;
    }

    uintptr_t last_player_state_ptr_{0};
    uintptr_t backup_data_ptr_{0};
    int32_t   backup_array_num_{0};
    bool      b_backup_valid_{false};
    bool      b_applied_for_state_{false};
    std::array<char16_t, kMaxBackupChars> backup_utf16_{};
};

} // namespace ue4::aarch64
