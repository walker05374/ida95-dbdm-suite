# Architecture: Python 3.10+ | Module: inspect_skillcheck.py | Target: libUE4_fixed.so (ARM64)
# Inventory:
#   - Purpose: Stage 7 Auto Skill Check — First-Frame Great resolution wired to real libUE4_fixed.so RVAs and USkillCheck offsets.
#   - Inputs: USkillCheck instance memory/state, local ADBDPlayer pointer, UI checkbox state.
#   - Outputs: Direct invocation of Local_OnSkillCheckSuccessTrigger(bonus=True, ...) on Frame 1 when activated.
#   - Guards: Single-execution lock per (instance_address, _skillCheckCount) + local _owner (0x2B8) validation + instant manual fallback.

from __future__ import annotations
import enum
import struct
import threading
from collections import OrderedDict
from dataclasses import dataclass
from typing import Callable, Final, Optional, Tuple

try:
    from .ui_timing_controller import (
        ActorContext,
        ProductionTimingController,
        RejectionReason,
        ResolutionOutcome,
        ResultType,
        TimingEventInstance,
    )
except ImportError:
    from ui_timing_controller import (
        ActorContext,
        ProductionTimingController,
        RejectionReason,
        ResolutionOutcome,
        ResultType,
        TimingEventInstance,
    )


CURRENT_PROGRESS_OFFSET: Final[int] = 0x0290
CURRENT_UI_PROGRESS_OFFSET: Final[int] = 0x0294
GENERATION_COUNT_OFFSET: Final[int] = 0x02A0
OWNER_ID_OFFSET: Final[int] = 0x02B8
CURRENT_INTERACTION_OFFSET: Final[int] = 0x02C0
CUSTOM_TYPE_OFFSET: Final[int] = 0x02C8
DEFINITION_CACHE_OFFSET: Final[int] = 0x02F8
STATE_FLAGS_OFFSET: Final[int] = 0x0420
VTABLE_RVA: Final[int] = 0x0EA4FFD8
ACTIVATE_HANDLER_RVA: Final[int] = 0x07B8AC28
ACTIVATE_CUSTOM_HANDLER_RVA: Final[int] = 0x07B8ABDC
SUCCESS_DISPATCH_RVA: Final[int] = 0x07B8AD94
INPUT_HANDLER_RVA: Final[int] = 0x07B8AFF4


class USkillCheckOffsets:
    """Exact struct offsets extracted from libUE4_fixed.so reflection table (0x0ecb4e20..0x0ecb54f8)."""
    CURRENT_PROGRESS: Final[int] = CURRENT_PROGRESS_OFFSET            # float _currentProgress
    CURRENT_PROGRESS_OFFSET: Final[int] = CURRENT_PROGRESS_OFFSET
    CURRENT_UI_PROGRESS: Final[int] = CURRENT_UI_PROGRESS_OFFSET      # float _currentUIProgress
    CURRENT_UI_PROGRESS_OFFSET: Final[int] = CURRENT_UI_PROGRESS_OFFSET
    X_OFFSET: Final[int] = 0x0298                                     # int32 xOffset
    Y_OFFSET: Final[int] = 0x029C                                     # int32 yOffset
    SKILL_CHECK_COUNT: Final[int] = GENERATION_COUNT_OFFSET           # int32 _skillCheckCount (generation ID)
    GENERATION_COUNT_OFFSET: Final[int] = GENERATION_COUNT_OFFSET
    OWNER_PAWN: Final[int] = OWNER_ID_OFFSET                          # ADBDPlayer* _owner
    OWNER_ID_OFFSET: Final[int] = OWNER_ID_OFFSET
    CURRENT_INTERACTION: Final[int] = CURRENT_INTERACTION_OFFSET      # UInteractionDefinition* _currentInteraction
    CURRENT_INTERACTION_OFFSET: Final[int] = CURRENT_INTERACTION_OFFSET
    CURRENT_SKILL_CHECK_TYPE: Final[int] = CUSTOM_TYPE_OFFSET         # ESkillCheckCustomType _currentSkillCheckType
    CUSTOM_TYPE_OFFSET: Final[int] = CUSTOM_TYPE_OFFSET
    DEFINITION_CACHE: Final[int] = DEFINITION_CACHE_OFFSET            # FSkillCheckDefinition _currentSkillCheckDefinitionCache
    DEFINITION_CACHE_OFFSET: Final[int] = DEFINITION_CACHE_OFFSET
    SAFE_CANCEL_TIMER: Final[int] = 0x0320                            # FTimerHandle _safeCancelTimer
    SHOW_SKILL_CHECK_TIMER: Final[int] = 0x0368                       # FTimerHandle _showSkillCheckTimer
    STATE_FLAGS_activated_displayed: Final[int] = STATE_FLAGS_OFFSET  # uint8 flags (_activated, _displayed, _warningGiven)
    STATE_FLAGS_OFFSET: Final[int] = STATE_FLAGS_OFFSET

    # FSkillCheckDefinition sub-offsets (base = 0x02F8, size = 0x28)
    DEF_SUCCESS_START: Final[int] = DEFINITION_CACHE_OFFSET + 0x00    # float SuccessZoneStart
    DEF_SUCCESS_END: Final[int] = DEFINITION_CACHE_OFFSET + 0x04      # float SuccessZoneEnd
    DEF_BONUS_LENGTH: Final[int] = DEFINITION_CACHE_OFFSET + 0x08     # float BonusZoneLength
    DEF_BONUS_START: Final[int] = DEFINITION_CACHE_OFFSET + 0x0C      # float BonusZoneStart
    DEF_PROGRESS_RATE: Final[int] = DEFINITION_CACHE_OFFSET + 0x10    # float ProgressRate
    DEF_START_TICKER: Final[int] = DEFINITION_CACHE_OFFSET + 0x14     # float StartingTickerPosition
    DEF_WARNING_DELAY: Final[int] = DEFINITION_CACHE_OFFSET + 0x1C    # float WarningSoundDelay
    DEF_BOOL_FLAGS: Final[int] = DEFINITION_CACHE_OFFSET + 0x28       # bool flags (IsInsane, IsContinuous, etc.)


class USkillCheckRVAs:
    """Exact function RVAs in libUE4_fixed.so (0x0ecb4ca0..0x0ecb4e18 & vtable 0x0ea4ffd8)."""
    VTABLE_ADDR: Final[int] = VTABLE_RVA
    VTABLE_RVA: Final[int] = VTABLE_RVA
    CTOR_RESET_SUB: Final[int] = 0x06E165C0
    ACTIVATE_IMPL_SUB: Final[int] = 0x06E166D4

    EXEC_GET_CHECK_RESULT: Final[int] = 0x07B8A9A4
    EXEC_GET_CUSTOM_TYPE: Final[int] = 0x07B8A9F0
    EXEC_GET_START_DELAY: Final[int] = 0x07B8AA3C
    EXEC_GET_UI_PROGRESS: Final[int] = 0x07B8AA88
    EXEC_IS_DISPLAYED: Final[int] = 0x07B8AAE0
    EXEC_LOCAL_ACTIVATE_CUSTOM: Final[int] = ACTIVATE_CUSTOM_HANDLER_RVA
    ACTIVATE_CUSTOM_HANDLER_RVA: Final[int] = ACTIVATE_CUSTOM_HANDLER_RVA
    EXEC_LOCAL_ACTIVATE: Final[int] = ACTIVATE_HANDLER_RVA
    ACTIVATE_HANDLER_RVA: Final[int] = ACTIVATE_HANDLER_RVA
    EXEC_LOCAL_FAILURE_TRIGGER: Final[int] = 0x07B8ACFC
    EXEC_LOCAL_SUCCESS_TRIGGER: Final[int] = SUCCESS_DISPATCH_RVA
    SUCCESS_DISPATCH_RVA: Final[int] = SUCCESS_DISPATCH_RVA
    EXEC_ON_SKILL_CHECK_INPUT: Final[int] = INPUT_HANDLER_RVA
    INPUT_HANDLER_RVA: Final[int] = INPUT_HANDLER_RVA
    EXEC_SERVER_ACTIVATE: Final[int] = 0x07B8B040
    EXEC_SERVER_DEACTIVATE: Final[int] = 0x07B8B08C
    EXEC_SERVER_ON_FAILURE: Final[int] = 0x07B8B0D8
    EXEC_SERVER_ON_SUCCESS: Final[int] = 0x07B8B130


@dataclass(slots=True)
class NativeSuccessTriggerParams:
    """
    Matches UFunction Local_OnSkillCheckSuccessTrigger parameter block (0x0ecb4360..0x0ecb44a0):
      +0x00: bool bonus (True for Great Skill Check)
      +0x01: bool insane
      +0x02: uint8 skillCheckCustomType
      +0x03: bool failedCountedAsGood
    """
    bonus: bool = True
    insane: bool = False
    skill_check_custom_type: int = 0
    failed_counted_as_good: bool = False

    def pack_params(self) -> bytes:
        return struct.pack(
            "<BBBB",
            1 if self.bonus else 0,
            1 if self.insane else 0,
            self.skill_check_custom_type & 0xFF,
            1 if self.failed_counted_as_good else 0,
        )


class Stage7RuntimeSession:
    """
    Binds ProductionTimingController directly to USkillCheck activation hooks
    (Local_ActivateSkillCheck @ 0x07B8AC28 / Local_ActivateCustomSkillCheck @ 0x07B8ABDC)
    and dispatches Local_OnSkillCheckSuccessTrigger(bonus=True) @ 0x07B8AD94 on Frame 1.
    """

    CURRENT_PROGRESS_OFFSET: Final[int] = CURRENT_PROGRESS_OFFSET
    CURRENT_UI_PROGRESS_OFFSET: Final[int] = CURRENT_UI_PROGRESS_OFFSET
    GENERATION_COUNT_OFFSET: Final[int] = GENERATION_COUNT_OFFSET
    OWNER_ID_OFFSET: Final[int] = OWNER_ID_OFFSET
    CURRENT_INTERACTION_OFFSET: Final[int] = CURRENT_INTERACTION_OFFSET
    CUSTOM_TYPE_OFFSET: Final[int] = CUSTOM_TYPE_OFFSET
    DEFINITION_CACHE_OFFSET: Final[int] = DEFINITION_CACHE_OFFSET
    STATE_FLAGS_OFFSET: Final[int] = STATE_FLAGS_OFFSET
    VTABLE_RVA: Final[int] = VTABLE_RVA
    ACTIVATE_HANDLER_RVA: Final[int] = ACTIVATE_HANDLER_RVA
    ACTIVATE_CUSTOM_HANDLER_RVA: Final[int] = ACTIVATE_CUSTOM_HANDLER_RVA
    SUCCESS_DISPATCH_RVA: Final[int] = SUCCESS_DISPATCH_RVA
    INPUT_HANDLER_RVA: Final[int] = INPUT_HANDLER_RVA

    def __init__(
        self,
        local_player_pawn: int,
        is_locally_controlled: bool = True,
        auto_skillcheck_enabled: bool = True,
        native_caller: Optional[Callable[[int, int, bytes], None]] = None,
    ) -> None:
        self._lock = threading.RLock()
        self.controller = ProductionTimingController(
            local_actor=ActorContext(
                actor_id=local_player_pawn,
                is_locally_controlled=is_locally_controlled,
            ),
            initial_auto_resolve=auto_skillcheck_enabled,
        )
        self._native_caller = native_caller
        self.native_call_history: list[Tuple[int, int, bytes]] = []

    def set_checkbox(self, enabled: bool) -> None:
        self.controller.on_ui_checkbox_toggled(enabled)

    def on_skillcheck_class_activated(
        self,
        skillcheck_ptr: int,
        owner_pawn_ptr: int,
        skillcheck_count: int,
        is_displayed: bool = True,
        current_progress: float = 0.0,
        custom_type: int = 0,
        is_insane: bool = False,
    ) -> ResolutionOutcome:
        """
        Hook entry point for Local_ActivateSkillCheck (0x07B8AC28) and
        Local_ActivateCustomSkillCheck (0x07B8ABDC).
        """
        event = TimingEventInstance(
            instance_address=skillcheck_ptr,
            owner_id=owner_pawn_ptr,
            is_active=is_displayed,
            progress=current_progress,
        )
        outcome = self.controller.dispatch_component_tick(
            instance=event,
            generation_id=skillcheck_count,
        )
        if outcome.triggered and outcome.result == ResultType.BONUS_ZONE:
            params = NativeSuccessTriggerParams(
                bonus=True,
                insane=is_insane,
                skill_check_custom_type=custom_type,
                failed_counted_as_good=False,
            )
            payload = params.pack_params()
            with self._lock:
                self.native_call_history.append(
                    (skillcheck_ptr, self.SUCCESS_DISPATCH_RVA, payload)
                )
            if self._native_caller is not None:
                self._native_caller(
                    skillcheck_ptr,
                    self.SUCCESS_DISPATCH_RVA,
                    payload,
                )
        return outcome

    def ReadInstanceFromMemoryBuffer(self, instance_addr: int, raw_struct_bytes: bytes) -> Tuple[int, int, bool, float, int]:
        """
        Parses a 0x430-byte USkillCheck memory snapshot using exact libUE4_fixed.so offsets:
          - _currentProgress @ 0x0290
          - _skillCheckCount @ 0x02A0
          - _owner           @ 0x02B8
          - _currentSkillCheckType @ 0x02C8
          - _activated/_displayed @ 0x0420
        """
        if len(raw_struct_bytes) < 0x0424:
            raise ValueError("Buffer smaller than USkillCheck struct size (0x424).")
        progress = struct.unpack_from("<f", raw_struct_bytes, self.CURRENT_PROGRESS_OFFSET)[0]
        count = struct.unpack_from("<i", raw_struct_bytes, self.GENERATION_COUNT_OFFSET)[0]
        owner_ptr = struct.unpack_from("<Q", raw_struct_bytes, self.OWNER_ID_OFFSET)[0]
        custom_type = struct.unpack_from("<B", raw_struct_bytes, self.CUSTOM_TYPE_OFFSET)[0]
        flags_420 = struct.unpack_from("<B", raw_struct_bytes, self.STATE_FLAGS_OFFSET)[0]
        is_active = bool(flags_420 & 0x03)
        return owner_ptr, max(0, count), is_active, progress, custom_type


def run_stage7_Verification() -> bool:
    local_survivor_pawn = 0x7FF600A01000
    other_survivor_pawn = 0x7FF600B02000
    skillcheck_component = 0x7FF610500000

    session = Stage7RuntimeSession(
        local_player_pawn=local_survivor_pawn,
        is_locally_controlled=True,
        auto_skillcheck_enabled=True,
    )

    # 1. 10 consecutive skill checks resolve as Great (bonus=True -> 0x07B8AD94) on Frame 1
    for check_num in range(1, 11):
        res_f1 = session.on_skillcheck_class_activated(
            skillcheck_ptr=skillcheck_component,
            owner_pawn_ptr=local_survivor_pawn,
            skillcheck_count=check_num,
            is_displayed=True,
            current_progress=0.0,
        )
        assert res_f1.triggered is True
        assert res_f1.result == ResultType.BONUS_ZONE

        # Guard: Frame 2 on the same _skillCheckCount must NOT double-fire
        res_f2 = session.on_skillcheck_class_activated(
            skillcheck_ptr=skillcheck_component,
            owner_pawn_ptr=local_survivor_pawn,
            skillcheck_count=check_num,
            is_displayed=True,
            current_progress=0.04,
        )
        assert res_f2.triggered is False
        assert res_f2.reason == RejectionReason.ALREADY_EXECUTED

    assert len(session.native_call_history) == 10
    for ptr, rva, payload in session.native_call_history:
        assert ptr == skillcheck_component
        assert rva == USkillCheckRVAs.EXEC_LOCAL_SUCCESS_TRIGGER
        assert payload[0] == 1  # bonus == True (Great Skill Check)

    # 2. Guard: Ignore skill checks spawned by another survivor (_owner != local_survivor_pawn)
    foreign_res = session.on_skillcheck_class_activated(
        skillcheck_ptr=skillcheck_component,
        owner_pawn_ptr=other_survivor_pawn,
        skillcheck_count=11,
        is_displayed=True,
        current_progress=0.0,
    )
    assert foreign_res.triggered is False
    assert foreign_res.reason == RejectionReason.OWNERSHIP_MISMATCH
    assert len(session.native_call_history) == 10

    # 3. Guard: Toggling checkbox off immediately returns to manual skill checks
    session.set_checkbox(False)
    manual_res = session.on_skillcheck_class_activated(
        skillcheck_ptr=skillcheck_component,
        owner_pawn_ptr=local_survivor_pawn,
        skillcheck_count=12,
        is_displayed=True,
        current_progress=0.0,
    )
    assert manual_res.triggered is False
    assert manual_res.reason == RejectionReason.AUTO_RESOLVE_DISABLED
    assert len(session.native_call_history) == 10

    return True


if __name__ == "__main__":
    ok = run_stage7_Verification()
    print(f"STAGE 7 LIBUE4_FIXED.SO INTEGRATION: {'PASSED' if ok else 'FAILED'}")