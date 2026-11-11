# Path: ida95_dbdm_suite/dbdm_config.py
# Runtime: Python 3.11+ / IDA Pro 9.x (ida_typeinf, ida_bytes, ida_segment)

from dataclasses import dataclass
from typing import Dict, List


@dataclass(frozen=True)
class SignatureSpec:
    name: str
    pattern: str
    section_hint: str
    is_pointer_ref: bool = True
    static_rva_fallback: int = 0


@dataclass(frozen=True)
class UE4Offsets:
    # FNamePool & FNameEntry (UE 4.27 DBDM com WITH_CASE_PRESERVING_NAME = 1 — Grau A T12)
    fname_pool_current_block_offset: int = 0x38
    fname_pool_current_cursor_offset: int = 0x3C
    fname_pool_blocks_offset: int = 0x40
    fname_stride: int = 0x04
    fname_entry_comparison_id_offset: int = 0x00
    fname_entry_header_offset: int = 0x04
    fname_entry_header_size: int = 0x06

    # FUObjectArray (0x10BF7920) & FChunkedFixedUObjectArray (0x10BF7930 — Grau A T8/T11)
    fuobject_item_size: int = 0x18
    tuobject_array_objects_offset: int = 0x10
    tuobject_array_preallocated_offset: int = 0x18
    tuobject_array_max_elements_offset: int = 0x20
    tuobject_array_num_elements_offset: int = 0x24
    tuobject_array_max_chunks_offset: int = 0x28
    tuobject_array_num_chunks_offset: int = 0x2C

    # UObjectBase (sizeof = 0x30 com FName de 12 bytes — Grau A T8)
    uobject_flags_offset: int = 0x08
    uobject_internal_index_offset: int = 0x0C
    uobject_class_private_offset: int = 0x10
    uobject_name_private_offset: int = 0x18
    uobject_name_display_offset: int = 0x1C
    uobject_name_number_offset: int = 0x20
    uobject_outer_private_offset: int = 0x28

    # UField / UStruct / UFunction (deslocados +0x08 por sizeof(UObjectBase) = 0x30)
    ufield_next_offset: int = 0x30
    ustruct_super_struct_offset: int = 0x48
    ustruct_children_offset: int = 0x50
    ustruct_child_properties_offset: int = 0x58
    ustruct_properties_size_offset: int = 0x60
    ustruct_min_alignment_offset: int = 0x64
    ufunction_flags_offset: int = 0xB8
    ufunction_func_offset: int = 0xE0

    # FField / FProperty (sizeof(FField) = 0x38 com FName de 12 bytes)
    ffield_class_offset: int = 0x08
    ffield_owner_offset: int = 0x10
    ffield_next_offset: int = 0x20
    ffield_name_offset: int = 0x28
    ffield_name_display_offset: int = 0x2C
    fproperty_array_dim_offset: int = 0x38
    fproperty_element_size_offset: int = 0x3C
    fproperty_property_flags_offset: int = 0x40
    fproperty_offset_internal_offset: int = 0x4C


DEFAULT_SIGNATURES: List[SignatureSpec] = [
    SignatureSpec(
        name="GNames",
        pattern="F5 2E 03 B0 F3 03 01 AA F4 03 00 AA B5 02 04 91 C8 00 00 37",
        section_hint=".text",
        is_pointer_ref=True,
        static_rva_fallback=0x10B7C100,
    ),
    SignatureSpec(
        name="GUObjectArray",
        pattern="20 78 02 B0 00 E4 42 F9 E1 03 13 AA E2 03 1F 2A",
        section_hint=".text",
        is_pointer_ref=True,
        static_rva_fallback=0x10BF7920,
    ),
    SignatureSpec(
        name="GWorld",
        pattern="E8 54 05 F0 08 21 42 F9 15 01 40 F9",
        section_hint=".text",
        is_pointer_ref=True,
        static_rva_fallback=0x10D69998,
    ),
    SignatureSpec(
        name="GEngine",
        pattern="48 A5 01 B0 08 55 41 F9 E1 03 00 AA 22 00 80 52 08 01 40 F9",
        section_hint=".text",
        is_pointer_ref=True,
        static_rva_fallback=0x10D65798,
    ),
    SignatureSpec(
        name="ProcessEvent",
        pattern="FA 67 BB A9 F8 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 7B 04 A9 FD 03 01 91 FF C3 02 D1 57 D0 3B D5 E8 16 40 F9 CA 78 02 90",
        section_hint=".text",
        is_pointer_ref=False,
        static_rva_fallback=0x0A750080,
    ),
    SignatureSpec(
        name="StaticFindObject",
        pattern="FF 83 04 D1 FC 6F 0C A9 FA 67 0D A9 F8 5F 0E A9 F6 57 0F A9 F4 4F 10 A9 FD 7B 11 A9 FD 43 04 91 58 D0 3B D5 08 17 40 F9 F9 03 01 AA 0A 25 03 90",
        section_hint=".text",
        is_pointer_ref=False,
        static_rva_fallback=0x0A755694,
    ),
]

DBDM_KNOWN_CLASSES: Dict[str, str] = {
    "ADBDPlayer": "Core survivor/killer base player pawn",
    "ASlasherPlayer": "Killer character class",
    "ACamperPlayer": "Survivor character class",
    "AGenerator": "Repairable objective generator",
    "APallet": "Interactable pallet obstacle",
    "AEscapeDoor": "Exit gate actor",
    "AHatch": "Escape hatch actor",
    "UDBDGameInstance": "Root game instance state",
}

