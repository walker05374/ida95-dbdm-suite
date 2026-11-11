# Path: ida95_dbdm_suite/ue4_analyzer.py
# Runtime: Python 3.11+ / IDA Pro 9.x

import ida_bytes
import ida_funcs
import ida_nalt
import ida_name
import idc
from typing import Dict, List, Optional, Tuple
from .arm64_resolver import ARM64Resolver
from .dbdm_config import DEFAULT_SIGNATURES, UE4Offsets
from .ida95_types import IDA95TypeBuilder


class UE4DBDMAnalyzer:
    """Core static & memory-snapshot analyzer for Dead by Daylight Mobile (UE4.27 ARM64, WITH_CASE_PRESERVING_NAME=1)."""

    MAX_FNAME_LEN = 1023
    MAX_OBJECTS_CAP = 400000
    ELEMENTS_PER_CHUNK = 64 * 1024

    def __init__(self, offsets: Optional[UE4Offsets] = None) -> None:
        self.offsets = offsets or UE4Offsets()
        self.resolved_globals: Dict[str, int] = {}
        self.fname_cache: Dict[int, str] = {}

    def scan_all_signatures(self) -> Dict[str, int]:
        image_base = ida_nalt.get_imagebase()
        for spec in DEFAULT_SIGNATURES:
            match_ea = ARM64Resolver.find_pattern(spec.pattern, spec.section_hint)
            if match_ea == idc.BADADDR:
                if spec.static_rva_fallback:
                    fallback_ea = image_base + spec.static_rva_fallback
                    if ARM64Resolver.is_valid_ea(fallback_ea):
                        self.resolved_globals[spec.name] = fallback_ea
                continue

            if spec.is_pointer_ref:
                target_ea = ARM64Resolver.resolve_adrp_pair(match_ea)
                # Em binários sem relocações aplicadas no GOT, faz fallback para o RVA estático provado (Grau A)
                if (
                    target_ea is None
                    or (
                        spec.static_rva_fallback
                        and target_ea != image_base + spec.static_rva_fallback
                        and ida_bytes.get_qword(target_ea) == 0
                    )
                ):
                    if spec.static_rva_fallback:
                        cand_ea = image_base + spec.static_rva_fallback
                        if ARM64Resolver.is_valid_ea(cand_ea):
                            target_ea = cand_ea

                if target_ea is not None:
                    self.resolved_globals[spec.name] = target_ea
                    ida_name.set_name(
                        target_ea,
                        f"g_{spec.name}",
                        ida_name.SN_NOCHECK | ida_name.SN_FORCE,
                    )
            else:
                self.resolved_globals[spec.name] = match_ea
                ida_funcs.add_func(match_ea)
                ida_name.set_name(
                    match_ea,
                    f"UE4_{spec.name}",
                    ida_name.SN_NOCHECK | ida_name.SN_FORCE,
                )

        return self.resolved_globals

    def read_fname(self, fname_pool_ea: int, name_entry_id: int) -> str:
        if name_entry_id in self.fname_cache:
            return self.fname_cache[name_entry_id]

        if not ARM64Resolver.is_valid_ea(fname_pool_ea) or name_entry_id < 0:
            return f"Name_{name_entry_id:X}"

        block_idx = name_entry_id >> 16
        offset_in_block = (name_entry_id & 0xFFFF) * self.offsets.fname_stride
        if block_idx >= 8192:
            return f"InvalidBlock_{name_entry_id:X}"

        blocks_base = fname_pool_ea + self.offsets.fname_pool_blocks_offset
        block_ptr_ea = blocks_base + (block_idx * 8)
        if not ARM64Resolver.is_valid_ea(block_ptr_ea):
            return f"UnmappedBlock_{name_entry_id:X}"

        block_ea = ida_bytes.get_qword(block_ptr_ea) & 0x0000FFFFFFFFFFFF
        if not ARM64Resolver.is_valid_ea(block_ea):
            return f"NullBlock_{name_entry_id:X}"

        entry_ea = block_ea + offset_in_block
        if not ARM64Resolver.is_valid_ea(entry_ea):
            return f"BadEntry_{name_entry_id:X}"

        # FNameEntry em UE 4.27 DBDM (WITH_CASE_PRESERVING_NAME = 1):
        # +0x00: uint32 ComparisonId
        # +0x04: uint16 Header (bIsWide = Header & 1, Len = Header >> 1)
        # +0x06: char AnsiName[Len] / char16_t WideName[Len]
        header = ida_bytes.get_word(entry_ea + self.offsets.fname_entry_header_offset)
        length = header >> 1
        is_wide = bool(header & 1)

        if length <= 0 or length > self.MAX_FNAME_LEN:
            return f"CorruptLen_{name_entry_id:X}"

        str_ea = entry_ea + self.offsets.fname_entry_header_size
        raw_bytes = ida_bytes.get_bytes(str_ea, length * (2 if is_wide else 1))
        if not raw_bytes:
            return f"EmptyStr_{name_entry_id:X}"

        try:
            decoded = (
                raw_bytes.decode("utf-16-le", errors="replace")
                if is_wide
                else raw_bytes.decode("utf-8", errors="replace")
            )
        except Exception:
            decoded = f"DecodeErr_{name_entry_id:X}"

        clean_name = "".join(
            ch if (ch.isalnum() or ch in "_/") else "_" for ch in decoded
        )
        self.fname_cache[name_entry_id] = clean_name
        return clean_name

    def get_uobject_by_index(self, guobject_array_ea: int, index: int) -> int:
        if index < 0 or index >= self.MAX_OBJECTS_CAP:
            return idc.BADADDR

        objects_ptr = (
            ida_bytes.get_qword(
                guobject_array_ea + self.offsets.tuobject_array_objects_offset
            )
            & 0x0000FFFFFFFFFFFF
        )
        if not ARM64Resolver.is_valid_ea(objects_ptr):
            objects_ptr = ida_bytes.get_qword(guobject_array_ea) & 0x0000FFFFFFFFFFFF
            if not ARM64Resolver.is_valid_ea(objects_ptr):
                return idc.BADADDR

        chunk_idx = index // self.ELEMENTS_PER_CHUNK
        within_idx = index % self.ELEMENTS_PER_CHUNK

        chunk_ptr_ea = objects_ptr + (chunk_idx * 8)
        if not ARM64Resolver.is_valid_ea(chunk_ptr_ea):
            return idc.BADADDR

        chunk_base = ida_bytes.get_qword(chunk_ptr_ea) & 0x0000FFFFFFFFFFFF
        if not ARM64Resolver.is_valid_ea(chunk_base):
            return idc.BADADDR

        item_ea = chunk_base + (within_idx * self.offsets.fuobject_item_size)
        if not ARM64Resolver.is_valid_ea(item_ea):
            return idc.BADADDR

        obj_ea = ida_bytes.get_qword(item_ea) & 0x0000FFFFFFFFFFFF
        return obj_ea if ARM64Resolver.is_valid_ea(obj_ea) else idc.BADADDR

    def read_uobject_name(self, fname_pool_ea: int, obj_ea: int) -> str:
        if not ARM64Resolver.is_valid_ea(obj_ea):
            return "InvalidObj"
        display_idx = ida_bytes.get_dword(
            obj_ea + self.offsets.uobject_name_display_offset
        )
        comp_idx = ida_bytes.get_dword(
            obj_ea + self.offsets.uobject_name_private_offset
        )
        entry_id = display_idx if display_idx != 0 else comp_idx
        return self.read_fname(fname_pool_ea, entry_id)

    def reconstruct_ustruct_and_functions(
        self, fname_pool_ea: int, guobject_array_ea: int
    ) -> Tuple[int, int]:
        """Iterates GUObjectArray when examining a live memory dump IDB, reconstructing UStructs and UFunction Exec pointers."""
        if not (
            ARM64Resolver.is_valid_ea(fname_pool_ea)
            and ARM64Resolver.is_valid_ea(guobject_array_ea)
        ):
            return 0, 0

        num_elements = ida_bytes.get_dword(
            guobject_array_ea + self.offsets.tuobject_array_num_elements_offset
        )
        if num_elements <= 0 or num_elements > self.MAX_OBJECTS_CAP:
            num_elements = ida_bytes.get_dword(guobject_array_ea + 0x14)
        if num_elements <= 0 or num_elements > self.MAX_OBJECTS_CAP:
            return 0, 0

        structs_built = 0
        funcs_annotated = 0

        for idx in range(num_elements):
            obj_ea = self.get_uobject_by_index(guobject_array_ea, idx)
            if obj_ea == idc.BADADDR:
                continue

            class_ea = (
                ida_bytes.get_qword(
                    obj_ea + self.offsets.uobject_class_private_offset
                )
                & 0x0000FFFFFFFFFFFF
            )
            if not ARM64Resolver.is_valid_ea(class_ea):
                continue

            class_name = self.read_uobject_name(fname_pool_ea, class_ea)
            obj_name = self.read_uobject_name(fname_pool_ea, obj_ea)

            if class_name == "Function":
                exec_ea = (
                    ida_bytes.get_qword(obj_ea + self.offsets.ufunction_func_offset)
                    & 0x0000FFFFFFFFFFFF
                )
                if ARM64Resolver.is_valid_ea(exec_ea):
                    outer_ea = (
                        ida_bytes.get_qword(
                            obj_ea + self.offsets.uobject_outer_private_offset
                        )
                        & 0x0000FFFFFFFFFFFF
                    )
                    outer_name = (
                        self.read_uobject_name(fname_pool_ea, outer_ea)
                        if ARM64Resolver.is_valid_ea(outer_ea)
                        else "Global"
                    )
                    full_fn_name = f"{outer_name}__{obj_name}_Exec"
                    ida_funcs.add_func(exec_ea)
                    ida_name.set_name(
                        exec_ea,
                        full_fn_name,
                        ida_name.SN_NOCHECK | ida_name.SN_NOWARN | ida_name.SN_FORCE,
                    )
                    funcs_annotated += 1

            elif class_name in ("Class", "ScriptStruct"):
                prop_fields: List[Tuple[str, str, int, int]] = []
                prop_ea = (
                    ida_bytes.get_qword(
                        obj_ea + self.offsets.ustruct_child_properties_offset
                    )
                    & 0x0000FFFFFFFFFFFF
                )
                guard = 0
                while ARM64Resolver.is_valid_ea(prop_ea) and guard < 512:
                    guard += 1
                    prop_disp_idx = ida_bytes.get_dword(
                        prop_ea + self.offsets.ffield_name_display_offset
                    )
                    prop_comp_idx = ida_bytes.get_dword(
                        prop_ea + self.offsets.ffield_name_offset
                    )
                    prop_name_idx = (
                        prop_disp_idx if prop_disp_idx != 0 else prop_comp_idx
                    )
                    prop_name = self.read_fname(fname_pool_ea, prop_name_idx)
                    elem_size = ida_bytes.get_dword(
                        prop_ea + self.offsets.fproperty_element_size_offset
                    )
                    offset_int = ida_bytes.get_dword(
                        prop_ea + self.offsets.fproperty_offset_internal_offset
                    )
                    if 0 <= offset_int < 0x10000 and 1 <= elem_size <= 0x1000:
                        c_type = (
                            "uint64_t"
                            if elem_size == 8
                            else (
                                "uint32_t"
                                if elem_size == 4
                                else f"uint8_t[{elem_size}]"
                            )
                        )
                        prop_fields.append((prop_name, c_type, offset_int, elem_size))
                    prop_ea = (
                        ida_bytes.get_qword(
                            prop_ea + self.offsets.ffield_next_offset
                        )
                        & 0x0000FFFFFFFFFFFF
                    )

                struct_size = ida_bytes.get_dword(
                    obj_ea + self.offsets.ustruct_properties_size_offset
                )
                if prop_fields:
                    IDA95TypeBuilder.create_or_update_struct(
                        f"U_{obj_name}",
                        prop_fields,
                        total_size=max(struct_size, 0),
                    )
                    structs_built += 1

        return structs_built, funcs_annotated

