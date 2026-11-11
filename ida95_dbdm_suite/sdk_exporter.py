# Path: ida95_dbdm_suite/sdk_exporter.py
# Runtime: Python 3.11+ / IDA Pro 9.x

import json
import os
import ida_nalt
from typing import Dict
from .dbdm_config import UE4Offsets


class DBDMSDKExporter:
    """Exports resolved DBDM offsets, function addresses, and C++20 SDK headers."""

    @staticmethod
    def get_image_base() -> int:
        return ida_nalt.get_imagebase()

    @classmethod
    def export_all(
        cls,
        output_dir: str,
        resolved_globals: Dict[str, int],
        offsets: UE4Offsets,
        structs_count: int,
        funcs_count: int,
    ) -> str:
        os.makedirs(output_dir, exist_ok=True)
        base = cls.get_image_base()

        rva_map = {
            k: f"0x{v - base:X}" if v >= base else f"0x{v:X}"
            for k, v in resolved_globals.items()
        }

        payload = {
            "target": "DeadByDaylightMobile_UE4_ARM64",
            "ida_compatibility": "9.0-9.5+",
            "image_base": f"0x{base:X}",
            "resolved_rvas": rva_map,
            "reconstructed_structs": structs_count,
            "annotated_ufunctions": funcs_count,
        }

        json_path = os.path.join(output_dir, "dbdm_offsets.json")
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)

        hpp_path = os.path.join(output_dir, "dbdm_sdk_offsets.hpp")
        with open(hpp_path, "w", encoding="utf-8") as f:
            f.write("#pragma once\n#include <cstdint>\n\nnamespace DBDM {\n")
            for name, rva_str in rva_map.items():
                f.write(f"    constexpr uintptr_t {name}_RVA = {rva_str};\n")
            f.write("\n    namespace UE4Layout {\n")
            f.write(f"        constexpr uint32_t FNamePoolBlocks = 0x{offsets.fname_pool_blocks_offset:X};\n")
            f.write(f"        constexpr uint32_t FNameStride = 0x{offsets.fname_stride:X};\n")
            f.write(f"        constexpr uint32_t FNameEntryHeader = 0x{offsets.fname_entry_header_offset:X};\n")
            f.write(f"        constexpr uint32_t FNameEntryString = 0x{offsets.fname_entry_header_size:X};\n")
            f.write(f"        constexpr uint32_t UObjectClassPrivate = 0x{offsets.uobject_class_private_offset:X};\n")
            f.write(f"        constexpr uint32_t UObjectNamePrivate = 0x{offsets.uobject_name_private_offset:X};\n")
            f.write(f"        constexpr uint32_t UObjectOuterPrivate = 0x{offsets.uobject_outer_private_offset:X};\n")
            f.write(f"        constexpr uint32_t UFieldNext = 0x{offsets.ufield_next_offset:X};\n")
            f.write(f"        constexpr uint32_t UStructSuperStruct = 0x{offsets.ustruct_super_struct_offset:X};\n")
            f.write(f"        constexpr uint32_t UStructChildren = 0x{offsets.ustruct_children_offset:X};\n")
            f.write(f"        constexpr uint32_t UStructChildProperties = 0x{offsets.ustruct_child_properties_offset:X};\n")
            f.write(f"        constexpr uint32_t UStructPropertiesSize = 0x{offsets.ustruct_properties_size_offset:X};\n")
            f.write(f"        constexpr uint32_t UFunctionExec = 0x{offsets.ufunction_func_offset:X};\n")
            f.write(f"        constexpr uint32_t FFieldNext = 0x{offsets.ffield_next_offset:X};\n")
            f.write(f"        constexpr uint32_t FFieldName = 0x{offsets.ffield_name_offset:X};\n")
            f.write(f"        constexpr uint32_t FPropertyElementSize = 0x{offsets.fproperty_element_size_offset:X};\n")
            f.write(f"        constexpr uint32_t FPropertyOffsetInternal = 0x{offsets.fproperty_offset_internal_offset:X};\n")
            f.write("    }\n}\n")

        return hpp_path

