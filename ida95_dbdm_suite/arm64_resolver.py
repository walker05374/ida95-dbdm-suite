# Path: ida95_dbdm_suite/arm64_resolver.py
# Runtime: Python 3.11+ / IDA Pro 9.x

import ida_bytes
import ida_ida
import ida_idp
import ida_segment
import idc
from typing import Dict, Optional, Tuple


class ARM64Resolver:
    """Decodes AArch64 ADRP+ADD / ADRP+LDR sequences and resolves cross-segment pointers safely in IDA 9.x."""

    MAX_SCAN_INSNS = 16

    @staticmethod
    def is_valid_ea(ea: int) -> bool:
        return ea != idc.BADADDR and ea != 0 and ida_segment.getseg(ea) is not None

    @staticmethod
    def decode_adrp(insn_bytes: int, pc: int) -> Optional[Tuple[int, int]]:
        # ADRP: [31]=1, [30:29]=immlo, [28:24]=10000, [23:5]=immhi, [4:0]=Rd
        if (insn_bytes & 0x9F000000) != 0x90000000:
            return None
        rd = insn_bytes & 0x1F
        immlo = (insn_bytes >> 29) & 0x3
        immhi = (insn_bytes >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= 1 << 21
        page_base = (pc & ~0xFFF) + (imm << 12)
        return rd, page_base & 0xFFFFFFFFFFFFFFFF

    @staticmethod
    def decode_add_imm64(insn_bytes: int) -> Optional[Tuple[int, int, int]]:
        # ADD (immediate, 64-bit): [31]=1, [30:29]=00, [28:23]=100010, [22]=sh, [21:10]=imm12, [9:5]=Rn, [4:0]=Rd
        if (insn_bytes & 0xFF800000) != 0x91000000:
            return None
        rd = insn_bytes & 0x1F
        rn = (insn_bytes >> 5) & 0x1F
        imm12 = (insn_bytes >> 10) & 0xFFF
        sh = (insn_bytes >> 22) & 0x1
        offset = imm12 << (12 if sh else 0)
        return rd, rn, offset

    @staticmethod
    def decode_ldr_imm64(insn_bytes: int) -> Optional[Tuple[int, int, int]]:
        # LDR (immediate, unsigned offset, 64-bit): [31:22]=1111100101, [21:10]=imm12, [9:5]=Rn, [4:0]=Rt
        if (insn_bytes & 0xFFC00000) != 0xF9400000:
            return None
        rt = insn_bytes & 0x1F
        rn = (insn_bytes >> 5) & 0x1F
        imm12 = (insn_bytes >> 10) & 0xFFF
        offset = imm12 * 8
        return rt, rn, offset

    @classmethod
    def resolve_adrp_pair(cls, start_ea: int) -> Optional[int]:
        if not cls.is_valid_ea(start_ea):
            return None

        adrp_pages: Dict[int, int] = {}
        curr_ea = start_ea

        for _ in range(cls.MAX_SCAN_INSNS):
            if not cls.is_valid_ea(curr_ea):
                break
            raw = ida_bytes.get_dword(curr_ea)
            adrp = cls.decode_adrp(raw, curr_ea)
            if adrp is not None:
                rd, page = adrp
                adrp_pages[rd] = page
                curr_ea += 4
                continue

            add_op = cls.decode_add_imm64(raw)
            if add_op is not None:
                _, rn, off = add_op
                if rn in adrp_pages:
                    target = (adrp_pages[rn] + off) & 0xFFFFFFFFFFFFFFFF
                    if cls.is_valid_ea(target):
                        return target

            ldr_op = cls.decode_ldr_imm64(raw)
            if ldr_op is not None:
                _, rn, off = ldr_op
                if rn in adrp_pages:
                    ptr_ea = (adrp_pages[rn] + off) & 0xFFFFFFFFFFFFFFFF
                    if cls.is_valid_ea(ptr_ea):
                        deref = ida_bytes.get_qword(ptr_ea) & 0x0000FFFFFFFFFFFF
                        if cls.is_valid_ea(deref):
                            return deref
                        return ptr_ea

            curr_ea += 4

        return None

    @classmethod
    def find_pattern(cls, pattern_str: str, section_name: str = ".text") -> int:
        seg = ida_segment.get_segm_by_name(section_name)
        if seg is not None:
            start_ea = seg.start_ea
            end_ea = seg.end_ea
        else:
            start_ea = ida_ida.inf_get_min_ea()
            end_ea = ida_ida.inf_get_max_ea()

        compiled = ida_bytes.compiled_binpat_vec_t()
        encoding = ida_idp.get_default_encoding_idx(ida_idp.BPU_1B)
        err = ida_bytes.parse_binpat_str(compiled, start_ea, pattern_str, 16, encoding)
        if err:
            return idc.BADADDR

        result = ida_bytes.bin_search3(
            start_ea,
            end_ea,
            compiled,
            ida_bytes.BIN_SEARCH_FORWARD | ida_bytes.BIN_SEARCH_NOBREAK,
        )
        if isinstance(result, tuple):
            return result[0]
        return result

