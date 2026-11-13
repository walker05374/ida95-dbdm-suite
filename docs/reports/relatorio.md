# Relatório Unificado: Arquitetura de Instrumentação e Diagnóstico Unreal Engine 4.27 (ELF64 AArch64 / C++20)

Este documento consolida todos os subsistemas modulares desenvolvidos em **C++20** (`-std=c++20 -O3 -fno-exceptions -fno-rtti`) para análise, telemetria e instrumentação *in-process* em arquitetura **ELF64 AArch64 (ARM64)** sobre a **Unreal Engine 4.27** (`libUE4.so`).

---

## Sumário Arquitetural dos Subsistemas

| Etapa / Módulo | Cabeçalho Lógico | Offsets / Âncoras | Responsabilidade Técnica | Complexidade |
| :--- | :--- | :--- | :--- | :--- |
| **Step 1: Inicialização de Mundo e Nível** | `ue4_aarch64_step1.hpp` | `GWorld` (`0x10D69998`), `PersistentLevel` (`+0x0038`), `ActorsData` (`+0x00A0`), `ActorsCount` (`+0x00A8`), `ActorsMax` (`+0x00AC`) | Leitura direta *in-process* compatível com AArch64 TBI/MTE, descoberta de base via `dl_iterate_phdr` e validação de `TArray<AActor*>`. | $O(1)$ |
| **Step 2: Resolução de Nomes (`GNamePool`)** | `ue4_aarch64_step2.hpp` | `GNamePool` (`0x10B7C100`), `bNamePoolInitialized` (`0x10BCF0C0`), `CurrentBlock` (`+0x0038`), `Blocks` (`+0x0040`), `UObject::NamePrivate` (`+0x0018`, `DisplayIndex` em `+0x001C`) | Decodificação de `FName` (`WITH_CASE_PRESERVING_NAME = 1`) via indexação em blocos (`DisplayIndex >> 16`) e *stride* de 4 bytes (`(DisplayIndex & 0xFFFF) * 4`), `Header` em `+0x04` (`Len = Header >> 1`) e string em `+0x06`. | $O(1)$ |
| **Step 3: Classificação de Entidades e Telemetria Espacial** | `ue4_aarch64_step3.hpp` | `RootComponent` (`+0x0140`), `RelativeLocation` (`+0x0134`) | Filtragem de atores por categoria (`Survivor`/`Camper`, `Killer`/`Slasher`, `Totem`, `Hatch`, `Generator`, `Pallet`) e extração de coordenadas 3D (`FVector3f`). | $O(N)$ |
| **Step 4: Ajuste Cinemático e Simulador de Tarefas Carregáveis** | `ue4_aarch64_step4.hpp` | `CharacterMovement` (`+0x0290`), `Velocity` (`+0x0140`), `MaxWalkSpeed` (`+0x018C`), `MaxAcceleration` (`+0x0198`) | Captura de *snapshot*, modificação segura com limites (*clamping*) de velocidade/aceleração e simulador de progresso de tarefas em *ring buffer* de capacidade fixa. | $O(1)$ |
| **Step 4B: Override de Nome Local (`APlayerState`)** | `ue4_playerstate_name_override.hpp` | `OwningGameInstance` (`+0x0410`), `LocalPlayers` (`+0x0040`), `PlayerController` (`+0x0038`), `AcknowledgedPawn` (`+0x02B8`), `PlayerState` (`+0x0238`/`+0x0248`), `PlayerNamePrivate` (`+0x0310`) | Resolução do `APlayerState` local, backup do buffer UTF-16LE original e sobrescrita *in-place* limitada por `FString::ArrayMax - 1`. | $O(1)$ |
| **Step 5: Orquestração de Ciclo de Vida e Thread/Hook de Execução** | `ue4_runtime_pipeline.hpp` | `dl_iterate_phdr`, `pthread_create`, Double-Buffer Lock-Free, `VTable` Tick Hook opcional | Resolução assíncrona de `libUE4.so`, detecção de transição de nível (`GWorld`/`ULevel`), publicação *lock-free* de *snapshots* e staging *double-buffered* do `PlayerNamePrivate`. | $O(1)$ sync |
| **Step 6: Entrypoint Nativo e Build Android NDK** | `lib_main.cpp` / `CMakeLists.txt` | `.init_array`, `.fini_array`, `JNI_OnLoad`, `UE4Diag_SetCustomPlayerName` | Inicialização automática da biblioteca compartilhada (`.so`), exportações C-ABI *thread-safe* e configuração CMake para `arm64-v8a`. | $O(1)$ |
| **Step 7: Integração `entry.cpp` (`libinstrument_core.so`) e Verificação `llvm-nm`** | `entry.cpp` / `exports.map` | `runtime::elf::find_module`, `InProcessMemory::safe_read`, `SYS_process_vm_writev`, `JNI_OnLoad` / `JNI_OnUnload` | Implementação enxuta com thread `joinable`, leitura/escrita via `process_vm_readv`/`process_vm_writev` e verificação de tabela dinâmica ELF (`llvm-nm`/`llvm-readelf`). | $O(N)$ tick |

---

## Código Unificado Completo (`ue4_aarch64_unified_diagnostics.hpp`)

O código abaixo reúne os **4 módulos** em uma única unidade de cabeçalho C++20 pronta para compilação sem alocação dinâmica (*zero-allocation*), sem exceções (`-fno-exceptions`) e sem RTTI (`-fno-rtti`).

```cpp
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

struct alignas(32) FEntityTelemetrySample {
    uintptr_t          ActorPtr{0};
    uintptr_t          RootComponentPtr{0};
    uint32_t           ComparisonIndex{0};
    EGameplayActorType Category{EGameplayActorType::Unknown};
    FVector3f          WorldLocation{};
    float              DistanceMeters{0.0f};
};
static_assert(sizeof(FEntityTelemetrySample) == 32, "FEntityTelemetrySample must be 32-byte aligned");

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
```

---

## Exemplo de Orquestração dos 4 Passos (`main_pipeline.cpp`)

```cpp
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
```

---

## Step 5: Orquestração de Ciclo de Vida, Double-Buffer Lock-Free e Pipeline de Execução (`ue4_runtime_pipeline.hpp`)

```cpp
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
```

---

## Step 6: Entrypoint Nativo (`lib_main.cpp`) e Blueprint de Build Android NDK (`CMakeLists.txt`)

### 1. Shared Library Entrypoint Wrapper (`lib_main.cpp`)

```cpp
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
#define DIAG_LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO,  "UE4Diag", fmt, ##__VA_ARGS__)
#define DIAG_LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, "UE4Diag", fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define DIAG_LOGI(fmt, ...) std::printf("[UE4Diag][INFO] " fmt "\n", ##__VA_ARGS__)
#define DIAG_LOGE(fmt, ...) std::printf("[UE4Diag][ERR]  " fmt "\n", ##__VA_ARGS__)
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
```

### 2. Configuração CMake para Android NDK (`CMakeLists.txt`)

```cmake
cmake_minimum_required(VERSION 3.22.1)

project(ue4_diagnostics_suite
    VERSION 1.0.0
    LANGUAGES CXX
)

# Garante compilação exclusiva para ELF64 AArch64 quando sob Android NDK
if(ANDROID AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This target requires ANDROID_ABI=arm64-v8a (ELF64 AArch64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(ue4_diagnostics SHARED
    lib_main.cpp
)

target_include_directories(ue4_diagnostics
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Flags estritas de otimização, remoção de RTTI/Exceptions e ocultação de símbolos internos
target_compile_options(ue4_diagnostics
    PRIVATE
        -O3
        -fno-exceptions
        -fno-rtti
        -fPIC
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
        -fno-stack-protector
        -fomit-frame-pointer
        -Wall
        -Wextra
        -Wpedantic
        $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>:-march=armv8-a+simd>
)

# Otimização de linkagem (Garbage Collection de seções não utilizadas, LTO e RELRO)
target_link_options(ue4_diagnostics
    PRIVATE
        -Wl,--gc-sections
        -Wl,--exclude-libs,ALL
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,--hash-style=gnu
        -flto=thin
)

# Bibliotecas de sistema (Android Log + dl + pthread)
if(ANDROID)
    find_library(ANDROID_LOG_LIB log REQUIRED)
    target_link_libraries(ue4_diagnostics
        PRIVATE
            ${ANDROID_LOG_LIB}
            dl
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(ue4_diagnostics
        PRIVATE
            Threads::Threads
            ${CMAKE_DL_LIBS}
    )
endif()
```

---

## Instruções de Compilação (Android NDK / Linux AArch64)

* **CMake + Android NDK (Cross-Compile `arm64-v8a`):**
  ```bash
  cmake -B build-android \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build build-android --config Release
  ```
* **Clang++ Direto (Android NDK / Cross-Compile AArch64):**
  ```bash
  aarch64-linux-android29-clang++ -std=c++20 -O3 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -fPIC -shared lib_main.cpp -llog -ldl -o libue4_diagnostics.so
  ```

---

## Step 7: Integração `entry.cpp` (`libinstrument_core.so`), Verificação `llvm-nm` e Reconciliação com a Rev. 7

### 1. Reconciliação de Offsets e Layout (`entry.cpp` vs `RELATORIO_REV7_COMPLETO.md` — Grau A)

| Campo / Parâmetro | Valor no `entry.cpp` (Template Base) | Valor Real Provado (Grau A — Rev. 7 `libUE4.so` Build `292949`) | Observação Técnica |
| :--- | :--- | :--- | :--- |
| `gworld_rva` (`GWorld`) | `0x08A4F120` | **`0x10D69998`** (GOT `0x0F668440`) | Provado em `0x04BC9678`–`0x04BC9680`. |
| `gnamepool_rva` (`GNamePool`) | `0x088C2400` | **`0x10B7C100`** (`bNamePoolInitialized` = `0x10BCF0C0`) | Provado em `FName::ToString` (`0x0A59F82C`). |
| `FNamePool` (`kBlocksOffset`, `kStride`, `Header`) | `kBlocksOffset = 0x0010`<br>`kStride = 2`<br>`len = *header >> 6`<br>`str = entry + 2` | **`kBlocksOffset = 0x0040`** (`CurrentBlock = +0x38`)<br>**`kStride = 4`** (`WITH_CASE_PRESERVING_NAME = 1`)<br>**`DisplayIndex` em `Obj + 0x1C`**<br>**`Header` em `entry + 0x04`** (`len = *header >> 1`)<br>**`AnsiName` em `entry + 0x06`** | O binário alvo usa `WITH_CASE_PRESERVING_NAME = 1` (`FName` de 12 B e `FNameEntry` com `uint32 ComparisonId` antes do `Header`). |
| `UWorld::PersistentLevel` | `0x0030` | **`0x0038`** | `0x0C78A6C0`: `LDR X0, [X0, #0x38]; RET`. |
| `UWorld::OwningGameInstance` | `0x0180` | **`0x0410`** | `0x0C1BFDFC`: `LDR X0, [X0, #0x410]`. |
| `ULevel::Actors` (`actor_array`) | `0x0098` | **`0x00A0`** (`Count = +0xA8`, `Max = +0xAC`) | `0x0C78A624`: `LDR X8, [X21, #0xA0]`. |
| `UGameInstance::LocalPlayers` | `0x0038` | **`0x0040`** | `FClassParams` de `UGameInstance` (`0x0C85FE90`). |
| `UPlayer::PlayerController` | `0x0030` | **`0x0038`** | `FClassParams` de `UPlayer` (`0x0C99FE40`). |
| `APlayerController::AcknowledgedPawn` | `0x02A8` | **`0x02B8`** | `FClassParams` de `APlayerController` (`0x0F3B84D8`). |
| `AActor::RootComponent` | `0x0130` | **`0x0140`** | `FClassParams` de `AActor` (`0x0C7ABD84`). |
| `USceneComponent::RelativeLocation` | `0x011C` | **`0x0134`** | `RelativeRotation = +0x0140`, `RelativeScale3D = +0x014C`. |

### 2. Implementação Completa de `entry.cpp` (Sincronizada com Offsets Grau A da Rev. 7)

```cpp
// entry.cpp
#include "visibility.hpp"
#include "elf_resolver.hpp"
#include "in_process_memory.hpp"
#include <jni.h>
#include <pthread.h>
#include <time.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace runtime {
using memory::InProcessMemory;
using memory::Status;

// ============================================================================
// 1. Unreal Engine 4.27 Layout Primitives & Offsets Configuration (Rev. 7)
// ============================================================================
namespace ue4 {
struct INSTRUMENT_LOCAL FVector {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    [[nodiscard]] constexpr float dist_squared(const FVector& other) const noexcept {
        const float dx = x - other.x;
        const float dy = y - other.y;
        const float dz = z - other.z;
        return (dx * dx) + (dy * dy) + (dz * dz);
    }
};

struct INSTRUMENT_LOCAL TArrayHeader {
    uintptr_t data_ptr{0};
    int32_t   count{0};
    int32_t   max{0};
};

struct INSTRUMENT_LOCAL EngineOffsets {
    uintptr_t gworld_rva                = 0x10D69998; // Rev. 7 Grau A (Template base: 0x08A4F120)
    uintptr_t gnamepool_rva             = 0x10B7C100; // Rev. 7 Grau A (Template base: 0x088C2400)
    // UWorld
    uintptr_t persistent_level          = 0x0038;     // Rev. 7 Grau A (Template base: 0x0030)
    uintptr_t owning_game_instance      = 0x0410;     // Rev. 7 Grau A (Template base: 0x0180)
    // ULevel
    uintptr_t actor_array               = 0x00A0;     // Rev. 7 Grau A (Template base: 0x0098)
    // UGameInstance -> LocalPlayers
    uintptr_t local_players_array       = 0x0040;     // Rev. 7 Grau A (Template base: 0x0038)
    uintptr_t player_controller         = 0x0038;     // Rev. 7 Grau A (Template base: 0x0030)
    uintptr_t acknowledged_pawn         = 0x02B8;     // Rev. 7 Grau A (Template base: 0x02A8)
    uintptr_t player_state              = 0x0248;     // Rev. 7 Grau A (Template base: 0x0240)
    // AActor / APawn
    uintptr_t fname_index               = 0x001C;     // DisplayIndex +0x1C (ComparisonIndex em +0x18)
    uintptr_t root_component            = 0x0140;     // Rev. 7 Grau A (Template base: 0x0130)
    uintptr_t character_movement        = 0x0290;     // Rev. 7 Grau A (Template base: 0x0288)
    // USceneComponent
    uintptr_t relative_location         = 0x0134;     // Rev. 7 Grau A (Template base: 0x011C)
    uintptr_t component_velocity        = 0x0140;
    // UCharacterMovementComponent
    uintptr_t max_walk_speed            = 0x018C;
    uintptr_t max_acceleration          = 0x0198;
    // APlayerState
    uintptr_t player_name_private       = 0x0310;     // FString (TArray<char16_t>)
};

// ============================================================================
// 2. GNamePool Safe Decoder (UE 4.27 FNamePool Block Architecture - Rev. 7)
// ============================================================================
class INSTRUMENT_LOCAL GNamePoolResolver {
public:
    static constexpr uint32_t  kBlockBitShift     = 16U;
    static constexpr uint32_t  kOffsetMask        = (1U << kBlockBitShift) - 1U;
    static constexpr uint32_t  kMaxBlocks         = 8192U;
    static constexpr size_t    kMaxNameLen        = 128U;
    static constexpr uintptr_t kBlocksOffset      = 0x0040U; // Rev. 7 Grau A (+0x40; Template s/ case-preserving: 0x0010U)
    static constexpr uint32_t  kStride            = 4U;      // Rev. 7 Grau A (Stride 4; Template s/ case-preserving: 2U)
    static constexpr uintptr_t kEntryHeaderOffset = 0x0004U; // +0x00 ComparisonId (4B), +0x04 Header (2B)
    static constexpr uintptr_t kEntryStringOffset = 0x0006U; // +0x06 AnsiName[Len]

    explicit constexpr GNamePoolResolver(uintptr_t pool_base) noexcept
        : pool_base_(pool_base) {}

    [[nodiscard]] bool resolve_name(uint32_t comparison_index, std::span<char> out_buffer) const noexcept {
        if (pool_base_ == 0 || out_buffer.size() < 2U) {
            return false;
        }
        const uint32_t block_idx = comparison_index >> kBlockBitShift;
        const uint32_t byte_off  = (comparison_index & kOffsetMask) * kStride;
        if (block_idx >= kMaxBlocks) {
            return false;
        }
        const uintptr_t block_ptr_addr =
            pool_base_ + kBlocksOffset + (static_cast<uintptr_t>(block_idx) * sizeof(uintptr_t));
        const auto block_base = mem::read<uintptr_t>(block_ptr_addr);
        if (!block_base.has_value() || *block_base == 0) {
            return false;
        }
        const uintptr_t entry_addr = *block_base + byte_off;
        const auto header = mem::read<uint16_t>(entry_addr + kEntryHeaderOffset);
        if (!header.has_value()) {
            return false;
        }
        const bool   is_wide = (*header & 1U) != 0;
        const size_t len     = static_cast<size_t>(*header >> 1U);
        if (is_wide || len == 0 || len >= kMaxNameLen || len >= out_buffer.size()) {
            return false;
        }
        std::span<uint8_t> str_slice{reinterpret_cast<uint8_t*>(out_buffer.data()), len};
        if (InProcessMemory::safe_read(entry_addr + kEntryStringOffset, str_slice) != Status::Ok) {
            return false;
        }
        out_buffer[len] = '\0';
        return true;
    }
private:
    uintptr_t pool_base_{0};
};

// ============================================================================
// 3. Spatial Entity Filter & Kinematic Tuning Module
// ============================================================================
struct INSTRUMENT_LOCAL EntitySnapshot {
    uintptr_t actor_address{0};
    uint32_t  fname_id{0};
    FVector   world_pos{};
    float     distance_meters{0.0f};
};

class INSTRUMENT_LOCAL SpatialAndKinematicPipeline {
public:
    static constexpr size_t kMaxActorsPerTick = 2048U;
    static constexpr size_t kMaxSnapshots     = 64U;

    [[nodiscard]] static std::optional<FVector> read_actor_location(
        uintptr_t actor,
        const EngineOffsets& off
    ) noexcept {
        if (actor == 0) {
            return std::nullopt;
        }
        const auto root_comp = mem::read<uintptr_t>(actor + off.root_component);
        if (!root_comp.has_value() || *root_comp == 0) {
            return std::nullopt;
        }
        return mem::read<FVector>(*root_comp + off.relative_location);
    }

    static Status apply_kinematic_limits(
        uintptr_t local_pawn,
        const EngineOffsets& off,
        float target_walk_speed,
        float target_accel
    ) noexcept {
        if (local_pawn == 0) {
            return Status::InvalidArgument;
        }
        const auto move_comp = mem::read<uintptr_t>(local_pawn + off.character_movement);
        if (!move_comp.has_value() || *move_comp == 0) {
            return Status::UnmappedOrFault;
        }
        if (target_walk_speed > 0.0f && target_walk_speed <= 5000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.max_walk_speed, target_walk_speed);
            if (st != Status::Ok) {
                return st;
            }
        }
        if (target_accel > 0.0f && target_accel <= 10000.0f) {
            const Status st = mem::write_data<float>(*move_comp + off.max_acceleration, target_accel);
            if (st != Status::Ok) {
                return st;
            }
        }
        return Status::Ok;
    }

    static size_t collect_nearby_entities(
        uintptr_t ulevel_addr,
        const FVector& origin,
        float max_radius_meters,
        const EngineOffsets& off,
        const GNamePoolResolver& name_resolver,
        std::span<EntitySnapshot> out_snapshots
    ) noexcept {
        if (ulevel_addr == 0 || out_snapshots.empty()) {
            return 0;
        }
        const auto actor_arr = mem::read<TArrayHeader>(ulevel_addr + off.actor_array);
        if (!actor_arr.has_value() || actor_arr->data_ptr == 0 || actor_arr->count <= 0) {
            return 0;
        }
        const size_t raw_count     = static_cast<size_t>(actor_arr->count);
        const size_t bounded_count = (raw_count > kMaxActorsPerTick) ? kMaxActorsPerTick : raw_count;
        const float max_dist_cm = max_radius_meters * 100.0f;
        const float max_dist_sq = max_dist_cm * max_dist_cm;
        size_t collected = 0;
        std::array<char, 128> name_buf{};
        for (size_t i = 0; i < bounded_count && collected < out_snapshots.size(); ++i) {
            const uintptr_t slot_addr = actor_arr->data_ptr + (i * sizeof(uintptr_t));
            const auto actor_ptr = mem::read<uintptr_t>(slot_addr);
            if (!actor_ptr.has_value() || *actor_ptr == 0) {
                continue;
            }
            const auto fname_idx = mem::read<uint32_t>(*actor_ptr + off.fname_index);
            if (!fname_idx.has_value() || *fname_idx == 0) {
                continue;
            }
            if (!name_resolver.resolve_name(*fname_idx, name_buf)) {
                continue;
            }
            const auto pos = read_actor_location(*actor_ptr, off);
            if (!pos.has_value()) {
                continue;
            }
            const float dist_sq = origin.dist_squared(*pos);
            if (dist_sq <= max_dist_sq) {
                out_snapshots[collected++] = EntitySnapshot{
                    .actor_address   = *actor_ptr,
                    .fname_id        = *fname_idx,
                    .world_pos       = *pos,
                    .distance_meters = std::sqrt(dist_sq) * 0.01f
                };
            }
        }
        return collected;
    }
};

// ============================================================================
// 4. LocalPlayerNameController (Bounded In-Place UTF-16 FString Override)
// ============================================================================
class INSTRUMENT_LOCAL LocalPlayerNameController {
public:
    static Status override_local_name(
        uintptr_t player_state_addr,
        const EngineOffsets& off,
        std::u16string_view new_name
    ) noexcept {
        if (player_state_addr == 0 || new_name.empty()) {
            return Status::InvalidArgument;
        }
        const uintptr_t fstring_addr = player_state_addr + off.player_name_private;
        const auto fstr = mem::read<TArrayHeader>(fstring_addr);
        if (!fstr.has_value() || fstr->data_ptr == 0 || fstr->max <= 1) {
            return Status::UnmappedOrFault;
        }
        uint8_t probe = 0;
        if (InProcessMemory::safe_read(fstr->data_ptr, std::span<uint8_t>{&probe, 1}) != Status::Ok) {
            return Status::UnmappedOrFault;
        }
        const size_t max_chars   = static_cast<size_t>(fstr->max) - 1U;
        const size_t desired_len = (new_name.size() < max_chars) ? new_name.size() : max_chars;
        std::array<char16_t, 64> staging{};
        const size_t clamped_len = (desired_len < (staging.size() - 1U)) ? desired_len : (staging.size() - 1U);
        std::memcpy(staging.data(), new_name.data(), clamped_len * sizeof(char16_t));
        staging[clamped_len] = u'\0';
        const size_t byte_len = (clamped_len + 1U) * sizeof(char16_t);
        struct iovec local_iov {
            .iov_base = staging.data(),
            .iov_len  = byte_len
        };
        struct iovec remote_iov {
            .iov_base = reinterpret_cast<void*>(fstr->data_ptr),
            .iov_len  = byte_len
        };
        const ssize_t written = ::syscall(
            SYS_process_vm_writev,
            ::getpid(),
            &local_iov, 1UL,
            &remote_iov, 1UL,
            0UL
        );
        if (written < 0 || static_cast<size_t>(written) != byte_len) {
            return Status::PartialTransfer;
        }
        const int32_t new_count = static_cast<int32_t>(clamped_len + 1U);
        return mem::write_data<int32_t>(fstring_addr + offsetof(TArrayHeader, count), new_count);
    }
};
} // namespace ue4

// ============================================================================
// 5. Runtime Context & Background Telemetry Worker Thread
// ============================================================================
struct INSTRUMENT_LOCAL InstrumentationState {
    std::atomic<bool>  running{false};
    std::atomic<bool>  thread_active{false};
    pthread_t          worker_thread{};
    elf::ModuleSpan    ue4_module{};
    ue4::EngineOffsets offsets{};
};

INSTRUMENT_LOCAL static InstrumentationState g_state{};

INSTRUMENT_LOCAL inline void sleep_ms(uint32_t milliseconds) noexcept {
    struct timespec ts{};
    ts.tv_sec  = static_cast<time_t>(milliseconds / 1000U);
    ts.tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000UL);
    ::nanosleep(&ts, nullptr);
}

INSTRUMENT_LOCAL static void execute_diagnostic_tick() noexcept {
    const uintptr_t base = g_state.ue4_module.base_address;
    if (base == 0) {
        return;
    }
    const auto& off = g_state.offsets;
    // 1. Resolve UWorld pointer safely
    const auto uworld = mem::read<uintptr_t>(base + off.gworld_rva);
    if (!uworld.has_value() || *uworld == 0) {
        return;
    }
    // 2. Construct GNamePool decoder
    const ue4::GNamePoolResolver name_pool{base + off.gnamepool_rva};
    // 3. Traverse UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController -> AcknowledgedPawn
    uintptr_t    local_pawn      = 0;
    uintptr_t    local_player_st = 0;
    ue4::FVector local_origin{};
    const auto game_instance = mem::read<uintptr_t>(*uworld + off.owning_game_instance);
    if (game_instance.has_value() && *game_instance != 0) {
        const auto lp_array = mem::read<ue4::TArrayHeader>(*game_instance + off.local_players_array);
        if (lp_array.has_value() && lp_array->data_ptr != 0 && lp_array->count > 0) {
            const auto first_lp = mem::read<uintptr_t>(lp_array->data_ptr);
            if (first_lp.has_value() && *first_lp != 0) {
                const auto pc = mem::read<uintptr_t>(*first_lp + off.player_controller);
                if (pc.has_value() && *pc != 0) {
                    const auto pawn = mem::read<uintptr_t>(*pc + off.acknowledged_pawn);
                    if (pawn.has_value() && *pawn != 0) {
                        local_pawn = *pawn;
                        if (const auto loc = ue4::SpatialAndKinematicPipeline::read_actor_location(local_pawn, off)) {
                            local_origin = *loc;
                        }
                        if (const auto ps = mem::read<uintptr_t>(local_pawn + off.player_state)) {
                            local_player_st = *ps;
                        }
                    }
                }
            }
        }
    }
    // 4. Execute kinematic parameter tuning and LocalPlayerNameController
    if (local_pawn != 0) {
        (void)ue4::SpatialAndKinematicPipeline::apply_kinematic_limits(
            local_pawn,
            off,
            600.0f,
            2048.0f
        );
    }
    if (local_player_st != 0) {
        (void)ue4::LocalPlayerNameController::override_local_name(
            local_player_st,
            off,
            u"DiagNode_AArch64"
        );
    }
    // 5. Traverse UWorld -> PersistentLevel -> Actors array and filter spatially
    const auto ulevel = mem::read<uintptr_t>(*uworld + off.persistent_level);
    if (ulevel.has_value() && *ulevel != 0) {
        std::array<ue4::EntitySnapshot, ue4::SpatialAndKinematicPipeline::kMaxSnapshots> snapshots{};
        (void)ue4::SpatialAndKinematicPipeline::collect_nearby_entities(
            *ulevel,
            local_origin,
            250.0f,
            off,
            name_pool,
            snapshots
        );
    }
}

INSTRUMENT_LOCAL static void* telemetry_worker_entry(void* /*arg*/) noexcept {
    pthread_setname_np(pthread_self(), "ue4_diag_worker");
    while (g_state.running.load(std::memory_order_acquire)) {
        const auto mod = elf::find_module("libUE4.so");
        if (mod.has_value()) {
            std::array<uint8_t, sizeof(Elf64_Ehdr)> hdr{};
            if (InProcessMemory::safe_read(mod->base_address, hdr) == Status::Ok) {
                g_state.ue4_module = *mod;
                break;
            }
        }
        sleep_ms(250);
    }
    while (g_state.running.load(std::memory_order_acquire)) {
        execute_diagnostic_tick();
        sleep_ms(16);
    }
    return nullptr;
}

INSTRUMENT_LOCAL bool initialize_instrumentation() noexcept {
    bool expected = false;
    if (!g_state.running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
    pthread_attr_t attr{};
    if (pthread_attr_init(&attr) != 0) {
        g_state.running.store(false, std::memory_order_release);
        return false;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    const int rc = pthread_create(&g_state.worker_thread, &attr, &telemetry_worker_entry, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        g_state.running.store(false, std::memory_order_release);
        return false;
    }
    g_state.thread_active.store(true, std::memory_order_release);
    return true;
}

INSTRUMENT_LOCAL void shutdown_instrumentation() noexcept {
    if (!g_state.running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (g_state.thread_active.exchange(false, std::memory_order_acq_rel)) {
        pthread_join(g_state.worker_thread, nullptr);
    }
}
} // namespace runtime

// ============================================================================
// 6. Public C-ABI / JNI Lifecycle Exports
// ============================================================================
INSTRUMENT_C_EXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    if (!vm) {
        return JNI_ERR;
    }
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (!runtime::initialize_instrumentation()) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

INSTRUMENT_C_EXPORT void JNICALL JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    runtime::shutdown_instrumentation();
}
```

### 3. Comandos de Build CMake + Ninja e Auditoria `llvm-nm` (`libinstrument_core.so`)

#### Linux / macOS (Bash)
```bash
# 1. Configure CMake with the Android NDK toolchain targeting ELF64 AArch64 (arm64-v8a)
cmake -S . -B build-arm64 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static

# 2. Compile the shared library
cmake --build build-arm64 --config Release --parallel

# 3. Verify exported dynamic symbols via llvm-nm (only JNI_OnLoad and JNI_OnUnload should be listed)
"${ANDROID_NDK_HOME}"/toolchains/llvm/prebuilt/*/bin/llvm-nm -gD --defined-only build-arm64/libinstrument_core.so

# 4. (Optional) Verify dynamic symbol table and RELRO / strip status via llvm-readelf
"${ANDROID_NDK_HOME}"/toolchains/llvm/prebuilt/*/bin/llvm-readelf --dyn-syms -l build-arm64/libinstrument_core.so
```

#### Windows (PowerShell)
```powershell
# 1. Configure CMake with the Android NDK toolchain targeting ELF64 AArch64 (arm64-v8a)
cmake -S . -B build-arm64 `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

# 2. Compile the shared library
cmake --build build-arm64 --config Release --parallel

# 3. Verify exported dynamic symbols via llvm-nm (only JNI_OnLoad and JNI_OnUnload should be listed)
& "$env:ANDROID_NDK_HOME\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe" -gD --defined-only build-arm64\libinstrument_core.so

# 4. (Optional) Verify dynamic symbol table and RELRO / strip status via llvm-readelf
& "$env:ANDROID_NDK_HOME\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe" --dyn-syms -l build-arm64\libinstrument_core.so
```

#### Saída Esperada do `llvm-nm`
```text
0000000000001xxx T JNI_OnLoad
0000000000001xxx T JNI_OnUnload
```

### 4. Empacotamento Zygisk/Magisk e Estrutura de Deploy (`com.netease.dbdena`)

#### 4.1 Árvore de Diretórios do Módulo Magisk/Zygisk (`ue4_instrument_zygisk.zip`)
No modelo Zygisk (Magisk v24+), o daemon do Magisk carrega automaticamente `zygisk/arm64-v8a.so` no processo `zygote64`. Para manter `libinstrument_core.so` estritamente isolado (exportando apenas `JNI_OnLoad` / `JNI_OnUnload`) ou embutido diretamente com o entrypoint `zygisk_module_entry`, a estrutura padrão do pacote `.zip` e do diretório instalado em `/data/adb/modules/ue4_instrument_core/` é:

```text
ue4_instrument_zygisk/
├── META-INF/
│   └── com/
│       └── google/
│           └── android/
│               ├── update-binary          # Script oficial module_installer.sh do Magisk
│               └── updater-script         # Contém apenas a string: #MAGISK
├── module.prop                            # Metadados do módulo Magisk (id, name, version, author)
├── customize.sh                           # Verificação de arquitetura arm64-v8a e permissões (0755/0644)
├── sepolicy.rule                          # (Opcional) Regras SELinux caso necessário para leitura de maps
├── system/
│   └── lib64/
│       └── libinstrument_core.so          # Biblioteca core compilada (JNI_OnLoad / JNI_OnUnload)
└── zygisk/
    └── arm64-v8a.so                       # Loader Zygisk (filtra "com.netease.dbdena" em preAppSpecialize)
```

* **`module.prop`:**
  ```ini
  id=ue4_instrument_core
  name=UE4.27 AArch64 Diagnostics & Telemetry Suite
  version=v1.0.0-rev7
  versionCode=107
  author=SystemsEngineering
  description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
  ```

* **`customize.sh`:**
  ```sh
  SKIPUNZIP=0
  if [ "$ARCH" != "arm64" ]; then
    abort "! Apenas arquitetura arm64-v8a (ELF64 AArch64) e suportada."
  fi
  ui_print "- Configurando permissoes para libinstrument_core.so e zygisk/arm64-v8a.so"
  set_perm_recursive "$MODPATH" 0 0 0755 0644
  set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755
  set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
  ```

* **Filtro de Pacote no Loader Zygisk (`zygisk/arm64-v8a.so` ou integrado ao `entry.cpp`):**
  1. Em `preAppSpecialize(zygisk::AppSpecializeArgs* args)`, converte `args->nice_name` via `env->GetStringUTFChars`.
  2. Compara exatamente com `"com.netease.dbdena"` (ou prefixo `"com.netease.dbdena"` para subprocessos).
  3. Se **não** for o pacote alvo: executa `api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY)` e retorna imediatamente (zero footprint em outros apps).
  4. Se **for** `com.netease.dbdena`: em `postAppSpecialize(const zygisk::AppSpecializeArgs*)`, carrega `/system/lib64/libinstrument_core.so` via `dlopen` e invoca `JNI_OnLoad(vm, nullptr)` (ou chama `runtime::initialize_instrumentation()` diretamente caso compilado em binário único).

---

#### 4.2 Comandos `adb` e `logcat` para Deploy, Inicialização e Monitoramento em Tempo Real

```bash
# 1. Empacotar o módulo na máquina host (sem comprimir o diretório raiz, apenas seu conteúdo)
cd ue4_instrument_zygisk && zip -r9 ../ue4_instrument_zygisk.zip * -x "*.DS_Store" && cd ..

# 2. Enviar o pacote .zip para o dispositivo Android via ADB
adb push ue4_instrument_zygisk.zip /data/local/tmp/ue4_instrument_zygisk.zip

# 3. Instalar o módulo via CLI do Magisk (requer root su)
adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"

# 4. Reiniciar o dispositivo (ou reiniciar o userspace/Zygote) para ativar o mount e o hook Zygisk
adb reboot
# Alternativa rápida (Soft-Reboot do Zygote sem reiniciar o kernel):
# adb shell "su -c 'stop && start'"

# 5. Aguardar reconexão do ADB e confirmar se o módulo está ativo
adb wait-for-device
adb shell "su -c 'ls -la /data/adb/modules/ue4_instrument_core/zygisk/'"

# 6. Limpar o buffer anterior do logcat e iniciar o pacote alvo (com.netease.dbdena)
adb logcat -c
adb shell "monkey -p com.netease.dbdena -c android.intent.category.LAUNCHER 1"

# 7. Verificar se libUE4.so e libinstrument_core.so estão mapeados no espaço de endereçamento do processo
adb shell "su -c 'PID=\$(pidof com.netease.dbdena); cat /proc/\$PID/maps | grep -E \"libUE4\\.so|libinstrument_core\\.so|arm64-v8a\\.so\"'"

# 8. Verificar se a thread dedicada (ue4_diag_worker) está ativa no processo
adb shell "su -c 'PID=\$(pidof com.netease.dbdena); ps -T -p \$PID | grep ue4_diag_worker'"

# 9. Filtrar em tempo real os logs de diagnóstico, falhas nativas (crash/tombstone) e eventos Zygisk
adb logcat -v threadtime -s UE4Diag:V Zygisk:V DEBUG:V libc:W
```

---

#### 4.3 Scripts Automatizados de Empacotamento, Verificação e Deploy (`build_module.sh` e `build_module.ps1`)

##### Linux / macOS (`build_module.sh`)
```bash
#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# Script        : build_module.sh
# Target        : com.netease.dbdena (ELF64 AArch64 / arm64-v8a)
# Artifact      : ue4_instrument_zygisk.zip
# ============================================================================

: "${ANDROID_NDK_HOME:?Defina a variavel de ambiente ANDROID_NDK_HOME}"

BUILD_DIR="build-arm64"
STAGE_DIR="dist/ue4_instrument_zygisk"
ZIP_OUT="dist/ue4_instrument_zygisk.zip"
CORE_SO="${BUILD_DIR}/libinstrument_core.so"
ZYGISK_SO="${BUILD_DIR}/libzygisk_loader.so"

echo "[1/6] Compilando artefatos ELF64 AArch64 (Release)..."
cmake -S . -B "${BUILD_DIR}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static

cmake --build "${BUILD_DIR}" --config Release --parallel

echo "[2/6] Auditando tabela de simbolos dinamicos via llvm-nm..."
LLVM_NM=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-nm" | head -n 1)
LLVM_STRIP=$(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" -name "llvm-strip" | head -n 1)

"${LLVM_STRIP}" --strip-unneeded "${CORE_SO}"
"${LLVM_NM}" -gD --defined-only "${CORE_SO}"

echo "[3/6] Montando arvore de diretorios do modulo Magisk/Zygisk..."
rm -rf "${STAGE_DIR}" "${ZIP_OUT}"
mkdir -p \
  "${STAGE_DIR}/META-INF/com/google/android" \
  "${STAGE_DIR}/system/lib64" \
  "${STAGE_DIR}/zygisk"

cat > "${STAGE_DIR}/module.prop" << 'EOF'
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
EOF

cat > "${STAGE_DIR}/customize.sh" << 'EOF'
SKIPUNZIP=0

if [ "$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/updater-script" << 'EOF'
#MAGISK
EOF

cat > "${STAGE_DIR}/META-INF/com/google/android/update-binary" << 'EOF'
#!/sbin/sh
umask 022
ui_print() { echo "$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=$2
ZIPFILE=$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ $MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
EOF

cp -f "${CORE_SO}" "${STAGE_DIR}/system/lib64/libinstrument_core.so"
if [ -f "${ZYGISK_SO}" ]; then
  "${LLVM_STRIP}" --strip-unneeded "${ZYGISK_SO}"
  cp -f "${ZYGISK_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
else
  cp -f "${CORE_SO}" "${STAGE_DIR}/zygisk/arm64-v8a.so"
fi

echo "[4/6] Aplicando permissoes locais (0755/0644) e gerando pacote ZIP..."
find "${STAGE_DIR}" -type d -exec chmod 0755 {} +
find "${STAGE_DIR}" -type f -exec chmod 0644 {} +
chmod 0755 \
  "${STAGE_DIR}/META-INF/com/google/android/update-binary" \
  "${STAGE_DIR}/customize.sh" \
  "${STAGE_DIR}/system/lib64/libinstrument_core.so" \
  "${STAGE_DIR}/zygisk/arm64-v8a.so"

(
  cd "${STAGE_DIR}"
  zip -r9 -q "../$(basename "${ZIP_OUT}")" \
    META-INF \
    module.prop \
    customize.sh \
    system \
    zygisk
)

echo "[5/6] Enviando ${ZIP_OUT} via ADB e executando instalacao no Magisk..."
if adb get-state >/dev/null 2>&1; then
  adb push "${ZIP_OUT}" /data/local/tmp/ue4_instrument_zygisk.zip
  adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
  echo "[6/6] Modulo instalado com sucesso. Reinicie o runtime com: adb reboot"
else
  echo "[6/6] Pacote gerado em ${ZIP_OUT} (nenhum dispositivo ADB conectado no momento)."
fi
```

##### Windows (`build_module.ps1`)
```powershell
# ============================================================================
# Script        : build_module.ps1
# Target        : com.netease.dbdena (ELF64 AArch64 / arm64-v8a)
# Artifact      : dist\ue4_instrument_zygisk.zip
# ============================================================================
$ErrorActionPreference = "Stop"

if (-not $env:ANDROID_NDK_HOME) {
    throw "Variavel de ambiente ANDROID_NDK_HOME nao definida."
}

$BuildDir  = "build-arm64"
$StageDir  = "dist\ue4_instrument_zygisk"
$ZipOut    = "dist\ue4_instrument_zygisk.zip"
$CoreSo    = Join-Path $BuildDir "libinstrument_core.so"
$ZygiskSo  = Join-Path $BuildDir "libzygisk_loader.so"
$LlvmBin   = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin"
$LlvmNm    = Join-Path $LlvmBin "llvm-nm.exe"
$LlvmStrip = Join-Path $LlvmBin "llvm-strip.exe"

Write-Host "[1/6] Compilando artefatos ELF64 AArch64 (Release)..." -ForegroundColor Cyan
cmake -S . -B $BuildDir `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-29 `
  -DANDROID_STL=c++_static

cmake --build $BuildDir --config Release --parallel

Write-Host "[2/6] Auditando e limpando simbolos dinamicos via llvm-strip / llvm-nm..." -ForegroundColor Cyan
& $LlvmStrip --strip-unneeded $CoreSo
& $LlvmNm -gD --defined-only $CoreSo

Write-Host "[3/6] Montando estrutura de diretorios do modulo Magisk/Zygisk..." -ForegroundColor Cyan
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
if (Test-Path $ZipOut)   { Remove-Item -Force $ZipOut }

New-Item -ItemType Directory -Force -Path "$StageDir\META-INF\com\google\android" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\system\lib64"                | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\zygisk"                      | Out-Null

$ModuleProp = @"
id=ue4_instrument_core
name=UE4.27 AArch64 Diagnostics & Telemetry Suite
version=v1.0.0-rev7
versionCode=107
author=SystemsEngineering
description=In-process ELF64 AArch64 telemetry and diagnostic instrumentation for com.netease.dbdena (UE 4.27)
"@
[System.IO.File]::WriteAllText("$StageDir\module.prop", ($ModuleProp -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

$CustomizeSh = @"
SKIPUNZIP=0

if [ "`$ARCH" != "arm64" ]; then
  abort "! Arquitetura incompativel: este modulo requer arm64-v8a (ELF64 AArch64)."
fi

ui_print "- Instalando UE4.27 AArch64 Diagnostics Suite (target: com.netease.dbdena)"
set_perm_recursive "`$MODPATH" 0 0 0755 0644
set_perm "`$MODPATH/system/lib64/libinstrument_core.so" 0 0 0755 u:object_r:system_lib_file:s0
set_perm "`$MODPATH/zygisk/arm64-v8a.so" 0 0 0755
"@
[System.IO.File]::WriteAllText("$StageDir\customize.sh", ($CustomizeSh -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\updater-script", "#MAGISK`n", [System.Text.UTF8Encoding]::new($false))

$UpdateBinary = @"
#!/sbin/sh
umask 022
ui_print() { echo "`$1"; }
require_new_magisk() {
  ui_print "**********************************"
  ui_print " Instale o Magisk v24.0+ (Zygisk) "
  ui_print "**********************************"
  exit 1
}
OUTFD=`$2
ZIPFILE=`$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ `$MAGISK_VER_CODE -lt 24000 ] && require_new_magisk
install_module
exit 0
"@
[System.IO.File]::WriteAllText("$StageDir\META-INF\com\google\android\update-binary", ($UpdateBinary -replace "`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

Copy-Item -Force $CoreSo "$StageDir\system\lib64\libinstrument_core.so"
if (Test-Path $ZygiskSo) {
    & $LlvmStrip --strip-unneeded $ZygiskSo
    Copy-Item -Force $ZygiskSo "$StageDir\zygisk\arm64-v8a.so"
} else {
    Copy-Item -Force $CoreSo "$StageDir\zygisk\arm64-v8a.so"
}

Write-Host "[4/6] Empacotando arquivo ZIP com caminhos POSIX..." -ForegroundColor Cyan
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipOut -CompressionLevel Optimal -Force

Write-Host "[5/6] Verificando conexao ADB para deploy..." -ForegroundColor Cyan
$AdbDevices = (& adb devices) -join "`n"
if ($AdbDevices -match "\bdevice\b") {
    & adb push $ZipOut /data/local/tmp/ue4_instrument_zygisk.zip
    & adb shell "su -c 'magisk --install-module /data/local/tmp/ue4_instrument_zygisk.zip'"
    Write-Host "[6/6] Modulo instalado com sucesso. Execute 'adb reboot' para ativar." -ForegroundColor Green
} else {
    Write-Host "[6/6] Arquivo gerado em $ZipOut (nenhum dispositivo ADB online)." -ForegroundColor Yellow
}
```



