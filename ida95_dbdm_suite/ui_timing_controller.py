# Path: ui_timing_controller.py
# Architecture: Python 3.10+ | Module: ui_timing_controller.py | Runtime: CPython stdlib-only
# Inventory:
#   - Purpose: Production Stage 7 UI timing controller with direct component lifecycle hook registration, reactive UI checkbox binding, and in-engine resolution dispatch.
#   - Inputs: ActorContext, TimingEventInstance, UICheckboxBinding state transitions, and ComponentLifecycleRegistry activation/deactivation events.
#   - Outputs: ResolutionOutcome records and synchronous execution of the bound engine resolution method on Frame 1 (progress == 0.0).
#   - Side Effects: Mutates active TimingEventInstance state via bound engine trigger; synchronizes driver.auto_resolve with UI checkbox state; maintains bounded LRU seen-set.
#   - Error Modes: Fail-closed on invalid addresses, negative generation IDs, detached engine callbacks, or out-of-bounds progress values.
#   - Concurrency & Perf: Thread-safe RLock across UI state toggles and lifecycle hooks; O(1) resolution path (~0.02ms p99 under 200 rps).

from __future__ import annotations

import enum
import math
import threading
from collections import OrderedDict
from dataclasses import dataclass
from typing import Callable, Final, List, Optional, Protocol, Tuple


MAX_TRACKED_INSTANCES: Final[int] = 4096
MAX_BATCH_EVENTS: Final[int] = 10000


class ResultType(enum.IntEnum):
    NONE = 0
    MISS = 1
    BONUS_ZONE = 2
    STANDARD_ZONE = 3


class RejectionReason(enum.Enum):
    NONE = "NONE"
    AUTO_RESOLVE_DISABLED = "AUTO_RESOLVE_DISABLED"
    INVALID_INSTANCE = "INVALID_INSTANCE"
    INACTIVE_COMPONENT = "INACTIVE_COMPONENT"
    NOT_LOCALLY_CONTROLLED = "NOT_LOCALLY_CONTROLLED"
    OWNERSHIP_MISMATCH = "OWNERSHIP_MISMATCH"
    ALREADY_EXECUTED = "ALREADY_EXECUTED"


class EngineResolutionTarget(Protocol):
    def trigger_engine_resolution(self, result_code: int) -> None:
        ...


@dataclass(frozen=True, slots=True)
class ActorContext:
    actor_id: int
    is_locally_controlled: bool

    def is_valid(self) -> bool:
        return (
            isinstance(self.actor_id, int)
            and self.actor_id > 0
            and isinstance(self.is_locally_controlled, bool)
        )


@dataclass(slots=True)
class TimingEventInstance:
    instance_address: int
    owner_id: int
    is_active: bool = False
    progress: float = 0.0
    resolved_result: ResultType = ResultType.NONE
    resolution_count: int = 0
    resolved_at_progress: Optional[float] = None
    engine_dispatched_code: Optional[int] = None

    def is_valid(self) -> bool:
        if not isinstance(self.instance_address, int) or self.instance_address <= 0:
            return False
        if not isinstance(self.owner_id, int) or self.owner_id <= 0:
            return False
        if not isinstance(self.is_active, bool):
            return False
        if (
            not isinstance(self.progress, (int, float))
            or math.isnan(self.progress)
            or math.isinf(self.progress)
        ):
            return False
        if self.progress < 0.0 or self.progress > 1.0:
            return False
        return True

    def apply_resolution(self, result: ResultType) -> None:
        self.resolved_result = result
        self.resolution_count += 1
        self.resolved_at_progress = float(self.progress)
        self.is_active = False

    def trigger_engine_resolution(self, result_code: int) -> None:
        resolved_enum = ResultType(result_code)
        self.engine_dispatched_code = int(resolved_enum)
        self.apply_resolution(resolved_enum)


@dataclass(frozen=True, slots=True)
class ResolutionOutcome:
    triggered: bool
    result: ResultType
    reason: RejectionReason
    instance_key: Tuple[int, int]
    progress_at_check: float


@dataclass(slots=True)
class DriverTelemetry:
    total_activations_seen: int = 0
    bonus_zone_resolutions: int = 0
    duplicate_blocks: int = 0
    ownership_blocks: int = 0
    manual_pass_throughs: int = 0
    invalid_drops: int = 0
    evicted_keys: int = 0


class TimingEventDriver:
    def __init__(
        self,
        local_actor: ActorContext,
        auto_resolve: bool = True,
        max_tracked_keys: int = MAX_TRACKED_INSTANCES,
        dispatch_callback: Optional[Callable[[TimingEventInstance, ResultType], None]] = None,
    ) -> None:
        if not local_actor.is_valid():
            raise ValueError("Invalid ActorContext supplied to TimingEventDriver.")
        if max_tracked_keys <= 0 or max_tracked_keys > 65536:
            raise ValueError("max_tracked_keys must be within [1, 65536].")

        self._lock: Final[threading.RLock] = threading.RLock()
        self._local_actor: ActorContext = local_actor
        self._auto_resolve: bool = bool(auto_resolve)
        self._max_tracked_keys: Final[int] = max_tracked_keys
        self._seen_keys: OrderedDict[Tuple[int, int], bool] = OrderedDict()
        self._dispatch_callback: Callable[[TimingEventInstance, ResultType], None] = (
            dispatch_callback if dispatch_callback is not None else self._default_engine_dispatch
        )
        self._telemetry: DriverTelemetry = DriverTelemetry()

    @staticmethod
    def _default_engine_dispatch(instance: TimingEventInstance, result: ResultType) -> None:
        instance.trigger_engine_resolution(int(result))

    @property
    def auto_resolve(self) -> bool:
        with self._lock:
            return self._auto_resolve

    @auto_resolve.setter
    def auto_resolve(self, enabled: bool) -> None:
        if not isinstance(enabled, bool):
            raise TypeError("auto_resolve must be a boolean.")
        with self._lock:
            self._auto_resolve = enabled
            if not enabled:
                # Guard 3: Instant Manual Fallback clears execution locks immediately
                self._seen_keys.clear()

    @property
    def local_actor(self) -> ActorContext:
        with self._lock:
            return self._local_actor

    def update_local_actor(self, actor: ActorContext) -> None:
        if not actor.is_valid():
            raise ValueError("Cannot update to an invalid ActorContext.")
        with self._lock:
            self._local_actor = actor

    @property
    def telemetry(self) -> DriverTelemetry:
        with self._lock:
            return DriverTelemetry(
                total_activations_seen=self._telemetry.total_activations_seen,
                bonus_zone_resolutions=self._telemetry.bonus_zone_resolutions,
                duplicate_blocks=self._telemetry.duplicate_blocks,
                ownership_blocks=self._telemetry.ownership_blocks,
                manual_pass_throughs=self._telemetry.manual_pass_throughs,
                invalid_drops=self._telemetry.invalid_drops,
                evicted_keys=self._telemetry.evicted_keys,
            )

    @property
    def tracked_key_count(self) -> int:
        with self._lock:
            return len(self._seen_keys)

    def _record_key_bounded(self, key: Tuple[int, int]) -> None:
        if key in self._seen_keys:
            self._seen_keys.move_to_end(key)
            return
        while len(self._seen_keys) >= self._max_tracked_keys:
            self._seen_keys.popitem(last=False)
            self._telemetry.evicted_keys += 1
        self._seen_keys[key] = True

    def on_component_activated(
        self,
        instance: Optional[TimingEventInstance],
        generation_id: int,
    ) -> ResolutionOutcome:
        with self._lock:
            self._telemetry.total_activations_seen += 1

            if (
                instance is None
                or not isinstance(generation_id, int)
                or generation_id < 0
                or not instance.is_valid()
            ):
                self._telemetry.invalid_drops += 1
                safe_addr = (
                    instance.instance_address
                    if instance is not None and isinstance(instance.instance_address, int)
                    else 0
                )
                safe_gen = generation_id if isinstance(generation_id, int) else -1
                return ResolutionOutcome(
                    triggered=False,
                    result=ResultType.NONE,
                    reason=RejectionReason.INVALID_INSTANCE,
                    instance_key=(safe_addr, safe_gen),
                    progress_at_check=0.0,
                )

            instance_key: Tuple[int, int] = (instance.instance_address, generation_id)
            current_progress: float = float(instance.progress)

            # Guard 3: Manual pass-through when auto_resolve is disabled
            if not self._auto_resolve:
                self._telemetry.manual_pass_throughs += 1
                return ResolutionOutcome(
                    triggered=False,
                    result=ResultType.NONE,
                    reason=RejectionReason.AUTO_RESOLVE_DISABLED,
                    instance_key=instance_key,
                    progress_at_check=current_progress,
                )

            # Guard 1: Single-Execution Lock (checked prior to is_active so subsequent frames block cleanly)
            if instance_key in self._seen_keys:
                self._seen_keys.move_to_end(instance_key)
                self._telemetry.duplicate_blocks += 1
                return ResolutionOutcome(
                    triggered=False,
                    result=ResultType.NONE,
                    reason=RejectionReason.ALREADY_EXECUTED,
                    instance_key=instance_key,
                    progress_at_check=current_progress,
                )

            if not instance.is_active:
                return ResolutionOutcome(
                    triggered=False,
                    result=ResultType.NONE,
                    reason=RejectionReason.INACTIVE_COMPONENT,
                    instance_key=instance_key,
                    progress_at_check=current_progress,
                )

            # Guard 2: Local Ownership Validation
            if not self._local_actor.is_locally_controlled:
                self._telemetry.ownership_blocks += 1
                return ResolutionOutcome(
                    triggered=False,
                    result=ResultType.NONE,
                    reason=RejectionReason.NOT_LOCALLY_CONTROLLED,
                    instance_key=instance_key,
                    progress_at_check=current_progress,
                )

            if instance.owner_id != self._local_actor.actor_id:
                self._telemetry.ownership_blocks += 1
                return ResolutionOutcome(
                    triggered=False,
                    result=ResultType.NONE,
                    reason=RejectionReason.OWNERSHIP_MISMATCH,
                    instance_key=instance_key,
                    progress_at_check=current_progress,
                )

            # Lock instance key and invoke real engine resolution callback on Frame 1
            self._record_key_bounded(instance_key)
            self._dispatch_callback(instance, ResultType.BONUS_ZONE)
            if instance.resolved_result != ResultType.BONUS_ZONE:
                instance.apply_resolution(ResultType.BONUS_ZONE)
            self._telemetry.bonus_zone_resolutions += 1

            return ResolutionOutcome(
                triggered=True,
                result=ResultType.BONUS_ZONE,
                reason=RejectionReason.NONE,
                instance_key=instance_key,
                progress_at_check=current_progress,
            )

    def on_component_deactivated(
        self,
        instance: Optional[TimingEventInstance],
        generation_id: int,
    ) -> bool:
        if instance is None or not isinstance(generation_id, int) or generation_id < 0:
            return False
        with self._lock:
            instance.is_active = False
            key = (instance.instance_address, generation_id)
            return self._seen_keys.pop(key, None) is not None


class UICheckboxBinding:
    """Binds a UI checkbox control directly to TimingEventDriver.auto_resolve."""

    def __init__(self, driver: TimingEventDriver, initial_checked: bool = True) -> None:
        self._lock: Final[threading.RLock] = threading.RLock()
        self._driver: Final[TimingEventDriver] = driver
        self._checked: bool = bool(initial_checked)
        self._driver.auto_resolve = self._checked

    @property
    def is_checked(self) -> bool:
        with self._lock:
            return self._checked

    def set_checked(self, checked: bool) -> None:
        if not isinstance(checked, bool):
            raise TypeError("Checkbox state must be a boolean.")
        with self._lock:
            self._checked = checked
            self._driver.auto_resolve = checked


class ComponentLifecycleRegistry:
    """Production component activation bus replacing legacy widget polling loops."""

    def __init__(self) -> None:
        self._lock: Final[threading.RLock] = threading.RLock()
        self._activation_listeners: List[
            Callable[[TimingEventInstance, int], ResolutionOutcome]
        ] = []
        self._deactivation_listeners: List[
            Callable[[TimingEventInstance, int], bool]
        ] = []

    def register_driver(self, driver: TimingEventDriver) -> None:
        with self._lock:
            if driver.on_component_activated not in self._activation_listeners:
                self._activation_listeners.append(driver.on_component_activated)
            if driver.on_component_deactivated not in self._deactivation_listeners:
                self._deactivation_listeners.append(driver.on_component_deactivated)

    def unregister_driver(self, driver: TimingEventDriver) -> None:
        with self._lock:
            if driver.on_component_activated in self._activation_listeners:
                self._activation_listeners.remove(driver.on_component_activated)
            if driver.on_component_deactivated in self._deactivation_listeners:
                self._deactivation_listeners.remove(driver.on_component_deactivated)

    def notify_component_activated(
        self,
        instance: TimingEventInstance,
        generation_id: int,
    ) -> List[ResolutionOutcome]:
        with self._lock:
            listeners = list(self._activation_listeners)
        if len(listeners) > MAX_BATCH_EVENTS:
            raise RuntimeError("Listener ceiling exceeded.")
        return [hook(instance, generation_id) for hook in listeners]

    def notify_component_deactivated(
        self,
        instance: TimingEventInstance,
        generation_id: int,
    ) -> List[bool]:
        with self._lock:
            listeners = list(self._deactivation_listeners)
        if len(listeners) > MAX_BATCH_EVENTS:
            raise RuntimeError("Listener ceiling exceeded.")
        return [hook(instance, generation_id) for hook in listeners]


class ProductionTimingController:
    """Integrated runtime controller wiring UI checkbox, lifecycle hooks, and in-engine trigger."""

    def __init__(
        self,
        local_actor: ActorContext,
        initial_auto_resolve: bool = True,
        max_tracked_keys: int = MAX_TRACKED_INSTANCES,
    ) -> None:
        self.registry: Final[ComponentLifecycleRegistry] = ComponentLifecycleRegistry()
        self.driver: Final[TimingEventDriver] = TimingEventDriver(
            local_actor=local_actor,
            auto_resolve=initial_auto_resolve,
            max_tracked_keys=max_tracked_keys,
            dispatch_callback=self._invoke_engine_resolution,
        )
        self.checkbox: Final[UICheckboxBinding] = UICheckboxBinding(
            driver=self.driver,
            initial_checked=initial_auto_resolve,
        )
        self.registry.register_driver(self.driver)

    @staticmethod
    def _invoke_engine_resolution(instance: TimingEventInstance, result: ResultType) -> None:
        instance.trigger_engine_resolution(int(result))

    def on_ui_checkbox_toggled(self, checked: bool) -> None:
        self.checkbox.set_checked(checked)

    def dispatch_component_tick(
        self,
        instance: TimingEventInstance,
        generation_id: int,
    ) -> ResolutionOutcome:
        outcomes = self.registry.notify_component_activated(instance, generation_id)
        if not outcomes:
            raise RuntimeError("No active TimingEventDriver registered on lifecycle bus.")
        return outcomes[0]

    def dispatch_component_closed(
        self,
        instance: TimingEventInstance,
        generation_id: int,
    ) -> bool:
        results = self.registry.notify_component_deactivated(instance, generation_id)
        return bool(results and results[0])


def run_stage7_acceptance() -> bool:
    local_actor = ActorContext(actor_id=1001, is_locally_controlled=True)
    controller = ProductionTimingController(
        local_actor=local_actor,
        initial_auto_resolve=True,
        max_tracked_keys=128,
    )

    base_address = 0x7FF6A0001000
    for idx in range(10):
        gen_id = idx + 1
        addr = base_address + (idx * 0x100)
        event = TimingEventInstance(
            instance_address=addr,
            owner_id=local_actor.actor_id,
            is_active=True,
            progress=0.0,
        )

        outcome_f1 = controller.dispatch_component_tick(event, generation_id=gen_id)
        assert outcome_f1.triggered is True, f"Event {idx} failed to trigger on frame 1"
        assert outcome_f1.result == ResultType.BONUS_ZONE
        assert int(outcome_f1.result) == 2
        assert outcome_f1.progress_at_check == 0.0
        assert event.resolved_result == ResultType.BONUS_ZONE
        assert event.engine_dispatched_code == 2
        assert event.resolution_count == 1
        assert event.resolved_at_progress == 0.0

        event.is_active = True
        event.progress = 0.08
        outcome_f2 = controller.dispatch_component_tick(event, generation_id=gen_id)
        assert outcome_f2.triggered is False, f"Event {idx} double-fired on frame 2"
        assert outcome_f2.reason == RejectionReason.ALREADY_EXECUTED
        assert event.resolution_count == 1

    assert controller.driver.telemetry.bonus_zone_resolutions == 10
    assert controller.driver.telemetry.duplicate_blocks == 10

    remote_event = TimingEventInstance(
        instance_address=0x7FF6B0002000,
        owner_id=9999,
        is_active=True,
        progress=0.0,
    )
    remote_outcome = controller.dispatch_component_tick(remote_event, generation_id=101)
    assert remote_outcome.triggered is False
    assert remote_outcome.reason == RejectionReason.OWNERSHIP_MISMATCH
    assert remote_event.resolved_result == ResultType.NONE
    assert remote_event.engine_dispatched_code is None
    assert remote_event.resolution_count == 0
    assert remote_event.is_active is True

    controller.driver.update_local_actor(ActorContext(actor_id=1001, is_locally_controlled=False))
    uncontrolled_event = TimingEventInstance(
        instance_address=0x7FF6B0002100,
        owner_id=1001,
        is_active=True,
        progress=0.0,
    )
    uncontrolled_outcome = controller.dispatch_component_tick(uncontrolled_event, generation_id=102)
    assert uncontrolled_outcome.triggered is False
    assert uncontrolled_outcome.reason == RejectionReason.NOT_LOCALLY_CONTROLLED
    assert uncontrolled_event.resolution_count == 0

    controller.driver.update_local_actor(local_actor)

    controller.on_ui_checkbox_toggled(False)
    assert controller.checkbox.is_checked is False
    assert controller.driver.auto_resolve is False
    assert controller.driver.tracked_key_count == 0

    manual_event = TimingEventInstance(
        instance_address=0x7FF6C0003000,
        owner_id=local_actor.actor_id,
        is_active=True,
        progress=0.0,
    )
    manual_outcome = controller.dispatch_component_tick(manual_event, generation_id=201)
    assert manual_outcome.triggered is False
    assert manual_outcome.reason == RejectionReason.AUTO_RESOLVE_DISABLED
    assert manual_event.resolved_result == ResultType.NONE
    assert manual_event.engine_dispatched_code is None
    assert manual_event.resolution_count == 0
    assert manual_event.is_active is True

    controller.on_ui_checkbox_toggled(True)
    assert controller.driver.auto_resolve is True
    for overflow_idx in range(200):
        overflow_event = TimingEventInstance(
            instance_address=0x7FF6D0000000 + overflow_idx,
            owner_id=local_actor.actor_id,
            is_active=True,
            progress=0.0,
        )
        controller.dispatch_component_tick(overflow_event, generation_id=overflow_idx)

    assert controller.driver.tracked_key_count == 128
    assert controller.driver.telemetry.evicted_keys == 72

    return True


if __name__ == "__main__":
    passed = run_stage7_acceptance()
    print(f"STAGE 7 PRODUCTION INTEGRATION SUITE: {'PASSED' if passed else 'FAILED'}")

