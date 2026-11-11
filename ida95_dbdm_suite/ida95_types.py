# Path: ida95_dbdm_suite/ida95_types.py
# Runtime: Python 3.11+ / IDA Pro 9.x (Replaces deprecated ida_struct and ida_enum with ida_typeinf)

import ida_name
import ida_typeinf
import idc
from typing import List, Tuple
from .dbdm_config import UE4Offsets


class IDA95TypeBuilder:
    """IDA 9.x native type creator using ida_typeinf.tinfo_t and udt_type_data_t."""

    @staticmethod
    def get_idati() -> ida_typeinf.til_t:
        return ida_typeinf.get_idati()

    @classmethod
    def create_or_update_struct(
        cls,
        struct_name: str,
        fields: List[Tuple[str, str, int, int]],
        total_size: int = 0,
    ) -> int:
        """
        Creates or updates a struct in IDA 9.x local type library without ida_struct.
        fields format: [(field_name, c_type_str, byte_offset, byte_size), ...]
        """
        til = cls.get_idati()
        udt = ida_typeinf.udt_type_data_t()
        udt.is_union = False

        sorted_fields = sorted(fields, key=lambda x: x[2])
        for fname, ftype_str, foffset, fsize in sorted_fields:
            udm = ida_typeinf.udm_t()
            udm.name = fname
            udm.offset = foffset * 8

            field_tif = ida_typeinf.tinfo_t()
            if not field_tif.get_named_type(til, ftype_str):
                if not ida_typeinf.parse_decl(
                    field_tif, til, f"{ftype_str} x;", ida_typeinf.PT_SIL
                ):
                    field_tif = ida_typeinf.tinfo_t(
                        ida_typeinf.BT_INT64 if fsize == 8 else ida_typeinf.BT_INT32
                    )

            udm.type = field_tif
            udm.size = fsize * 8
            udt.push_back(udm)

        if total_size > 0:
            udt.total_size = total_size

        tif = ida_typeinf.tinfo_t()
        if not tif.create_udt(udt, ida_typeinf.BTF_STRUCT):
            return idc.BADADDR

        flags = ida_typeinf.NTF_REPLACE
        res = tif.set_named_type(til, struct_name, flags)
        if res != ida_typeinf.TERR_OK:
            return idc.BADADDR
        return tif.get_ordinal()

    @classmethod
    def apply_struct_at(cls, ea: int, struct_name: str, symbol_name: str = "") -> bool:
        til = cls.get_idati()
        tif = ida_typeinf.tinfo_t()
        if not tif.get_named_type(til, struct_name):
            return False

        if symbol_name:
            ida_name.set_name(ea, symbol_name, ida_name.SN_NOCHECK | ida_name.SN_FORCE)

        return ida_typeinf.apply_tinfo(ea, tif, ida_typeinf.TINFO_DEFINITE)

    @classmethod
    def register_core_ue4_types(cls, off: UE4Offsets) -> None:
        # FName (12 bytes em UE 4.27 DBDM com WITH_CASE_PRESERVING_NAME = 1)
        cls.create_or_update_struct(
            "FName",
            [
                ("ComparisonIndex", "uint32_t", 0x00, 4),
                ("DisplayIndex", "uint32_t", 0x04, 4),
                ("Number", "uint32_t", 0x08, 4),
            ],
            total_size=0x0C,
        )

        # UObject / UObjectBase (0x30 bytes)
        cls.create_or_update_struct(
            "UObject",
            [
                ("VTable", "void*", 0x00, 8),
                ("ObjectFlags", "uint32_t", off.uobject_flags_offset, 4),
                ("InternalIndex", "uint32_t", off.uobject_internal_index_offset, 4),
                ("ClassPrivate", "void*", off.uobject_class_private_offset, 8),
                ("NamePrivate", "FName", off.uobject_name_private_offset, 12),
                ("Pad_24", "uint32_t", 0x24, 4),
                ("OuterPrivate", "void*", off.uobject_outer_private_offset, 8),
            ],
            total_size=0x30,
        )

        # UField (0x38 bytes)
        cls.create_or_update_struct(
            "UField",
            [
                ("BaseObject", "UObject", 0x00, 0x30),
                ("Next", "void*", off.ufield_next_offset, 8),
            ],
            total_size=0x38,
        )

        # UStruct (SuperStruct +0x48, Children +0x50, ChildProperties +0x58, PropertiesSize +0x60)
        cls.create_or_update_struct(
            "UStruct",
            [
                ("BaseField", "UField", 0x00, 0x38),
                ("StructBaseChain", "uint64_t[2]", 0x38, 16),
                ("SuperStruct", "void*", off.ustruct_super_struct_offset, 8),
                ("Children", "void*", off.ustruct_children_offset, 8),
                ("ChildProperties", "void*", off.ustruct_child_properties_offset, 8),
                ("PropertiesSize", "int32_t", off.ustruct_properties_size_offset, 4),
                ("MinAlignment", "int32_t", off.ustruct_min_alignment_offset, 4),
            ],
            total_size=0x68,
        )

        # FUObjectItem (0x18 bytes)
        cls.create_or_update_struct(
            "FUObjectItem",
            [
                ("Object", "UObject*", 0x00, 8),
                ("Flags", "int32_t", 0x08, 4),
                ("ClusterRootIndex", "int32_t", 0x0C, 4),
                ("SerialNumber", "int32_t", 0x10, 4),
                ("Pad_14", "int32_t", 0x14, 4),
            ],
            total_size=off.fuobject_item_size,
        )

        # TUObjectArray / FChunkedFixedUObjectArray (0x20 bytes)
        cls.create_or_update_struct(
            "TUObjectArray",
            [
                ("Objects", "FUObjectItem**", 0x00, 8),
                ("PreAllocatedObjects", "FUObjectItem*", 0x08, 8),
                ("MaxElements", "int32_t", 0x10, 4),
                ("NumElements", "int32_t", 0x14, 4),
                ("MaxChunks", "int32_t", 0x18, 4),
                ("NumChunks", "int32_t", 0x1C, 4),
            ],
            total_size=0x20,
        )

        # FUObjectArray (GUObjectArray @ 0x10BF7920, ObjObjects @ +0x10)
        cls.create_or_update_struct(
            "FUObjectArray",
            [
                ("ObjFirstGCIndex", "int32_t", 0x00, 4),
                ("ObjLastNonGCIndex", "int32_t", 0x04, 4),
                ("MaxObjectsNotConsideredByGC", "int32_t", 0x08, 4),
                ("OpenForDisregardForGC", "uint32_t", 0x0C, 4),
                ("ObjObjects", "TUObjectArray", off.tuobject_array_objects_offset, 0x20),
            ],
            total_size=0x30,
        )

