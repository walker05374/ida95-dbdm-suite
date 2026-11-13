// File: native_outline_controller.hpp
// ============================================================================
// Architecture : ELF64 AArch64 (arm64-v8a) | Unreal Engine 4.27 (libUE4.so)
// Target       : com.netease.dbdena (Build 292949 — Grade A Verified Offsets)
// Module       : Native UE4 CustomDepth / Outline Stencil Telemetry Controller
// Standard     : C++20 (-std=c++20 -O3 -fno-exceptions -fno-rtti)
// ============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hud::outline {

// ============================================================================
// 1. Static Reflection & Disassembly Verified RVAs / Offsets (libUE4.so)
// ============================================================================
namespace offsets {

// Conversion factor: 1.0 meter = 100.0 Unreal Engine 4 units (centimeters)
inline constexpr float kUe4UnitsPerMeter = 100.0f;
inline constexpr float kMetersPerUe4Unit = 0.01f;

// ----------------------------------------------------------------------------
// 1.1 Global Engine Singletons & Reflection Tables (libUE4_fixed.so)
// ----------------------------------------------------------------------------
inline constexpr std::uintptr_t kRvaGWorld                          = 0x10D69998ULL;
inline constexpr std::uintptr_t kRvaGNamePool                       = 0x10B7C100ULL;
inline constexpr std::uintptr_t kRvaGUObjectArray                   = 0x10BF7920ULL;
inline constexpr std::uintptr_t kRvaFWeakObjectPtrGet               = 0x0A796E4CULL; // FWeakObjectPtr::Get()

// ----------------------------------------------------------------------------
// 1.2 UObjectBase & FUObjectArray / FUObjectItem GC Lifecycle Flags
// ----------------------------------------------------------------------------
inline constexpr std::uintptr_t kUObjectVTable                      = 0x0000ULL;     // void** VTable
inline constexpr std::uintptr_t kUObjectFlags                       = 0x0008ULL;     // EObjectFlags (uint32)
inline constexpr std::uintptr_t kUObjectInternalIndex               = 0x000CULL;     // int32 InternalIndex
inline constexpr std::uintptr_t kUObjectClassPrivate                = 0x0010ULL;     // UClass* ClassPrivate
inline constexpr std::uintptr_t kUObjectNameComparisonIndex         = 0x0018ULL;     // FName::ComparisonIndex
inline constexpr std::uintptr_t kUObjectNameDisplayIndex            = 0x001CULL;     // FName::DisplayIndex
inline constexpr std::uintptr_t kUObjectOuterPrivate                = 0x0028ULL;     // UObject* OuterPrivate
inline constexpr std::uintptr_t kUStructSuperStruct                 = 0x0048ULL;     // UStruct* SuperStruct

inline constexpr std::uint32_t  kRF_BeginDestroyed                  = 0x00008000U;   // 1U << 15
inline constexpr std::uint32_t  kRF_FinishDestroyed                 = 0x00010000U;   // 1U << 16
inline constexpr std::uint32_t  kFUObjectItemPendingKill            = 0x20000000U;   // 1U << 29 (RF_PendingKill in FUObjectItem::Flags)
inline constexpr std::uint32_t  kFUObjectItemUnreachable            = 0x10000000U;   // 1U << 28 (Unreachable in FUObjectItem::Flags)

inline constexpr std::uintptr_t kGUObjectObjectsOffset              = 0x0010ULL;     // ObjObjects.Objects (FUObjectItem**)
inline constexpr std::uintptr_t kGUObjectMaxElementsOffset          = 0x0020ULL;     // ObjObjects.MaxElements (int32)
inline constexpr std::uintptr_t kGUObjectNumElementsOffset          = 0x0024ULL;     // ObjObjects.NumElements (int32)
inline constexpr std::uintptr_t kGUObjectNumChunksOffset            = 0x002CULL;     // ObjObjects.NumChunks (int32)
inline constexpr std::size_t    kFUObjectItemStride                 = 0x0018ULL;     // sizeof(FUObjectItem) = 24 bytes
inline constexpr std::size_t    kFUObjectChunkElements              = 65536ULL;      // 64K objects per chunk

// ----------------------------------------------------------------------------
// 1.3 UDBDOutlineComponent (: UBatchMeshCommands : USceneComponent, Size = 0x03A0)
// ----------------------------------------------------------------------------
inline constexpr std::uintptr_t kRvaDBDOutlineStaticClass           = 0x077F1C04ULL;
inline constexpr std::uintptr_t kRvaDBDOutlineZConstruct            = 0x077F2104ULL;
inline constexpr std::uintptr_t kRvaDBDOutlineNativeCtor            = 0x074887D0ULL;
inline constexpr std::uintptr_t kRvaDBDOutlineVTable                = 0x0EAF5390ULL; // +0x10 = 0x0EAF53A0
inline constexpr std::uintptr_t kRvaDBDOutlineClassParams           = 0x0EBC33F8ULL;
inline constexpr std::uintptr_t kRvaDBDOutlineTickComponent         = 0x07489D6CULL; // VTable[108] (+0x360)
inline constexpr std::uintptr_t kRvaDBDOutlineSetTargetColor        = 0x0748A47CULL; // exec = 0x077F1A78
inline constexpr std::uintptr_t kRvaDBDOutlineSetMinDistance        = 0x0748A61CULL; // exec = 0x077F19D0
inline constexpr std::uintptr_t kRvaDBDOutlineSetFadeIntensity      = 0x0748A6B4ULL; // exec = 0x077F1880
inline constexpr std::uintptr_t kRvaDBDOutlineSetOutlineIntensity   = 0x0748A6C4ULL; // exec = 0x077F17D8
inline constexpr std::uintptr_t kRvaDBDOutlineRefreshComponent      = 0x07488C4CULL; // exec = 0x077F1B20
inline constexpr std::uintptr_t kRvaDBDOutlineReInitBatcher         = 0x0748A194ULL; // exec = 0x077F1B34
inline constexpr std::uintptr_t kRvaDBDOutlineResetToTransparent    = 0x0748A8ACULL; // exec = 0x077F17C4
inline constexpr std::uintptr_t kRvaDBDOutlineApplyToRenderStrategy = 0x0748A03CULL;

// UBatchMeshCommands (: USceneComponent, Size = 0x02A0)
inline constexpr std::uintptr_t kRvaBatchMeshSetRenderCustomDepth   = 0x0D12E248ULL; // exec = 0x0D13A510
inline constexpr std::uintptr_t kRvaBatchMeshSetCustomDepthStencil  = 0x0D12E30CULL; // exec = 0x0D13A468
inline constexpr std::uintptr_t kBatchMeshGroups                    = 0x0218ULL;     // TArray<FBatchMeshCommandsGroup>
inline constexpr std::uintptr_t kBatchMeshEnableMDICopy             = 0x0228ULL;     // bool EnableMDIParametersCopy
inline constexpr std::uintptr_t kBatchMeshTargetMeshes              = 0x0290ULL;     // TArray<FTargetMeshEntry> (stride = 0x48)
inline constexpr std::size_t    kBatchMeshTargetEntryStride         = 0x0048ULL;     // 72 bytes per entry

// UDBDOutlineComponent Member Offsets
inline constexpr std::uintptr_t kOutlineMaterialGroupName           = 0x02A0ULL;     // FString _materialGroupName
inline constexpr std::uintptr_t kOutlineInterpolationSpeed          = 0x02B0ULL;     // float InterpolationSpeed (default 2.0f)
inline constexpr std::uintptr_t kOutlineShouldBeAboveOutlines       = 0x02B4ULL;     // uint8 bool ShouldBeAboveOutlines (SetBit @ 0x077F21C4)
inline constexpr std::uintptr_t kOutlineLimitToCustomDepthObjects   = 0x02B5ULL;     // uint8 bool LimitToCustomDepthObjects (SetBit @ 0x077F21D0)
inline constexpr std::uintptr_t kOutlineFadeOutAsClosingIn          = 0x02B6ULL;     // uint8 bool FadeOutAsClosingIn (SetBit @ 0x077F21DC)
inline constexpr std::uintptr_t kOutlineIsAlwaysVisible             = 0x02B7ULL;     // uint8 bool IsAlwaysVisible (SetBit @ 0x077F21E8)
inline constexpr std::uintptr_t kOutlineMinimumOutlineDistance      = 0x02B8ULL;     // float MinimumOutlineDistance (default 448.0f)
inline constexpr std::uintptr_t kOutlineIsUseEventStrategy          = 0x02BCULL;     // uint8 bool IsUseEventOutlineStrategy (SetBit @ 0x077F21F4)
inline constexpr std::uintptr_t kOutlineForceOutlineFarAway         = 0x02BDULL;     // uint8 bool ForceOutlineFarAway (SetBit @ 0x077F2200)
inline constexpr std::uintptr_t kOutlineName                        = 0x02C8ULL;     // FString OutlineName
inline constexpr std::uintptr_t kOutlineCurrentShaderColor          = 0x02D8ULL;     // FLinearColor CurrentShaderColor (16 bytes RGBA)
inline constexpr std::uintptr_t kOutlinePreviousColor               = 0x02E8ULL;     // FLinearColor PreviousColor (16 bytes RGBA)
inline constexpr std::uintptr_t kOutlineSavedAlpha                  = 0x02F8ULL;     // float SavedAlpha
inline constexpr std::uintptr_t kOutlineInterpolationAlpha          = 0x02FCULL;     // float ColorInterpolationAlpha (1.0f = complete)
inline constexpr std::uintptr_t kOutlineCurrentTargetColor          = 0x0300ULL;     // FLinearColor CurrentTargetColor (16 bytes RGBA)
inline constexpr std::uintptr_t kOutlineTargetColor                 = 0x0314ULL;     // FLinearColor TargetColor (16 bytes RGBA)
inline constexpr std::uintptr_t kOutlineClipIfVisible               = 0x0324ULL;     // uint8 bool bIsClipOutlinesIfVisible
inline constexpr std::uintptr_t kOutlineFadeIntensity               = 0x0328ULL;     // float FadeIntensity (default 1.0f)
inline constexpr std::uintptr_t kOutlineRenderStrategySelector      = 0x0330ULL;     // FDBDOutlineRenderStrategySelector
inline constexpr std::uintptr_t kOutlineBatchMeshCommandsPtr        = 0x0370ULL;     // UBatchMeshCommands* _batchMeshCommands
inline constexpr std::uintptr_t kOutlineIsInitialized               = 0x0380ULL;     // uint8 bool bIsOutlineInitialized
inline constexpr std::uintptr_t kOutlineRenderingStrategyPtr        = 0x0388ULL;     // UBaseOutlineRenderStrategy* _renderingStrategy

// ----------------------------------------------------------------------------
// 1.4 UOutlineUpdateStrategy & Subclasses (: UActorComponent, Base Size = 0x00C0)
// ----------------------------------------------------------------------------
inline constexpr std::uintptr_t kRvaOutlineStrategyStaticClass      = 0x07A9C5C4ULL;
inline constexpr std::uintptr_t kRvaOutlineStrategyNativeCtor       = 0x06D1886CULL;
inline constexpr std::uintptr_t kRvaOutlineStrategyVTable           = 0x0EA1EB98ULL; // +0x10 = 0x0EA1EBA8
inline constexpr std::uintptr_t kRvaOutlineStrategyTickComponent    = 0x06D188D4ULL;
inline constexpr std::uintptr_t kRvaSourceBasedStrategyStaticClass  = 0x07BC2B48ULL;
inline constexpr std::uintptr_t kRvaDefaultStrategyStaticClass      = 0x0789CD04ULL;
inline constexpr std::uintptr_t kRvaEscapeStrategyStaticClass       = 0x078F72B8ULL;
inline constexpr std::uintptr_t kRvaGeneratorStrategyStaticClass    = 0x07957C60ULL;
inline constexpr std::uintptr_t kRvaTotemStrategyStaticClass        = 0x07C60E84ULL;
inline constexpr std::uintptr_t kRvaVaultableStrategyStaticClass    = 0x07D672F8ULL;
inline constexpr std::uintptr_t kRvaPalletStrategyStaticClass       = 0x07AB8A60ULL;
inline constexpr std::uintptr_t kRvaSurvivorStrategyStaticClass     = 0x07C23250ULL;
inline constexpr std::uintptr_t kRvaSlasherStrategyStaticClass      = 0x07BA063CULL;

// Common pointer in all UOutlineUpdateStrategy subclasses:
inline constexpr std::uintptr_t kStrategyOutlineComponentPtr        = 0x00B8ULL;     // UDBDOutlineComponent* _outlineComponent

// Strategy-specific color and reveal fields:
inline constexpr std::uintptr_t kStrategyPrimaryColor               = 0x00C0ULL;     // FLinearColor (_defaultColorForSurvivor / _revealedColorToSurvivor / _doorOpenColorForSurvivor)
inline constexpr std::uintptr_t kStrategySecondaryColor             = 0x00D0ULL;     // FLinearColor (_defaultColorForSlasher / _revealedColorToKiller / _nonRevealedColor)
inline constexpr std::uintptr_t kSlasherStrategyRevealedColor       = 0x00E0ULL;     // FLinearColor USlasherOutlineUpdateStrategy::_revealedColor
inline constexpr std::uintptr_t kSlasherStrategyKillerColor         = 0x00F0ULL;     // FLinearColor USlasherOutlineUpdateStrategy::_revealedColorForKiller
inline constexpr std::uintptr_t kSurvivorStrategyKillerColor        = 0x0100ULL;     // FLinearColor USurvivorOutlineUpdateStrategy::_colorForKiller
inline constexpr std::uintptr_t kSurvivorStrategyCustomColor        = 0x0110ULL;     // FLinearColor USurvivorOutlineUpdateStrategy::_customColor
inline constexpr std::uintptr_t kSurvivorStrategyUseCustomColor     = 0x0145ULL;     // uint8 bool USurvivorOutlineUpdateStrategy::_useCustomColor (SetBit @ 0x07C2356C)
inline constexpr std::uintptr_t kVaultableStrategyCanBeRevealed     = 0x0138ULL;     // uint8 bool UVaultable/UPalletOutlineUpdateStrategy::_canBeRevealedToLocalPlayer
inline constexpr std::uintptr_t kPalletStrategyRevealedByPowerColor = 0x013CULL;     // FLinearColor UPalletOutlineUpdateStrategy::_revealedByPowerColor
inline constexpr std::uintptr_t kGenStrategyUnrepairedColor         = 0x0138ULL;     // FLinearColor UGeneratorOutlineUpdateStrategy::_unrepairedAuraColor
inline constexpr std::uintptr_t kGenStrategyFullyRepairedColor      = 0x0148ULL;     // FLinearColor UGeneratorOutlineUpdateStrategy::_fullyRepairedAuraColor
inline constexpr std::uintptr_t kTotemStrategyRevealedColor         = 0x0138ULL;     // FLinearColor UTotemOutlineUpdateStrategy::_revealedColorToSurvivor
inline constexpr std::uintptr_t kTotemStrategyBoonColor             = 0x0148ULL;     // FLinearColor UTotemOutlineUpdateStrategy::_boonTotemRevealedColorToSurvivor

// ----------------------------------------------------------------------------
// 1.5 AActor, ACharacter, ADBDPlayer, and AInteractable Hierarchy Offsets
// ----------------------------------------------------------------------------
inline constexpr std::uintptr_t kActorRootComponent                 = 0x0140ULL;     // USceneComponent* AActor::RootComponent
inline constexpr std::uintptr_t kActorOwnedComponents               = 0x01C0ULL;     // TSet<UActorComponent*> AActor::OwnedComponents (Data +0x1C0, Num +0x1C8, Max +0x1D8, stride 0x10)
inline constexpr std::uintptr_t kActorInstanceComponents            = 0x0200ULL;     // TArray<UActorComponent*> AActor::InstanceComponents
inline constexpr std::uintptr_t kActorBlueprintCreatedComponents    = 0x0210ULL;     // TArray<UActorComponent*> AActor::BlueprintCreatedComponents
inline constexpr std::uintptr_t kSceneComponentAttachParent         = 0x00C8ULL;     // USceneComponent* AttachParent
inline constexpr std::uintptr_t kSceneComponentAttachChildren       = 0x00E0ULL;     // TArray<USceneComponent*> AttachChildren
inline constexpr std::uintptr_t kSceneComponentVisibilityFlags      = 0x0164ULL;     // uint16 bitfield (bVisible=0x0040, bHiddenInGame=0x1000)

inline constexpr std::uintptr_t kCharacterSkeletalMeshPrimary       = 0x0288ULL;     // USkeletalMeshComponent* ACharacter::Mesh (layout A)
inline constexpr std::uintptr_t kCharacterSkeletalMeshSecondary     = 0x0290ULL;     // USkeletalMeshComponent* ACharacter::Mesh (FClassParams @ 0x0F290188, FPropertyParams @ 0x0F290040)
inline constexpr std::uintptr_t kDBDPlayerStateMachine              = 0x0AA8ULL;     // UPlayerStateMachine* ADBDPlayer::_stateMachine (FClassParams @ 0x0EBD9D20, FPropertyParams @ 0x0EBD9230, GetImmobilizedState @ 0x06F44E80: LDR X20, [X0, #0xAA8])
inline constexpr std::uintptr_t kStateMachineImmobilizeState        = 0x01A0ULL;     // uint8 EImmobilizedState (*(_stateMachine + 0x01A0), FEnumParams @ 0x0EBDC548, GetImmobilizedState @ 0x07009408: LDRB W0, [X0, #0x1A0])

// AGenerator (: AInteractable, FClassParams @ 0x0EC26220, Size = 0x0610, StaticClass = 0x07941410)
inline constexpr std::uintptr_t kGeneratorActivated                 = 0x0368ULL;     // uint8 bool Activated (FPropertyParams @ 0x0EC25480, SetBit @ 0x07942A2C)
inline constexpr std::uintptr_t kGeneratorIsRepaired                = 0x0369ULL;     // uint8 bool IsRepaired (FPropertyParams @ 0x0EC254A8, SetBit @ 0x07942A38)
inline constexpr std::uintptr_t kGeneratorForceRevealToLocalPlayer  = 0x036DULL;     // uint8 bool ForceRevealToLocalPlayer (FPropertyParams @ 0x0EC25548, SetBit @ 0x07942A5C)
inline constexpr std::uintptr_t kGeneratorNativePercentComplete     = 0x0378ULL;     // float NativePercentComplete (0.0f .. 1.0f, FPropertyParams @ 0x0EC255C0)
inline constexpr std::uintptr_t kGeneratorChargeComponent           = 0x0520ULL;     // UChargeableComponent* _generatorCharge (FPropertyParams @ 0x0EC25A58)

// APallet (: AInteractable, FClassParams @ 0x0EC6DC38, Size = 0x0468, StaticClass = 0x07AB7474)
inline constexpr std::uintptr_t kPalletState                        = 0x0368ULL;     // uint8 EPalletState _state (FPropertyParams @ 0x0EC6D5C8: 0=Up, 1=Falling, 2=Fallen, 3=Destroyed, 4=Illusionary)
inline constexpr std::uintptr_t kPalletIsPendingDestruction         = 0x036AULL;     // uint8 bool _isPendingDestruction (FPropertyParams @ 0x0EC6D620, SetBit @ 0x07AB84D4)
inline constexpr std::uintptr_t kPalletIsDestroyedByEntity          = 0x036CULL;     // uint8 bool _isDestroyedByEntity (FPropertyParams @ 0x0EC6D670, SetBit @ 0x07AB84EC)
inline constexpr std::uintptr_t kPalletIllusionaryMesh              = 0x0370ULL;     // USkeletalMeshComponent* _illusionaryPallet (FPropertyParams @ 0x0EC6D698)

// AWindow (: AInteractable, FClassParams @ 0x0ED23BF8, Size = 0x0458, StaticClass = 0x07D6BA8C)
inline constexpr std::uintptr_t kWindowCollider                     = 0x0398ULL;     // UPrimitiveComponent* _windowCollider (FPropertyParams @ 0x0ED237A8)
inline constexpr std::uintptr_t kWindowIsBlockedByLevel             = 0x03A0ULL;     // uint8 bool _isBlockedByLevel (FPropertyParams @ 0x0ED237D8, SetBit @ 0x07D6C07C)

// ATotem (: AInteractable, FClassParams @ 0x0ECECC50, Size = 0x04C0, StaticClass = 0x07C582B0)
inline constexpr std::uintptr_t kTotemBoonAuraRevealColor           = 0x0388ULL;     // FLinearColor _boonAuraRevealColor (FPropertyParams @ 0x0ECEC658)
inline constexpr std::uintptr_t kTotemState                         = 0x03A8ULL;     // uint8 ETotemState _totemState (FPropertyParams @ 0x0ECEC6D8: 0=Cleansed, 1=Dull, 2=Hex, 3=Boon)
inline constexpr std::uintptr_t kTotemOutlineUpdateStrategy         = 0x0450ULL;     // UTotemOutlineUpdateStrategy* _totemOutlineUpdateStrategy (FPropertyParams @ 0x0ECEC9A0)

// AEscapeDoor (: AInteractable, Size = 0x0448, StaticClass = 0x078F60A4)
inline constexpr std::uintptr_t kEscapeDoorKillerOpenChargeable     = 0x0380ULL;     // UChargeableComponent* _killerOpenChargeable
inline constexpr std::uintptr_t kEscapeDoorOpenChargeable           = 0x0388ULL;     // UChargeableComponent* _openChargeable
inline constexpr std::uintptr_t kEscapeDoorActivated                = 0x03A8ULL;     // uint8 bool _activated
inline constexpr std::uintptr_t kEscapeDoorIsOpen                   = 0x03E0ULL;     // uint8 bool _isOpen

// AHatch (: AInteractable, Size = 0x03E0, StaticClass = 0x0795C2D4)
inline constexpr std::uintptr_t kHatchState                         = 0x03A0ULL;     // uint8 EHatchState (0=Hidden, 1=DefaultClose, 2=Opened, 3=ForcedClose)
inline constexpr std::uintptr_t kHatchIsForceOpen                   = 0x03A3ULL;     // uint8 bool _isForceOpen
inline constexpr std::uintptr_t kHatchBaseCollision                 = 0x03B0ULL;     // UPrimitiveComponent* _baseCollision

// ASearchable (: AInteractable, Size = 0x0418, StaticClass = 0x07B87888)
inline constexpr std::uintptr_t kSearchableHasBeenSearched          = 0x038CULL;     // uint8 bool _hasBeenSearched (SetBit @ 0x07B87C68)

// ABearTrap (: ABaseTrap, Size = 0x02E0)
inline constexpr std::uintptr_t kBearTrapIsBroken                   = 0x02CDULL;     // uint8 bool _isBroken (SetBit @ 0x09D9A170)
inline constexpr std::uintptr_t kBearTrapIsBeingCollected           = 0x02CEULL;     // uint8 bool _isBeingCollected (SetBit @ 0x09D9A17C)
inline constexpr std::uintptr_t kBearTrapContainsSurvivor           = 0x02CFULL;     // uint8 bool _containsSurvivor (SetBit @ 0x09D9A188)
inline constexpr std::uintptr_t kBearTrapIsTrapSet                  = 0x02D0ULL;     // uint8 bool _isTrapSet (SetBit @ 0x09D9A194)
inline constexpr std::uintptr_t kBearTrapWasJustDisarmed            = 0x02D1ULL;     // uint8 bool _wasJustDisarmed (SetBit @ 0x09D9A1A0)

// ----------------------------------------------------------------------------
// 1.6 UPrimitiveComponent Fallback Offsets (: USceneComponent, Size = 0x0480)
// ----------------------------------------------------------------------------
inline constexpr std::uintptr_t kRvaPrimitiveStaticClass            = 0x0C9C6E18ULL;
inline constexpr std::uintptr_t kRvaPrimitiveSetRenderCustomDepth   = 0x0C063874ULL; // UPrimitiveComponent::SetRenderCustomDepth(bool)
inline constexpr std::uintptr_t kRvaPrimitiveSetCustomDepthStencil  = 0x0C063904ULL; // UPrimitiveComponent::SetCustomDepthStencilValue(int32)
inline constexpr std::uintptr_t kRvaActorComponentMarkRenderDirty   = 0x0C507074ULL; // UActorComponent::MarkRenderStateDirty()

inline constexpr std::uintptr_t kPrimitiveMinDrawDistance           = 0x0220ULL;     // float MinDrawDistance
inline constexpr std::uintptr_t kPrimitiveLDMaxDrawDistance         = 0x0224ULL;     // float LDMaxDrawDistance
inline constexpr std::uintptr_t kPrimitiveCachedMaxDrawDistance     = 0x0228ULL;     // float CachedMaxDrawDistance
inline constexpr std::uintptr_t kPrimitiveFlagsDword0               = 0x0230ULL;     // uint32 bitfield (bNeverDistanceCull = 0x00000004, bRenderCustomColourNoDepth = 0x00020000)
inline constexpr std::uintptr_t kPrimitiveCustomDepthFlagsDword     = 0x023CULL;     // uint32 bitfield containing bRenderCustomDepth
inline constexpr std::uintptr_t kPrimitiveCustomDepthFlagsByte      = 0x023EULL;     // uint8 byte containing bRenderCustomDepth (bit 5 = 0x20)
inline constexpr std::uintptr_t kPrimitiveCustomDepthWriteMask      = 0x0244ULL;     // uint8 ERendererStencilMask CustomDepthStencilWriteMask
inline constexpr std::uintptr_t kPrimitiveCustomDepthStencilValue   = 0x0248ULL;     // int32 CustomDepthStencilValue (0..255)
inline constexpr std::uintptr_t kPrimitiveSceneProxy                = 0x0458ULL;     // FPrimitiveSceneProxy* SceneProxy

inline constexpr std::uint32_t  kMaskNeverDistanceCull              = 0x00000004U;   // bit 2 at +0x0230
inline constexpr std::uint32_t  kMaskRenderCustomColourNoDepth      = 0x00020000U;   // bit 17 at +0x0230
inline constexpr std::uint32_t  kMaskRenderInMainPass               = 0x00400000U;   // bit 22 at +0x0230
inline constexpr std::uint32_t  kMaskRenderInDepthPass              = 0x00800000U;   // bit 23 at +0x0230
inline constexpr std::uint32_t  kMaskRenderCustomDepth              = 0x00200000U;   // bit 21 at +0x023C (SetBit @ 0x0C9C6DE8)
inline constexpr std::uint8_t   kByteMaskRenderCustomDepth          = 0x20U;         // bit 5 at +0x023E

} // namespace offsets

// ============================================================================
// 2. UE4 Linear Color & Entity Outline Categories
// ============================================================================
struct alignas(4) LinearColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    [[nodiscard]] constexpr bool approx_equals(const LinearColor& other, float eps = 0.01f) const noexcept {
        const float dr = (r > other.r) ? (r - other.r) : (other.r - r);
        const float dg = (g > other.g) ? (g - other.g) : (other.g - g);
        const float db = (b > other.b) ? (b - other.b) : (other.b - b);
        const float da = (a > other.a) ? (a - other.a) : (other.a - a);
        return (dr <= eps) && (dg <= eps) && (db <= eps) && (da <= eps);
    }

    [[nodiscard]] constexpr bool is_visible_aura() const noexcept {
        return (a > 0.05f) && ((r + g + b) > 0.05f);
    }
};

enum class OutlineCategory : std::uint8_t {
    Slasher    = 0, // 1. Agente Principal Oposto (ASlasherPlayer)
    Camper     = 1, // 2. Agentes Cooperativos (ACamperPlayer)
    Generator  = 2, // 3. Unidades de Progresso (AGenerator)
    Pallet     = 3, // 4a. Obstáculos Dinâmicos (APallet intactos)
    Window     = 4, // 4b. Vaults / Janelas (AWindow ativas)
    Totem      = 5, // 5. Nodos Secundários (ATotem: Dull vs. Hex/Boon)
    Hatch      = 6, // 6a. Ponto de Extração: Alçapão (AHatch)
    EscapeDoor = 7, // 6b. Ponto de Extração: Portões (AEscapeDoor)
    Searchable = 8, // 6c. Objetos Interativos: Baús (ASearchable)
    BearTrap   = 9, // 7. Armadilhas do Assassino (ABearTrap / ABaseTrap)
    Count      = 10,
    None       = 255
};

enum class TotemSubState : std::uint8_t {
    Cleansed = 0,
    Dull     = 1,
    Hex      = 2,
    Boon     = 3
};

struct CategoryOutlineFilter {
    bool        enabled{true};
    float       max_distance_m{64.0f};      // Cull distance in meters [0.0f .. 128.0f] (1m = 100.0f UE4 units)
    LinearColor custom_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::int32_t stencil_value{1};          // CustomDepthStencilValue fallback [1 .. 255]
};

struct NativeOutlineConfig {
    bool master_enabled{true};
    bool generator_dynamic_progress_color{true}; // Interpolate Red (0%) -> Yellow (50%) -> Green (99%)
    bool totem_distinguish_hex_boon{true};       // Differentiate Dull (Yellow) vs Hex (Red-Orange) vs Boon (Cyan-Blue)
    bool enable_custom_depth_fallback{true};     // Fallback to UPrimitiveComponent::bRenderCustomDepth + StencilValue
    bool kindred_mode{false};                    // Combined Kindred aura mode (Slasher + Camper + Generator + Totem + Hatch at 128m)
    std::uintptr_t obsession_actor_ptr{0};       // ADBDGameState::_obsessionTarget (+0x0738)

    // Per-category filters indexed by static_cast<size_t>(OutlineCategory)
    std::array<CategoryOutlineFilter, static_cast<std::size_t>(OutlineCategory::Count)> categories{{
        // [0] Slasher (ASlasherPlayer) — Red Crimson, 128m default
        CategoryOutlineFilter{true, 128.0f, LinearColor{1.00f, 0.14f, 0.14f, 1.00f}, 252},
        // [1] Camper (ACamperPlayer) — Cyan/Teal, 96m default
        CategoryOutlineFilter{true,  96.0f, LinearColor{0.00f, 0.90f, 1.00f, 1.00f}, 250},
        // [2] Generator (AGenerator) — Dynamic Progress / Emerald, 96m default
        CategoryOutlineFilter{true,  96.0f, LinearColor{0.18f, 0.80f, 0.44f, 1.00f}, 248},
        // [3] Pallet (APallet) — Gold/Amber, 48m default
        CategoryOutlineFilter{true,  48.0f, LinearColor{1.00f, 0.88f, 0.25f, 1.00f}, 244},
        // [4] Window (AWindow) — Sky Blue / White-Gold, 36m default
        CategoryOutlineFilter{true,  36.0f, LinearColor{0.35f, 0.78f, 1.00f, 1.00f}, 242},
        // [5] Totem (ATotem) — Dull Gold (Hex/Boon overridden when distinguish_hex_boon=true), 64m default
        CategoryOutlineFilter{true,  64.0f, LinearColor{0.95f, 0.77f, 0.06f, 1.00f}, 246},
        // [6] Hatch (AHatch) — Royal Purple / Magenta, 128m default
        CategoryOutlineFilter{true, 128.0f, LinearColor{0.88f, 0.25f, 0.98f, 1.00f}, 254},
        // [7] EscapeDoor (AEscapeDoor) — Violet / Amber, 128m default
        CategoryOutlineFilter{true, 128.0f, LinearColor{0.75f, 0.35f, 1.00f, 1.00f}, 253},
        // [8] Searchable / Chest (ASearchable) — Warm Orange, 48m default
        CategoryOutlineFilter{true,  48.0f, LinearColor{1.00f, 0.60f, 0.05f, 1.00f}, 240},
        // [9] BearTrap (ABearTrap / ABaseTrap) — Fiery Orange-Red, 64m default
        CategoryOutlineFilter{false, 64.0f, LinearColor{1.00f, 0.32f, 0.08f, 1.00f}, 245}
    }};

    LinearColor totem_hex_color{1.00f, 0.22f, 0.05f, 1.00f};  // Active Hex Totem: Fiery Red-Orange
    LinearColor totem_boon_color{0.10f, 0.85f, 1.00f, 1.00f}; // Active Boon Totem: Luminous Cyan-Blue
    LinearColor obsession_color{1.00f, 0.30f, 0.85f, 1.00f};  // Obsession Survivor: Vivid Magenta-Pink

    [[nodiscard]] constexpr CategoryOutlineFilter& filter(OutlineCategory cat) noexcept {
        const auto idx = static_cast<std::size_t>(cat);
        return categories[idx < categories.size() ? idx : 0U];
    }

    [[nodiscard]] constexpr const CategoryOutlineFilter& filter(OutlineCategory cat) const noexcept {
        const auto idx = static_cast<std::size_t>(cat);
        return categories[idx < categories.size() ? idx : 0U];
    }
};

// Global thread-safe / shared outline configuration accessor
[[nodiscard]] inline NativeOutlineConfig& get_native_outline_config() noexcept {
    static NativeOutlineConfig s_config{};
    return s_config;
}

// ============================================================================
// 3. Pure Color Computation & Category Classification Helpers
// ============================================================================

// Computes dynamic generator FLinearColor from repair progress [0.0f .. 100.0f]:
//   0%  -> Red    (1.00, 0.18, 0.18)
//   50% -> Yellow (1.00, 0.85, 0.10)
//   99% -> Green  (0.18, 0.92, 0.42)
[[nodiscard]] constexpr LinearColor compute_generator_progress_color(float progress_pct) noexcept {
    const float clamped = (progress_pct < 0.0f) ? 0.0f : ((progress_pct > 100.0f) ? 100.0f : progress_pct);
    const float t       = clamped * 0.01f; // [0.0f .. 1.0f]
    if (t < 0.5f) {
        const float local_t = t * 2.0f;
        return LinearColor{
            1.00f,
            0.18f + (0.85f - 0.18f) * local_t,
            0.18f + (0.10f - 0.18f) * local_t,
            1.00f
        };
    }
    const float local_t = (t - 0.5f) * 2.0f;
    return LinearColor{
        1.00f + (0.18f - 1.00f) * local_t,
        0.85f + (0.92f - 0.85f) * local_t,
        0.10f + (0.42f - 0.10f) * local_t,
        1.00f
    };
}

[[nodiscard]] constexpr LinearColor compute_totem_state_color(
    TotemSubState              sub_state,
    const NativeOutlineConfig& cfg) noexcept {
    if (!cfg.totem_distinguish_hex_boon) {
        return cfg.filter(OutlineCategory::Totem).custom_color;
    }
    switch (sub_state) {
        case TotemSubState::Hex:  return cfg.totem_hex_color;
        case TotemSubState::Boon: return cfg.totem_boon_color;
        case TotemSubState::Dull:
        default:
            return cfg.filter(OutlineCategory::Totem).custom_color;
    }
}

// Converts meters to UE4 units (1m = 100.0f cm)
[[nodiscard]] constexpr float meters_to_ue4_units(float meters) noexcept {
    return (meters > 0.0f) ? (meters * offsets::kUe4UnitsPerMeter) : 0.0f;
}

// Validates that an untagged AArch64 pointer resides in canonical 48-bit user-space [0x10000, 0x00007FFFFFFFFFFF]
[[nodiscard]] constexpr std::uintptr_t untag_aarch64_ptr(std::uintptr_t raw_ptr) noexcept {
    return raw_ptr & 0x00FFFFFFFFFFFFFFULL;
}

[[nodiscard]] constexpr bool is_canonical_aarch64_ptr(std::uintptr_t raw_ptr) noexcept {
    const std::uintptr_t untagged = untag_aarch64_ptr(raw_ptr);
    return (untagged >= 0x00010000ULL &&
            untagged <= 0x00007FFFFFFFFFFFULL &&
            (untagged & 0x3ULL) == 0ULL);
}

namespace detail {

[[nodiscard]] constexpr unsigned char to_lower_ascii(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

[[nodiscard]] inline bool icontains_fast(
    const char* haystack,
    const char* needle,
    std::size_t max_len = 256) noexcept {
    if (haystack == nullptr || needle == nullptr || needle[0] == '\0') {
        return false;
    }
    std::size_t h_len = 0;
    while (h_len < max_len && haystack[h_len] != '\0') {
        ++h_len;
    }
    std::size_t n_len = 0;
    while (n_len < 64 && needle[n_len] != '\0') {
        ++n_len;
    }
    if (n_len == 0 || n_len > h_len) {
        return false;
    }
    for (std::size_t i = 0; i <= h_len - n_len; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < n_len; ++j) {
            if (to_lower_ascii(static_cast<unsigned char>(haystack[i + j])) !=
                to_lower_ascii(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// Classifies an actor into one of the 9 supported Native Outline Categories using its resolved
// FName, UClass name, and UStruct::SuperStruct chain.
[[nodiscard]] inline OutlineCategory classify_outline_category(
    const char* actor_name,
    const char* class_name,
    const char* super_chain = nullptr) noexcept {

    auto has_any = [&](const char* token) noexcept -> bool {
        return detail::icontains_fast(super_chain, token) ||
               detail::icontains_fast(class_name, token)  ||
               detail::icontains_fast(actor_name, token);
    };

    // Reject templates, CDOs, spawners (except HexSpawner), and destroyed remnants upfront
    if (detail::icontains_fast(actor_name, "Default__")   ||
        detail::icontains_fast(actor_name, "Archetype")   ||
        detail::icontains_fast(actor_name, "Lobby")       ||
        detail::icontains_fast(class_name, "Component")   ||
        detail::icontains_fast(actor_name, "LevelGenerator") ||
        detail::icontains_fast(actor_name, "Portal_Generator")) {
        return OutlineCategory::None;
    }

    // 1. ASlasherPlayer (Antagonist)
    if ((has_any("SlasherPlayer") || has_any("Slasher_Character") || has_any("BP_Slasher")) &&
        !has_any("Trap") && !has_any("Projectile") && !has_any("Power") &&
        !has_any("Stalker") && !has_any("Weapon") && !has_any("Mori") &&
        !has_any("SlasherInteractable")) {
        return OutlineCategory::Slasher;
    }

    // 2. ACamperPlayer (Cooperative Teammate)
    if ((has_any("CamperPlayer") || has_any("Camper_Character") ||
         has_any("BP_Camper") || has_any("CamperMale") || has_any("CamperFemale")) &&
        !has_any("CamperInteractable")) {
        return OutlineCategory::Camper;
    }

    // 3. AGenerator (Progress Node)
    if ((has_any("->Generator->") || has_any("GeneratorStandard") ||
         has_any("GeneratorNoPole") || has_any("GeneratorShort") ||
         has_any("GeneratorIndoor") || has_any("BP_Generator")) &&
        !has_any("GeneratorEntity") && !has_any("GeneratorDriven")) {
        return OutlineCategory::Generator;
    }

    // 4a. APallet (Dynamic Barrier)
    if ((has_any("->Pallet->") || has_any("BP_Pallet") || has_any("Bookshelf") ||
         detail::icontains_fast(class_name, "Pallet")) &&
        !has_any("Window") && !has_any("Vault") && !has_any("Broken") &&
        !has_any("Destroyed") && !has_any("Illusion") && !has_any("Fake")) {
        return OutlineCategory::Pallet;
    }

    // 4b. AWindow (Vault / Window Obstacle)
    if ((has_any("->Window->") || has_any("WindowStandard") || has_any("BP_Window") ||
         detail::icontains_fast(class_name, "Window")) &&
        !has_any("Blocker") && !has_any("ClipManager")) {
        return OutlineCategory::Window;
    }

    // 5. ATotem (Secondary Node — Dull / Hex / Boon)
    if ((has_any("->Totem->") || has_any("TotemBase") || has_any("BP_Totem") ||
         has_any("HexSpawner") || has_any("HexTotem") || has_any("BoonTotem")) &&
        !has_any("Cleansed") && !has_any("Destroyed") && !has_any("Broken")) {
        return OutlineCategory::Totem;
    }

    // 6a. AHatch (Escape Hatch)
    if (has_any("->Hatch->") || has_any("BP_Hatch") || detail::icontains_fast(class_name, "Hatch")) {
        return OutlineCategory::Hatch;
    }

    // 6b. AEscapeDoor (Exit Gate)
    if ((has_any("->EscapeDoor->") || has_any("BP_EscapeBase") || has_any("EscapeDoor") || has_any("ExitGate")) &&
        !has_any("BP_IND_ExitGate")) {
        return OutlineCategory::EscapeDoor;
    }

    // 6c. ASearchable (Chest)
    if ((has_any("->Searchable->") || has_any("BP_Chest") || detail::icontains_fast(class_name, "Searchable")) &&
        !has_any("Spawner")) {
        return OutlineCategory::Searchable;
    }

    // 7. ABearTrap / ABaseTrap (Killer Traps)
    if ((has_any("BearTrap") || has_any("BaseTrap") || detail::icontains_fast(class_name, "BearTrap")) &&
        !has_any("ReverseBearTrap") && !has_any("Remover") && !has_any("Spawner")) {
        return OutlineCategory::BearTrap;
    }

    return OutlineCategory::None;
}

// ============================================================================
// 4. Per-Actor Clean State Snapshot & O(1) Controller Cache
// ============================================================================
inline constexpr std::size_t kMaxTrackedOutlineActors = 256;
inline constexpr std::size_t kMaxFallbackMeshes       = 4;

struct FallbackMeshBaseline {
    std::uintptr_t mesh_ptr{0};
    std::uint32_t  flags_dword_230{0};
    std::uint32_t  custom_depth_dword_23c{0};
    std::int32_t   stencil_value_248{0};
    float          cached_max_draw_dist_228{0.0f};
    bool           captured{false};
};

struct OriginalOutlineSnapshot {
    bool        captured{false};
    // Universal verified UDBDOutlineComponent baseline fields (+0x02B4..+0x02BD, +0x02D8, +0x02E8, +0x02FC, +0x0300, +0x0314, +0x0328)
    std::uint8_t should_be_above_2b4{1};
    std::uint8_t limit_custom_depth_2b5{0};
    std::uint8_t fade_out_closing_in_2b6{0};
    std::uint8_t is_always_visible_2b7{0};
    float        min_outline_dist_2b8{448.0f};
    std::uint8_t is_use_event_strategy_2bc{0};
    std::uint8_t force_far_away_2bd{0};
    LinearColor  shader_color_2d8{};
    LinearColor  previous_color_2e8{};
    float        interp_alpha_2fc{1.0f};
    LinearColor  current_target_color_300{};
    LinearColor  target_color_314{};
    float        fade_intensity_328{1.0f};
    std::uint8_t generator_force_reveal_36d{0};
    LinearColor  gen_unrepaired_color_138{1.0f, 0.1f, 0.1f, 1.0f};
    bool         gen_strategy_captured{false};
};

struct ActorOutlineSlot {
    std::uintptr_t          actor_ptr{0};
    OutlineCategory         category{OutlineCategory::None};
    std::uintptr_t          outline_comp_ptr{0};
    std::uintptr_t          strategy_comp_ptr{0};
    std::size_t             mesh_count{0};
    std::array<FallbackMeshBaseline, kMaxFallbackMeshes> meshes{};
    OriginalOutlineSnapshot baseline{};
    LinearColor             last_applied_color{};
    bool                    components_resolved{false};
    bool                    currently_overridden{false};
    std::uint64_t           last_seen_tick{0};
    std::uint64_t           last_resolve_tick{0};
};

// ============================================================================
// 5. NativeOutlineController (O(1) Per-Actor State Machine + Clean Restore)
// ============================================================================
class NativeOutlineController {
public:
    constexpr NativeOutlineController() noexcept = default;

    struct UObjectHeaderProbe {
        std::uintptr_t vtable{0};
        std::uint32_t  object_flags{0};
        std::int32_t   internal_idx{-1};
        std::uintptr_t class_private{0};
    };

    // Validates that a UObject pointer is canonical, mapped, and NOT marked RF_BeginDestroyed
    // or RF_FinishDestroyed using a single atomic 24-byte header read.
    template <typename ReadFn>
    [[nodiscard]] static bool is_uobject_alive(
        ReadFn&&       read_mem,
        std::uintptr_t obj_ptr,
        std::uintptr_t /*guobjectarray_abs*/ = 0) noexcept {

        const std::uintptr_t addr = untag_aarch64_ptr(obj_ptr);
        if (!is_canonical_aarch64_ptr(addr)) {
            return false;
        }

        UObjectHeaderProbe hdr{};
        if (!read_mem(addr, hdr)) {
            return false;
        }

        if (!is_canonical_aarch64_ptr(untag_aarch64_ptr(hdr.vtable)) ||
            !is_canonical_aarch64_ptr(untag_aarch64_ptr(hdr.class_private))) {
            return false;
        }

        // Check UObject::ObjectFlags for RF_BeginDestroyed | RF_FinishDestroyed
        if ((hdr.object_flags & (offsets::kRF_BeginDestroyed | offsets::kRF_FinishDestroyed)) != 0U) {
            return false;
        }

        if (hdr.internal_idx < 0 || hdr.internal_idx >= 0x400000) {
            return false;
        }

        return true;
    }

    // Checks whether an interactive actor is currently in a valid/intact state for highlighting.
    // Returns false for destroyed/fallen pallets, blocked windows, cleansed totems, searched chests,
    // dead survivors, or completed generators (100% / IsRepaired).
    template <typename ReadFn>
    [[nodiscard]] static bool is_actor_state_eligible(
        ReadFn&&        read_mem,
        std::uintptr_t  actor_ptr,
        OutlineCategory category,
        float           progress_pct,
        TotemSubState*  out_totem_state = nullptr) noexcept {

        const std::uintptr_t addr = untag_aarch64_ptr(actor_ptr);
        if (!is_canonical_aarch64_ptr(addr)) {
            return false;
        }

        switch (category) {
            case OutlineCategory::Slasher:
                return true;

            case OutlineCategory::Camper: {
                // ADBDPlayer::_stateMachine (+0x0AA8) -> EImmobilizedState (+0x01A0), VE_Dead == 4
                std::uintptr_t state_machine = 0;
                if (read_mem(addr + offsets::kDBDPlayerStateMachine, state_machine)) {
                    const std::uintptr_t clean_sm = untag_aarch64_ptr(state_machine);
                    if (is_canonical_aarch64_ptr(clean_sm)) {
                        std::uint8_t imm_state = 0;
                        if (read_mem(clean_sm + offsets::kStateMachineImmobilizeState, imm_state) && imm_state == 4U) {
                            return false; // EImmobilizedState::VE_Dead
                        }
                    }
                }
                return true;
            }

            case OutlineCategory::Generator: {
                std::uint8_t is_repaired = 0;
                std::uint8_t activated   = 0;
                (void)read_mem(addr + offsets::kGeneratorIsRepaired, is_repaired);
                (void)read_mem(addr + offsets::kGeneratorActivated, activated);
                if ((is_repaired & 0x01U) != 0U || (activated & 0x01U) != 0U) {
                    return false;
                }
                if (std::isfinite(progress_pct) && progress_pct >= 99.9f) {
                    return false;
                }
                return true;
            }

            case OutlineCategory::Pallet: {
                // APallet::_state (+0x0368): 0=Up, 1=Falling, 2=Fallen, 3=Destroyed, 4=Illusionary
                std::uint8_t pallet_state = 0;
                std::uint8_t pending_dest = 0;
                std::uint8_t entity_dest  = 0;
                if (read_mem(addr + offsets::kPalletState, pallet_state) && pallet_state >= 3U) {
                    return false;
                }
                if (read_mem(addr + offsets::kPalletIsPendingDestruction, pending_dest) && (pending_dest & 0x01U) != 0U) {
                    return false;
                }
                if (read_mem(addr + offsets::kPalletIsDestroyedByEntity, entity_dest) && (entity_dest & 0x01U) != 0U) {
                    return false;
                }
                return true;
            }

            case OutlineCategory::Window: {
                // AWindow::_isBlockedByLevel (+0x03A0)
                std::uint8_t blocked = 0;
                if (read_mem(addr + offsets::kWindowIsBlockedByLevel, blocked) && (blocked & 0x01U) != 0U) {
                    return false;
                }
                return true;
            }

            case OutlineCategory::Totem: {
                // ATotem::_totemState (+0x03A8): 0=Cleansed, 1=Dull, 2=Hex, 3=Boon
                std::uint8_t raw_state = 1U;
                if (read_mem(addr + offsets::kTotemState, raw_state)) {
                    if (raw_state == 0U || raw_state > 3U) {
                        if (out_totem_state != nullptr) {
                            *out_totem_state = TotemSubState::Cleansed;
                        }
                        return false;
                    }
                    if (out_totem_state != nullptr) {
                        *out_totem_state = static_cast<TotemSubState>(raw_state);
                    }
                } else if (out_totem_state != nullptr) {
                    *out_totem_state = TotemSubState::Dull;
                }
                if (std::isfinite(progress_pct) && progress_pct < 0.0f) {
                    return false;
                }
                return true;
            }

            case OutlineCategory::Searchable: {
                // ASearchable::_hasBeenSearched (+0x038C)
                std::uint8_t searched = 0;
                if (read_mem(addr + offsets::kSearchableHasBeenSearched, searched) && (searched & 0x01U) != 0U) {
                    return false;
                }
                return true;
            }

            case OutlineCategory::BearTrap: {
                // ABearTrap::_isBroken (+0x02CD)
                std::uint8_t broken = 0;
                if (read_mem(addr + offsets::kBearTrapIsBroken, broken) && (broken & 0x01U) != 0U) {
                    return false;
                }
                return true;
            }

            case OutlineCategory::Hatch:
            case OutlineCategory::EscapeDoor:
                return true;

            case OutlineCategory::Count:
            case OutlineCategory::None:
            default:
                return false;
        }
    }

    // O(1) Per-Actor Evaluation & Native Outline / CustomDepth Application.
    // Strictly excludes local_pawn (actor_addr == local_pawn) and automatically restores original state
    // if the category is toggled off, master toggle is off, the actor exceeds max_distance_m,
    // or the actor state becomes ineligible (e.g. destroyed pallet / cleansed totem / dead camper).
    template <typename ReadFn, typename WriteFn>
    void evaluate_actor(
        ReadFn&&                   read_mem,
        WriteFn&&                  write_mem,
        std::uintptr_t             actor_ptr,
        OutlineCategory            category,
        float                      distance_m,
        float                      progress_pct,
        std::uint64_t              current_tick,
        std::uintptr_t             guobjectarray_abs,
        const NativeOutlineConfig& cfg,
        std::uintptr_t             local_pawn = 0) noexcept {

        const std::uintptr_t clean_actor = untag_aarch64_ptr(actor_ptr);
        const std::uintptr_t clean_local = untag_aarch64_ptr(local_pawn);

        if (category == OutlineCategory::None ||
            static_cast<std::size_t>(category) >= static_cast<std::size_t>(OutlineCategory::Count) ||
            !is_canonical_aarch64_ptr(clean_actor)) {
            return;
        }

        // Strict Local Pawn Exclusion: NEVER apply UDBDOutlineComponent or UPrimitiveComponent overrides
        // to the local player's own pawn (clean_actor == clean_local).
        if (clean_local != 0 && clean_actor == clean_local) {
            if (ActorOutlineSlot* existing = find_existing_slot(clean_actor)) {
                if (existing->currently_overridden) {
                    restore_actor_state(read_mem, write_mem, *existing, guobjectarray_abs);
                }
                clear_slot(*existing);
            }
            return;
        }

        ActorOutlineSlot* slot = find_or_allocate_slot(clean_actor, category, current_tick);
        if (slot == nullptr) {
            return;
        }
        slot->last_seen_tick = current_tick;
        slot->category       = category;

        if (!is_uobject_alive(read_mem, clean_actor, guobjectarray_abs)) {
            clear_slot(*slot);
            return;
        }

        const CategoryOutlineFilter& cat_filter = cfg.filter(category);
        TotemSubState totem_sub = TotemSubState::Dull;

        const bool kindred_applies =
            cfg.kindred_mode &&
            (category == OutlineCategory::Slasher ||
             category == OutlineCategory::Camper ||
             category == OutlineCategory::Generator ||
             category == OutlineCategory::Totem ||
             category == OutlineCategory::Hatch);

        const float effective_max_dist_m = kindred_applies ? 128.0f : cat_filter.max_distance_m;

        const bool within_cull_distance =
            std::isfinite(distance_m) &&
            distance_m >= 0.0f &&
            (meters_to_ue4_units(distance_m) <= meters_to_ue4_units(effective_max_dist_m));

        const bool state_eligible = is_actor_state_eligible(
            read_mem, clean_actor, category, progress_pct, &totem_sub);

        const bool want_aura_visibility =
            cfg.master_enabled &&
            (cat_filter.enabled || kindred_applies) &&
            within_cull_distance &&
            state_eligible;

        const bool want_gen_progress_color =
            cfg.master_enabled &&
            (category == OutlineCategory::Generator) &&
            cfg.generator_dynamic_progress_color &&
            within_cull_distance &&
            state_eligible;

        const bool should_highlight = want_aura_visibility || want_gen_progress_color;

        if (!should_highlight) {
            if (slot->currently_overridden) {
                restore_actor_state(read_mem, write_mem, *slot, guobjectarray_abs);
            }
            return;
        }

        // Resolve UDBDOutlineComponent* and fallback UPrimitiveComponent* meshes
        if (!slot->components_resolved || (current_tick - slot->last_resolve_tick) >= 120ULL) {
            resolve_actor_outline_components(read_mem, *slot, guobjectarray_abs, current_tick);
        }

        // Capture clean baseline state before applying our first override
        if (!slot->baseline.captured) {
            capture_baseline_state(read_mem, *slot, guobjectarray_abs);
        }

        // Compute target FLinearColor for this actor
        LinearColor target_color = cat_filter.custom_color;
        if (category == OutlineCategory::Generator && cfg.generator_dynamic_progress_color) {
            target_color = compute_generator_progress_color(progress_pct);
        } else if (category == OutlineCategory::Totem) {
            target_color = compute_totem_state_color(totem_sub, cfg);
        } else if (category == OutlineCategory::Camper &&
                   cfg.obsession_actor_ptr != 0 &&
                   clean_actor == untag_aarch64_ptr(cfg.obsession_actor_ptr)) {
            target_color = cfg.obsession_color;
        }

        apply_actor_outline(
            read_mem,
            write_mem,
            *slot,
            target_color,
            want_aura_visibility,
            want_gen_progress_color,
            guobjectarray_abs);
    }

    // Prunes actors not seen in the current tick (e.g., despawned or out of collection range)
    // and cleanly restores any still-live actors that had active overrides.
    template <typename ReadFn, typename WriteFn>
    void prune_and_restore_stale(
        ReadFn&&       read_mem,
        WriteFn&&      write_mem,
        std::uint64_t  current_tick,
        std::uintptr_t guobjectarray_abs,
        std::uintptr_t current_ulevel = 0) noexcept {

        if (current_ulevel != 0 && current_ulevel != last_ulevel_) {
            // Level transition detected: clear cache without writing to torn-down ULevel actors
            last_ulevel_ = current_ulevel;
            reset_cache_without_writes();
            return;
        }

        for (ActorOutlineSlot& slot : slots_) {
            if (slot.actor_ptr == 0) {
                continue;
            }
            if (slot.last_seen_tick != current_tick) {
                if (slot.currently_overridden) {
                    restore_actor_state(read_mem, write_mem, slot, guobjectarray_abs);
                }
                if ((current_tick - slot.last_seen_tick) > 30ULL) {
                    clear_slot(slot);
                }
            }
        }
    }

    // Restores all currently overridden actors immediately (e.g. when master toggle is turned off)
    template <typename ReadFn, typename WriteFn>
    void restore_all(
        ReadFn&&       read_mem,
        WriteFn&&      write_mem,
        std::uintptr_t guobjectarray_abs) noexcept {

        for (ActorOutlineSlot& slot : slots_) {
            if (slot.actor_ptr != 0 && slot.currently_overridden) {
                restore_actor_state(read_mem, write_mem, slot, guobjectarray_abs);
            }
        }
    }

    constexpr void reset_cache_without_writes() noexcept {
        for (ActorOutlineSlot& slot : slots_) {
            clear_slot(slot);
        }
        active_overridden_count_ = 0;
    }

    [[nodiscard]] constexpr std::size_t active_overridden_count() const noexcept {
        return active_overridden_count_;
    }

private:
    std::array<ActorOutlineSlot, kMaxTrackedOutlineActors> slots_{};
    std::uintptr_t                                         last_ulevel_{0};
    std::size_t                                            active_overridden_count_{0};

    static constexpr void clear_slot(ActorOutlineSlot& slot) noexcept {
        slot = ActorOutlineSlot{};
    }

    [[nodiscard]] ActorOutlineSlot* find_existing_slot(std::uintptr_t actor_ptr) noexcept {
        const std::size_t base_idx = static_cast<std::size_t>((actor_ptr >> 4U) * 11400714819323198485ULL) &
                                     (kMaxTrackedOutlineActors - 1U);
        for (std::size_t step = 0; step < kMaxTrackedOutlineActors; ++step) {
            const std::size_t idx = (base_idx + step) & (kMaxTrackedOutlineActors - 1U);
            if (slots_[idx].actor_ptr == actor_ptr) {
                return &slots_[idx];
            }
        }
        return nullptr;
    }

    [[nodiscard]] ActorOutlineSlot* find_or_allocate_slot(
        std::uintptr_t  actor_ptr,
        OutlineCategory category,
        std::uint64_t   current_tick) noexcept {

        const std::size_t base_idx = static_cast<std::size_t>((actor_ptr >> 4U) * 11400714819323198485ULL) &
                                     (kMaxTrackedOutlineActors - 1U);

        std::size_t free_idx   = kMaxTrackedOutlineActors;
        std::size_t oldest_idx = base_idx;
        std::uint64_t oldest_tick = UINT64_MAX;

        for (std::size_t step = 0; step < kMaxTrackedOutlineActors; ++step) {
            const std::size_t idx = (base_idx + step) & (kMaxTrackedOutlineActors - 1U);
            ActorOutlineSlot& candidate = slots_[idx];
            if (candidate.actor_ptr == actor_ptr) {
                return &candidate;
            }
            if (candidate.actor_ptr == 0 && free_idx == kMaxTrackedOutlineActors) {
                free_idx = idx;
            } else if (candidate.last_seen_tick < oldest_tick) {
                oldest_tick = candidate.last_seen_tick;
                oldest_idx  = idx;
            }
        }

        const std::size_t target_idx = (free_idx < kMaxTrackedOutlineActors) ? free_idx : oldest_idx;
        ActorOutlineSlot& slot = slots_[target_idx];
        clear_slot(slot);
        slot.actor_ptr      = actor_ptr;
        slot.category       = category;
        slot.last_seen_tick = current_tick;
        return &slot;
    }

    // Protected memory write wrapper: verifies that the owning UObject is still alive,
    // the target field address is canonical and readable right before writing, and executes write_mem.
    template <typename ReadFn, typename WriteFn, typename T>
    static bool safe_verified_write(
        ReadFn&&       read_mem,
        WriteFn&&      write_mem,
        std::uintptr_t obj_base,
        std::uintptr_t field_offset,
        const T&       value,
        std::uintptr_t guobjectarray_abs) noexcept {

        const std::uintptr_t clean_base = untag_aarch64_ptr(obj_base);
        const std::uintptr_t target_addr = clean_base + field_offset;
        if (!is_canonical_aarch64_ptr(clean_base) || !is_canonical_aarch64_ptr(target_addr)) {
            return false;
        }
        if (!is_uobject_alive(read_mem, clean_base, guobjectarray_abs)) {
            return false;
        }
        T probe{};
        if (!read_mem(target_addr, probe)) {
            return false;
        }
        return write_mem(target_addr, value);
    }

    // Strictly validates that a candidate UObject* is an initialized, live UDBDOutlineComponent:
    // 1. Both comp_addr and its UObject::ClassPrivate (+0x0010) must be live UObjects.
    // 2. +0x0380 (bIsOutlineInitialized) MUST be true (== 1U).
    // 3. +0x0388 (_renderingStrategy) MUST be a valid canonical pointer and a live UObject.
    // 4. Universal scalar fields (+0x02B0, +0x02B4..+0x02BD, +0x0328) must be within exact UE4 ranges.
    template <typename ReadFn>
    [[nodiscard]] static bool is_verified_dbd_outline_component(
        ReadFn&&       read_mem,
        std::uintptr_t comp_addr,
        std::uintptr_t guobjectarray_abs) noexcept {

        const std::uintptr_t clean_comp = untag_aarch64_ptr(comp_addr);
        if (!is_uobject_alive(read_mem, clean_comp, guobjectarray_abs)) {
            return false;
        }

        std::uintptr_t class_private = 0;
        if (!read_mem(clean_comp + offsets::kUObjectClassPrivate, class_private) ||
            !is_uobject_alive(read_mem, class_private, guobjectarray_abs)) {
            return false;
        }

        std::uint8_t   init_flag    = 0U;
        std::uintptr_t render_strat = 0;
        if (!read_mem(clean_comp + offsets::kOutlineIsInitialized, init_flag) ||
            !read_mem(clean_comp + offsets::kOutlineRenderingStrategyPtr, render_strat)) {
            return false;
        }

        // Strict requirement: +0x0380 (bIsOutlineInitialized) MUST be true (1U)
        // and +0x0388 (_renderingStrategy) MUST be a canonical, live object pointer.
        if (init_flag != 1U) {
            return false;
        }
        const std::uintptr_t clean_strat = untag_aarch64_ptr(render_strat);
        std::uintptr_t strat_vtable = 0;
        if (!is_canonical_aarch64_ptr(clean_strat) ||
            !read_mem(clean_strat, strat_vtable) ||
            !is_canonical_aarch64_ptr(untag_aarch64_ptr(strat_vtable))) {
            return false;
        }

        float        interp_speed  = 0.0f;
        float        min_dist      = -1.0f;
        float        fade_intens   = -1.0f;
        std::uint8_t flag_above    = 255U;
        std::uint8_t flag_limit_cd = 255U;
        std::uint8_t flag_fade_in  = 255U;
        std::uint8_t flag_always   = 255U;
        std::uint8_t flag_event    = 255U;
        std::uint8_t flag_far      = 255U;

        if (!read_mem(clean_comp + offsets::kOutlineInterpolationSpeed, interp_speed) ||
            !read_mem(clean_comp + offsets::kOutlineMinimumOutlineDistance, min_dist) ||
            !read_mem(clean_comp + offsets::kOutlineFadeIntensity, fade_intens) ||
            !read_mem(clean_comp + offsets::kOutlineShouldBeAboveOutlines, flag_above) ||
            !read_mem(clean_comp + offsets::kOutlineLimitToCustomDepthObjects, flag_limit_cd) ||
            !read_mem(clean_comp + offsets::kOutlineFadeOutAsClosingIn, flag_fade_in) ||
            !read_mem(clean_comp + offsets::kOutlineIsAlwaysVisible, flag_always) ||
            !read_mem(clean_comp + offsets::kOutlineIsUseEventStrategy, flag_event) ||
            !read_mem(clean_comp + offsets::kOutlineForceOutlineFarAway, flag_far)) {
            return false;
        }

        if (!std::isfinite(interp_speed) || interp_speed < 0.0f || interp_speed > 200.0f) {
            return false;
        }
        if (!std::isfinite(min_dist) || min_dist < 0.0f || min_dist > 100000.0f) {
            return false;
        }
        if (!std::isfinite(fade_intens) || fade_intens < 0.0f || fade_intens > 50.0f) {
            return false;
        }
        if (flag_above > 1U || flag_limit_cd > 1U || flag_fade_in > 1U ||
            flag_always > 1U || flag_event > 1U || flag_far > 1U) {
            return false;
        }

        return true;
    }

    // Inspects a candidate UObject* to resolve UDBDOutlineComponent (either directly or via
    // UOutlineUpdateStrategy::_outlineComponent at +0x00B8).
    // Strictly verifies UObjectBase::OuterPrivate (+0x0028) == slot.actor_ptr to reject stale/freed
    // TSparseArray slots in OwnedComponents (+0x01C0), and NEVER collects or writes to raw UPrimitiveComponent
    // offsets (+0x0228/+0x023C/+0x0248) which can point past 0x00C0-byte UActorComponents into adjacent heap objects.
    template <typename ReadFn>
    static void inspect_candidate_component(
        ReadFn&&          read_mem,
        std::uintptr_t    comp_ptr,
        ActorOutlineSlot& slot,
        std::uintptr_t    guobjectarray_abs) noexcept {

        const std::uintptr_t clean_comp = untag_aarch64_ptr(comp_ptr);
        if (!is_uobject_alive(read_mem, clean_comp, guobjectarray_abs)) {
            return;
        }

        // Verify that the component belongs to this Actor via UObjectBase::OuterPrivate (+0x0028)
        std::uintptr_t outer_ptr = 0;
        if (read_mem(clean_comp + offsets::kUObjectOuterPrivate, outer_ptr)) {
            const std::uintptr_t clean_outer = untag_aarch64_ptr(outer_ptr);
            if (clean_outer != slot.actor_ptr) {
                return;
            }
        } else {
            return;
        }

        // 1. Check if this component itself is a verified UDBDOutlineComponent
        if (is_verified_dbd_outline_component(read_mem, clean_comp, guobjectarray_abs)) {
            slot.outline_comp_ptr = clean_comp;
            return;
        }

        // 2. Check if this component is an UOutlineUpdateStrategy holding _outlineComponent at +0x00B8
        //    (read-only discovery; we never write to strategy subclass offsets)
        std::uintptr_t strategy_outline_cand = 0;
        if (read_mem(clean_comp + offsets::kStrategyOutlineComponentPtr, strategy_outline_cand)) {
            const std::uintptr_t clean_out = untag_aarch64_ptr(strategy_outline_cand);
            if (is_verified_dbd_outline_component(read_mem, clean_out, guobjectarray_abs)) {
                std::uintptr_t out_outer = 0;
                if (read_mem(clean_out + offsets::kUObjectOuterPrivate, out_outer) &&
                    untag_aarch64_ptr(out_outer) == slot.actor_ptr) {
                    slot.strategy_comp_ptr = clean_comp;
                    slot.outline_comp_ptr  = clean_out;
                    return;
                }
            }
        }
    }

    template <typename ReadFn>
    [[nodiscard]] static bool looks_like_primitive_component(
        ReadFn&&       read_mem,
        std::uintptr_t comp_addr,
        std::uintptr_t guobjectarray_abs) noexcept {

        const std::uintptr_t clean_comp = untag_aarch64_ptr(comp_addr);
        std::uintptr_t class_private = 0;
        if (!read_mem(clean_comp + offsets::kUObjectClassPrivate, class_private) ||
            !is_uobject_alive(read_mem, class_private, guobjectarray_abs)) {
            return false;
        }

        std::uintptr_t scene_proxy  = 0;
        std::uint32_t  flags_230    = 0;
        std::int32_t   stencil_val  = -1;

        if (!read_mem(clean_comp + offsets::kPrimitiveSceneProxy, scene_proxy) ||
            !read_mem(clean_comp + offsets::kPrimitiveFlagsDword0, flags_230) ||
            !read_mem(clean_comp + offsets::kPrimitiveCustomDepthStencilValue, stencil_val)) {
            return false;
        }

        if (!is_canonical_aarch64_ptr(scene_proxy)) {
            return false;
        }
        if (stencil_val < 0 || stencil_val > 255) {
            return false;
        }
        return (flags_230 & (offsets::kMaskRenderInMainPass | offsets::kMaskRenderInDepthPass)) != 0U;
    }

    static void add_fallback_mesh(ActorOutlineSlot& slot, std::uintptr_t mesh_addr) noexcept {
        for (std::size_t i = 0; i < slot.mesh_count; ++i) {
            if (slot.meshes[i].mesh_ptr == mesh_addr) {
                return;
            }
        }
        if (slot.mesh_count < kMaxFallbackMeshes) {
            slot.meshes[slot.mesh_count++].mesh_ptr = mesh_addr;
        }
    }

    template <typename ReadFn>
    static void scan_tarray_components(
        ReadFn&&          read_mem,
        std::uintptr_t    tarray_addr,
        ActorOutlineSlot& slot,
        std::uintptr_t    guobjectarray_abs,
        std::size_t       max_elems = 32) noexcept {

        std::uintptr_t data_ptr = 0;
        std::int32_t   count    = 0;
        std::int32_t   max_cap  = 0;
        if (!read_mem(tarray_addr, data_ptr) ||
            !read_mem(tarray_addr + 0x08ULL, count) ||
            !read_mem(tarray_addr + 0x0CULL, max_cap)) {
            return;
        }
        const std::uintptr_t clean_data = untag_aarch64_ptr(data_ptr);
        if (!is_canonical_aarch64_ptr(clean_data) || count <= 0 || max_cap < count || count > 256) {
            return;
        }
        const std::size_t bounded = (static_cast<std::size_t>(count) < max_elems)
            ? static_cast<std::size_t>(count)
            : max_elems;

        for (std::size_t i = 0; i < bounded; ++i) {
            std::uintptr_t elem_ptr = 0;
            if (read_mem(clean_data + (i * sizeof(std::uintptr_t)), elem_ptr)) {
                inspect_candidate_component(read_mem, elem_ptr, slot, guobjectarray_abs);
            }
        }
    }

    // Resolves UDBDOutlineComponent* and fallback UPrimitiveComponent* meshes
    // for any ADBDPlayer or AInteractable in O(1) bounded steps.
    template <typename ReadFn>
    static void resolve_actor_outline_components(
        ReadFn&&          read_mem,
        ActorOutlineSlot& slot,
        std::uintptr_t    guobjectarray_abs,
        std::uint64_t     current_tick) noexcept {

        slot.last_resolve_tick = current_tick;
        const std::uintptr_t actor = slot.actor_ptr;

        // 1. Direct category-specific pointers (e.g. ATotem::_totemOutlineUpdateStrategy at +0x0450)
        if (slot.category == OutlineCategory::Totem) {
            std::uintptr_t totem_strat = 0;
            if (read_mem(actor + offsets::kTotemOutlineUpdateStrategy, totem_strat)) {
                inspect_candidate_component(read_mem, totem_strat, slot, guobjectarray_abs);
            }
        } else if (slot.category == OutlineCategory::Window) {
            std::uintptr_t win_col = 0;
            if (read_mem(actor + offsets::kWindowCollider, win_col)) {
                inspect_candidate_component(read_mem, win_col, slot, guobjectarray_abs);
            }
        } else if (slot.category == OutlineCategory::Hatch) {
            std::uintptr_t hatch_col = 0;
            if (read_mem(actor + offsets::kHatchBaseCollision, hatch_col)) {
                inspect_candidate_component(read_mem, hatch_col, slot, guobjectarray_abs);
            }
        }

        // 2. Traverse RootComponent (+0x0140) and its AttachChildren (+0x00E0) + 1 level of child AttachChildren
        std::uintptr_t root_comp = 0;
        if (read_mem(actor + offsets::kActorRootComponent, root_comp)) {
            const std::uintptr_t clean_root = untag_aarch64_ptr(root_comp);
            if (is_uobject_alive(read_mem, clean_root, guobjectarray_abs)) {
                inspect_candidate_component(read_mem, clean_root, slot, guobjectarray_abs);

                std::uintptr_t children_data = 0;
                std::int32_t   children_cnt  = 0;
                if (read_mem(clean_root + offsets::kSceneComponentAttachChildren, children_data) &&
                    read_mem(clean_root + offsets::kSceneComponentAttachChildren + 0x08ULL, children_cnt)) {
                    const std::uintptr_t clean_children = untag_aarch64_ptr(children_data);
                    if (is_canonical_aarch64_ptr(clean_children) && children_cnt > 0 && children_cnt <= 64) {
                        const std::size_t bound = (children_cnt < 24) ? static_cast<std::size_t>(children_cnt) : 24U;
                        for (std::size_t i = 0; i < bound; ++i) {
                            std::uintptr_t child_ptr = 0;
                            if (read_mem(clean_children + (i * sizeof(std::uintptr_t)), child_ptr)) {
                                inspect_candidate_component(read_mem, child_ptr, slot, guobjectarray_abs);
                                const std::uintptr_t clean_child = untag_aarch64_ptr(child_ptr);
                                if (is_canonical_aarch64_ptr(clean_child)) {
                                    scan_tarray_components(
                                        read_mem,
                                        clean_child + offsets::kSceneComponentAttachChildren,
                                        slot,
                                        guobjectarray_abs,
                                        16);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2b. For Camper / Slasher characters, also inspect ACharacter::Mesh (+0x0288 / +0x0290) and its AttachChildren
        if (slot.outline_comp_ptr == 0 &&
            (slot.category == OutlineCategory::Camper || slot.category == OutlineCategory::Slasher)) {
            constexpr std::array<std::uintptr_t, 2> kMeshOffsets = {
                offsets::kCharacterSkeletalMeshPrimary,
                offsets::kCharacterSkeletalMeshSecondary
            };
            for (std::uintptr_t m_off : kMeshOffsets) {
                std::uintptr_t char_mesh = 0;
                if (read_mem(actor + m_off, char_mesh)) {
                    const std::uintptr_t clean_mesh = untag_aarch64_ptr(char_mesh);
                    if (is_uobject_alive(read_mem, clean_mesh, guobjectarray_abs)) {
                        inspect_candidate_component(read_mem, clean_mesh, slot, guobjectarray_abs);
                        scan_tarray_components(
                            read_mem,
                            clean_mesh + offsets::kSceneComponentAttachChildren,
                            slot,
                            guobjectarray_abs,
                            24);
                    }
                }
            }
        }

        // 3. Scan AActor::BlueprintCreatedComponents (+0x0210) & AActor::InstanceComponents (+0x0200)
        if (slot.outline_comp_ptr == 0) {
            scan_tarray_components(read_mem, actor + offsets::kActorBlueprintCreatedComponents, slot, guobjectarray_abs, 32);
        }
        if (slot.outline_comp_ptr == 0) {
            scan_tarray_components(read_mem, actor + offsets::kActorInstanceComponents, slot, guobjectarray_abs, 32);
        }

        // 4. Scan AActor::OwnedComponents (+0x01C0 TSet<UActorComponent*>: Data +0x1C0, ArrayNum +0x1C8, ArrayMax +0x1CC, stride 0x10)
        if (slot.outline_comp_ptr == 0) {
            std::uintptr_t set_data  = 0;
            std::int32_t   set_count = 0;
            std::int32_t   set_max   = 0;
            if (read_mem(actor + offsets::kActorOwnedComponents, set_data) &&
                read_mem(actor + offsets::kActorOwnedComponents + 0x08ULL, set_count) &&
                read_mem(actor + offsets::kActorOwnedComponents + 0x0CULL, set_max)) {
                const std::uintptr_t clean_set = untag_aarch64_ptr(set_data);
                if (is_canonical_aarch64_ptr(clean_set) && set_count > 0 && set_max >= set_count && set_max <= 256) {
                    const std::size_t bound = (set_max < 64) ? static_cast<std::size_t>(set_max) : 64U;
                    for (std::size_t i = 0; i < bound; ++i) {
                        std::uintptr_t comp_ptr = 0;
                        if (read_mem(clean_set + (i * 0x10ULL), comp_ptr) && is_canonical_aarch64_ptr(comp_ptr)) {
                            inspect_candidate_component(read_mem, comp_ptr, slot, guobjectarray_abs);
                            if (slot.outline_comp_ptr != 0) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        slot.components_resolved = (slot.outline_comp_ptr != 0 || slot.mesh_count > 0);
    }

    // Captures the actor's original UDBDOutlineComponent universal fields and UPrimitiveComponent state
    // before any telemetry highlight is applied, ensuring 100% lossless restoration.
    template <typename ReadFn>
    static void capture_baseline_state(
        ReadFn&&          read_mem,
        ActorOutlineSlot& slot,
        std::uintptr_t    guobjectarray_abs) noexcept {

        OriginalOutlineSnapshot& base = slot.baseline;
        if (slot.outline_comp_ptr != 0 &&
            is_verified_dbd_outline_component(read_mem, slot.outline_comp_ptr, guobjectarray_abs)) {
            const std::uintptr_t oc = slot.outline_comp_ptr;
            (void)read_mem(oc + offsets::kOutlineShouldBeAboveOutlines,     base.should_be_above_2b4);
            (void)read_mem(oc + offsets::kOutlineLimitToCustomDepthObjects, base.limit_custom_depth_2b5);
            (void)read_mem(oc + offsets::kOutlineFadeOutAsClosingIn,        base.fade_out_closing_in_2b6);
            (void)read_mem(oc + offsets::kOutlineIsAlwaysVisible,           base.is_always_visible_2b7);
            (void)read_mem(oc + offsets::kOutlineMinimumOutlineDistance,    base.min_outline_dist_2b8);
            (void)read_mem(oc + offsets::kOutlineIsUseEventStrategy,        base.is_use_event_strategy_2bc);
            (void)read_mem(oc + offsets::kOutlineForceOutlineFarAway,       base.force_far_away_2bd);
            (void)read_mem(oc + offsets::kOutlineCurrentShaderColor,        base.shader_color_2d8);
            (void)read_mem(oc + offsets::kOutlinePreviousColor,             base.previous_color_2e8);
            (void)read_mem(oc + offsets::kOutlineInterpolationAlpha,        base.interp_alpha_2fc);
            (void)read_mem(oc + offsets::kOutlineCurrentTargetColor,        base.current_target_color_300);
            (void)read_mem(oc + offsets::kOutlineTargetColor,               base.target_color_314);
            (void)read_mem(oc + offsets::kOutlineFadeIntensity,             base.fade_intensity_328);
        }

        if (slot.category == OutlineCategory::Generator &&
            is_uobject_alive(read_mem, slot.actor_ptr, guobjectarray_abs)) {
            (void)read_mem(
                slot.actor_ptr + offsets::kGeneratorForceRevealToLocalPlayer,
                base.generator_force_reveal_36d);
            if (slot.strategy_comp_ptr != 0 &&
                is_uobject_alive(read_mem, slot.strategy_comp_ptr, guobjectarray_abs)) {
                if (read_mem(
                        slot.strategy_comp_ptr + offsets::kGenStrategyUnrepairedColor,
                        base.gen_unrepaired_color_138)) {
                    base.gen_strategy_captured = true;
                }
            }
        }

        for (std::size_t i = 0; i < slot.mesh_count; ++i) {
            FallbackMeshBaseline& mb = slot.meshes[i];
            if (mb.mesh_ptr != 0 && is_uobject_alive(read_mem, mb.mesh_ptr, guobjectarray_abs)) {
                if (read_mem(mb.mesh_ptr + offsets::kPrimitiveFlagsDword0, mb.flags_dword_230) &&
                    read_mem(mb.mesh_ptr + offsets::kPrimitiveCustomDepthFlagsDword, mb.custom_depth_dword_23c) &&
                    read_mem(mb.mesh_ptr + offsets::kPrimitiveCustomDepthStencilValue, mb.stencil_value_248) &&
                    read_mem(mb.mesh_ptr + offsets::kPrimitiveCachedMaxDrawDistance, mb.cached_max_draw_dist_228)) {
                    mb.captured = true;
                }
            }
        }

        base.captured = true;
    }

    // Applies the Native UE4 Outline configuration to the resolved UDBDOutlineComponent.
    // Separates Aura Visibility flags (+0x02B6..+0x02BD, +0x036D) from Generator Progress Color
    // material/strategy parameter writes (UGeneratorOutlineUpdateStrategy::_unrepairedAuraColor @ +0x0138
    // and UDBDOutlineComponent shader/target color parameters @ +0x02D8..+0x0314).
    template <typename ReadFn, typename WriteFn>
    void apply_actor_outline(
        ReadFn&&           read_mem,
        WriteFn&&          write_mem,
        ActorOutlineSlot&  slot,
        const LinearColor& target_color,
        bool               want_aura_visibility,
        bool               want_gen_progress_color,
        std::uintptr_t     guobjectarray_abs) noexcept {

        bool applied_any = false;

        // 0a. Generator Progress Color Parameter (Separate from Aura Visibility flag):
        //     Writes to UGeneratorOutlineUpdateStrategy::_unrepairedAuraColor (+0x0138, FPropertyParams @ 0x0EC2AFC8)
        if (slot.category == OutlineCategory::Generator &&
            slot.strategy_comp_ptr != 0 &&
            is_uobject_alive(read_mem, slot.strategy_comp_ptr, guobjectarray_abs)) {
            if (want_gen_progress_color) {
                LinearColor cur_strat_col{};
                if (read_mem(slot.strategy_comp_ptr + offsets::kGenStrategyUnrepairedColor, cur_strat_col) &&
                    !cur_strat_col.approx_equals(target_color)) {
                    (void)safe_verified_write(
                        read_mem,
                        write_mem,
                        slot.strategy_comp_ptr,
                        offsets::kGenStrategyUnrepairedColor,
                        target_color,
                        guobjectarray_abs);
                }
                applied_any = true;
            } else if (slot.baseline.gen_strategy_captured) {
                LinearColor cur_strat_col{};
                if (read_mem(slot.strategy_comp_ptr + offsets::kGenStrategyUnrepairedColor, cur_strat_col) &&
                    !cur_strat_col.approx_equals(slot.baseline.gen_unrepaired_color_138)) {
                    (void)safe_verified_write(
                        read_mem,
                        write_mem,
                        slot.strategy_comp_ptr,
                        offsets::kGenStrategyUnrepairedColor,
                        slot.baseline.gen_unrepaired_color_138,
                        guobjectarray_abs);
                }
            }
        }

        // 0b. Native AGenerator::ForceRevealToLocalPlayer (+0x036D) property (Aura Visibility Flag ONLY)
        if (slot.category == OutlineCategory::Generator &&
            is_uobject_alive(read_mem, slot.actor_ptr, guobjectarray_abs)) {
            std::uint8_t gen_act = 255;
            std::uint8_t gen_rep = 255;
            std::uint8_t cur_gen_reveal = 255;
            if (read_mem(slot.actor_ptr + offsets::kGeneratorActivated, gen_act) &&
                read_mem(slot.actor_ptr + offsets::kGeneratorIsRepaired, gen_rep) &&
                read_mem(slot.actor_ptr + offsets::kGeneratorForceRevealToLocalPlayer, cur_gen_reveal) &&
                gen_act <= 1U && gen_rep <= 1U && cur_gen_reveal <= 1U) {
                const std::uint8_t desired_reveal = want_aura_visibility
                    ? static_cast<std::uint8_t>(1U)
                    : slot.baseline.generator_force_reveal_36d;
                if (cur_gen_reveal != desired_reveal) {
                    (void)safe_verified_write(
                        read_mem,
                        write_mem,
                        slot.actor_ptr,
                        offsets::kGeneratorForceRevealToLocalPlayer,
                        desired_reveal,
                        guobjectarray_abs);
                }
                if (want_aura_visibility) {
                    applied_any = true;
                }
            }
        }

        // 1. Primary Path: Verified UDBDOutlineComponent universal offsets
        if (slot.outline_comp_ptr != 0 &&
            is_verified_dbd_outline_component(read_mem, slot.outline_comp_ptr, guobjectarray_abs)) {
            const std::uintptr_t oc = slot.outline_comp_ptr;

            std::uint8_t cur_fade_close = 1;
            std::uint8_t cur_always_vis = 0;
            float        cur_min_dist   = 448.0f;
            std::uint8_t cur_force_far  = 0;
            LinearColor  cur_shader{};
            LinearColor  cur_target{};

            (void)read_mem(oc + offsets::kOutlineFadeOutAsClosingIn,     cur_fade_close);
            (void)read_mem(oc + offsets::kOutlineIsAlwaysVisible,        cur_always_vis);
            (void)read_mem(oc + offsets::kOutlineMinimumOutlineDistance, cur_min_dist);
            (void)read_mem(oc + offsets::kOutlineForceOutlineFarAway,    cur_force_far);
            (void)read_mem(oc + offsets::kOutlineCurrentShaderColor,     cur_shader);
            (void)read_mem(oc + offsets::kOutlineTargetColor,            cur_target);

            if (want_aura_visibility) {
                if (cur_fade_close != 0U) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineFadeOutAsClosingIn, static_cast<std::uint8_t>(0U), guobjectarray_abs);
                }
                if (cur_always_vis != 1U) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineIsAlwaysVisible, static_cast<std::uint8_t>(1U), guobjectarray_abs);
                }
                if (cur_min_dist > 1.0f) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineMinimumOutlineDistance, 0.0f, guobjectarray_abs);
                }
                if (cur_force_far != 1U) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineForceOutlineFarAway, static_cast<std::uint8_t>(1U), guobjectarray_abs);
                }
            } else if (slot.baseline.captured) {
                if (cur_always_vis != slot.baseline.is_always_visible_2b7) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineIsAlwaysVisible, slot.baseline.is_always_visible_2b7, guobjectarray_abs);
                }
                if (cur_force_far != slot.baseline.force_far_away_2bd) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineForceOutlineFarAway, slot.baseline.force_far_away_2bd, guobjectarray_abs);
                }
            }

            if (want_aura_visibility || want_gen_progress_color) {
                if (!cur_target.approx_equals(target_color) || !cur_shader.approx_equals(target_color)) {
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineCurrentShaderColor, target_color, guobjectarray_abs);
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlinePreviousColor, target_color, guobjectarray_abs);
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineInterpolationAlpha, 0.5f, guobjectarray_abs);
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineCurrentTargetColor, target_color, guobjectarray_abs);
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineTargetColor, target_color, guobjectarray_abs);
                    (void)safe_verified_write(
                        read_mem, write_mem, oc, offsets::kOutlineFadeIntensity, 1.0f, guobjectarray_abs);
                }
                applied_any = true;
            }
        }

        slot.last_applied_color = target_color;
        if (applied_any && !slot.currently_overridden) {
            slot.currently_overridden = true;
            ++active_overridden_count_;
        }
    }

    // Clean State Restore: Reverts UDBDOutlineComponent universal fields to their exact pre-override values.
    template <typename ReadFn, typename WriteFn>
    void restore_actor_state(
        ReadFn&&          read_mem,
        WriteFn&&         write_mem,
        ActorOutlineSlot& slot,
        std::uintptr_t    guobjectarray_abs) noexcept {

        if (!slot.currently_overridden) {
            return;
        }

        slot.currently_overridden = false;
        if (active_overridden_count_ > 0) {
            --active_overridden_count_;
        }

        if (!slot.baseline.captured || !is_uobject_alive(read_mem, slot.actor_ptr, guobjectarray_abs)) {
            return;
        }

        const OriginalOutlineSnapshot& base = slot.baseline;

        // 0a. Restore UGeneratorOutlineUpdateStrategy::_unrepairedAuraColor (+0x0138)
        if (slot.category == OutlineCategory::Generator &&
            base.gen_strategy_captured &&
            slot.strategy_comp_ptr != 0 &&
            is_uobject_alive(read_mem, slot.strategy_comp_ptr, guobjectarray_abs)) {
            (void)safe_verified_write(
                read_mem,
                write_mem,
                slot.strategy_comp_ptr,
                offsets::kGenStrategyUnrepairedColor,
                base.gen_unrepaired_color_138,
                guobjectarray_abs);
        }

        // 0b. Restore AGenerator::ForceRevealToLocalPlayer (+0x036D)
        if (slot.category == OutlineCategory::Generator && base.generator_force_reveal_36d <= 1U) {
            std::uint8_t cur_gen_reveal = 255;
            if (read_mem(slot.actor_ptr + offsets::kGeneratorForceRevealToLocalPlayer, cur_gen_reveal) &&
                cur_gen_reveal <= 1U) {
                (void)safe_verified_write(
                    read_mem,
                    write_mem,
                    slot.actor_ptr,
                    offsets::kGeneratorForceRevealToLocalPlayer,
                    base.generator_force_reveal_36d,
                    guobjectarray_abs);
            }
        }

        // 1. Restore verified UDBDOutlineComponent universal fields
        if (slot.outline_comp_ptr != 0 &&
            is_verified_dbd_outline_component(read_mem, slot.outline_comp_ptr, guobjectarray_abs)) {
            const std::uintptr_t oc = slot.outline_comp_ptr;
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineFadeOutAsClosingIn, base.fade_out_closing_in_2b6, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineIsAlwaysVisible, base.is_always_visible_2b7, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineMinimumOutlineDistance, base.min_outline_dist_2b8, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineForceOutlineFarAway, base.force_far_away_2bd, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineFadeIntensity, base.fade_intensity_328, guobjectarray_abs);

            const LinearColor restore_target = (base.is_always_visible_2b7 != 0U)
                ? base.target_color_314
                : LinearColor{0.0f, 0.0f, 0.0f, 0.0f};
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineCurrentShaderColor, restore_target, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlinePreviousColor, restore_target, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineInterpolationAlpha, 0.5f, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineCurrentTargetColor, restore_target, guobjectarray_abs);
            (void)safe_verified_write(
                read_mem, write_mem, oc, offsets::kOutlineTargetColor, restore_target, guobjectarray_abs);
        }
    }
};

// Singleton controller instance for the 60 Hz diagnostic worker thread
[[nodiscard]] inline NativeOutlineController& get_native_outline_controller() noexcept {
    static NativeOutlineController s_controller{};
    return s_controller;
}

} // namespace hud::outline

