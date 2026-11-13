# Path: ida95_dbdm_suite/__init__.py

from .dbdm_config import UE4Offsets, SignatureSpec, DEFAULT_SIGNATURES, DBDM_KNOWN_CLASSES
from .ui_timing_controller import (
    ActorContext,
    ComponentLifecycleRegistry,
    ProductionTimingController,
    RejectionReason,
    ResolutionOutcome,
    ResultType,
    TimingEventDriver,
    TimingEventInstance,
    UICheckboxBinding,
    run_stage7_acceptance,
)
def __getattr__(name: str):
    if name in ("USkillCheckOffsets", "USkillCheckRVAs", "run_stage7_Verification"):
        from . import inspect_skillcheck
        return getattr(inspect_skillcheck, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

try:
    from .arm64_resolver import ARM64Resolver
    from .ida95_types import IDA95TypeBuilder
    from .ue4_analyzer import UE4DBDMAnalyzer
    from .sdk_exporter import DBDMSDKExporter
except ImportError:
    ARM64Resolver = None  # type: ignore[assignment,misc]
    IDA95TypeBuilder = None  # type: ignore[assignment,misc]
    UE4DBDMAnalyzer = None  # type: ignore[assignment,misc]
    DBDMSDKExporter = None  # type: ignore[assignment,misc]

__all__ = [
    "UE4Offsets",
    "SignatureSpec",
    "DEFAULT_SIGNATURES",
    "DBDM_KNOWN_CLASSES",
    "ARM64Resolver",
    "IDA95TypeBuilder",
    "UE4DBDMAnalyzer",
    "DBDMSDKExporter",
    "ActorContext",
    "ComponentLifecycleRegistry",
    "ProductionTimingController",
    "RejectionReason",
    "ResolutionOutcome",
    "ResultType",
    "TimingEventDriver",
    "TimingEventInstance",
    "UICheckboxBinding",
    "run_stage7_acceptance",
    "USkillCheckOffsets",
    "USkillCheckRVAs",
    "run_stage7_Verification",
]
