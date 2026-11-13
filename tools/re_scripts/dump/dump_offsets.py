#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fnmatch
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from elf_layout import (  # type: ignore
        ElfLayout,
        apply_rela_in_memory,
        estimate_function_size,
        lookup_fde_function_size,
        parse_elf_layout,
        scan_ret_function_size,
    )
else:
    from .elf_layout import (
        ElfLayout,
        apply_rela_in_memory,
        estimate_function_size,
        lookup_fde_function_size,
        parse_elf_layout,
        scan_ret_function_size,
    )


PROP_GEN_TYPES: Dict[int, Tuple[str, int]] = {
    0x00: ("ByteProperty", 1),
    0x01: ("Int8Property", 1),
    0x02: ("Int16Property", 2),
    0x03: ("IntProperty", 4),
    0x04: ("Int64Property", 8),
    0x05: ("UInt16Property", 2),
    0x06: ("UInt32Property", 4),
    0x07: ("UInt64Property", 8),
    0x08: ("UnsizedIntProperty", 8),
    0x09: ("UnsizedUIntProperty", 8),
    0x0A: ("FloatProperty", 4),
    0x0B: ("DoubleProperty", 8),
    0x0C: ("BoolProperty", 1),
    0x0D: ("SoftClassProperty", 0x18),
    0x0E: ("WeakObjectProperty", 8),
    0x0F: ("LazyObjectProperty", 0x1C),
    0x10: ("SoftObjectProperty", 0x18),
    0x11: ("ClassProperty", 8),
    0x12: ("ObjectProperty", 8),
    0x13: ("InterfaceProperty", 0x10),
    0x14: ("NameProperty", 0x0C),
    0x15: ("StrProperty", 0x10),
    0x16: ("ArrayProperty", 0x10),
    0x17: ("MapProperty", 0x50),
    0x18: ("SetProperty", 0x50),
    0x19: ("StructProperty", 0),
    0x1A: ("DelegateProperty", 0x10),
    0x1B: ("InlineMulticastDelegateProperty", 0x10),
    0x1C: ("SparseMulticastDelegateProperty", 1),
    0x1D: ("TextProperty", 0x18),
    0x1E: ("EnumProperty", 1),
}

DEFAULT_DOMAIN_PATTERNS: Dict[str, List[str]] = {
    "movement": ["*Movement*", "*Character*", "*Pawn*"],
    "skillcheck": ["*SkillCheck*", "*Chargeable*", "*Interaction*"],
    "outline": ["*Outline*", "*Aura*"],
    "player": ["*DBDPlayer*", "*Camper*", "*Slasher*", "*PlayerState*"],
    "world": ["*Generator*", "*Pallet*", "*Totem*", "*Hatch*", "*EscapeDoor*", "*Window*", "*Searchable*"],
}


def resolve_so_from_i64(i64_path: Path) -> Path:
    candidates = [
        i64_path.parent / "libUE4_fixed.so",
        i64_path.with_suffix(""),
        i64_path.parent / "libUE4.so",
    ]
    for cand in candidates:
        if cand.is_file():
            return cand
    raise FileNotFoundError(f"Could not locate companion ELF binary for {i64_path}")


def compute_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_cstr(buf: bytes, va: int, max_len: int = 160) -> str:
    if va <= 0 or va >= len(buf):
        return ""
    end = buf.find(b"\x00", va, min(len(buf), va + max_len))
    if end == -1 or end == va:
        return ""
    try:
        s = buf[va:end].decode("ascii")
        return s if s.isprintable() else ""
    except Exception:
        return ""


def read_utf16le(buf: bytes, va: int, max_chars: int = 128) -> str:
    if va <= 0 or va + 2 > len(buf):
        return ""
    chars: List[str] = []
    for i in range(max_chars):
        pos = va + i * 2
        if pos + 2 > len(buf):
            break
        ch = struct.unpack_from("<H", buf, pos)[0]
        if ch == 0:
            break
        if 32 <= ch < 127:
            chars.append(chr(ch))
        else:
            return ""
    return "".join(chars)


def prologue_hex(buf: bytes, fn_va: int, nbytes: int = 16) -> str:
    if fn_va <= 0 or fn_va + nbytes > len(buf):
        return ""
    return buf[fn_va : fn_va + nbytes].hex()


def decode_static_class(buf: bytes, layout: ElfLayout, fn_va: int) -> Tuple[str, int, int, int]:
    if not (layout.text_start <= fn_va < layout.text_end):
        return "", 0, 0, 0
    regs: Dict[int, int] = {}
    adrp: Dict[int, int] = {}
    singleton_bss = 0
    str_xref_va = 0
    for off in range(fn_va, min(fn_va + 256, layout.text_end, len(buf)), 4):
        insn = struct.unpack_from("<I", buf, off)[0]
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 0x3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= 1 << 21
            adrp[rd] = (off & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            if rn in adrp:
                val = adrp[rn] + imm12
                regs[rd] = val
                if rd == 1 and str_xref_va == 0:
                    str_xref_va = off
                val_va = layout.offset_to_va(val)
                if layout.bss_va_start <= val_va < layout.bss_va_end and singleton_bss == 0:
                    singleton_bss = val_va
        elif (insn & 0xFF800000) == 0x52800000:
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            sh = ((insn >> 21) & 0x3) * 16
            regs[rd] = imm16 << sh
        elif (insn & 0xFC000000) == 0x94000000:
            break
    cls_name = read_utf16le(buf, regs.get(1, 0))
    cls_size = regs.get(4, 0)
    return cls_name, cls_size, singleton_bss, str_xref_va


def decode_setbit(buf: bytes, layout: ElfLayout, setbit_va: int) -> Tuple[Optional[int], int, str]:
    if not (layout.text_start <= setbit_va < layout.text_end):
        return None, 1, ""
    base_add = 0
    for off in range(setbit_va, min(setbit_va + 40, layout.text_end, len(buf)), 4):
        insn = struct.unpack_from("<I", buf, off)[0]
        if (insn & 0xFF800000) == 0x91000000 and ((insn >> 5) & 0x1F) == 0:
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            base_add = imm12
        elif (insn & 0xFFC003E0) in (0x39400000, 0x39000000):
            imm12 = (insn >> 10) & 0xFFF
            return base_add + imm12, 1, f"xref:0x{off:08X} ldrb/strb [x0,#0x{base_add + imm12:X}]"
        elif (insn & 0xFFC003E0) in (0x79400000, 0x79000000):
            imm12 = ((insn >> 10) & 0xFFF) * 2
            return base_add + imm12, 2, f"xref:0x{off:08X} ldrh/strh [x0,#0x{base_add + imm12:X}]"
        elif (insn & 0xFFC003E0) in (0xB9400000, 0xB9000000):
            imm12 = ((insn >> 10) & 0xFFF) * 4
            return base_add + imm12, 4, f"xref:0x{off:08X} ldr/str [x0,#0x{base_add + imm12:X}]"
    return None, 1, ""


def get_static_register_natives_va(buf: bytes, layout: ElfLayout, fn_va: int) -> int:
    if not (layout.text_start <= fn_va < layout.text_end):
        return 0
    regs: Dict[int, int] = {}
    adrp: Dict[int, int] = {}
    for off in range(fn_va, min(fn_va + 256, layout.text_end, len(buf)), 4):
        insn = struct.unpack_from("<I", buf, off)[0]
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            imm = (((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 0x3)
            if imm & (1 << 20):
                imm -= 1 << 21
            adrp[rd] = (off & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd, rn = insn & 0x1F, (insn >> 5) & 0x1F
            imm12 = ((insn >> 10) & 0xFFF) << (12 if ((insn >> 22) & 1) else 0)
            if rn in adrp:
                regs[rd] = adrp[rn] + imm12
        elif (insn & 0xFC000000) == 0x94000000:
            break
    return regs.get(3, 0)


def parse_native_lookup(buf: bytes, layout: ElfLayout, reg_fn_va: int) -> Dict[str, int]:
    if not (layout.text_start <= reg_fn_va < layout.text_end):
        return {}
    adrp: Dict[int, int] = {}
    regs: Dict[int, int] = {}
    table_va = 0
    count = 0
    for off in range(reg_fn_va, min(reg_fn_va + 256, layout.text_end, len(buf)), 4):
        insn = struct.unpack_from("<I", buf, off)[0]
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            imm = (((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 0x3)
            if imm & (1 << 20):
                imm -= 1 << 21
            adrp[rd] = (off & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd, rn = insn & 0x1F, (insn >> 5) & 0x1F
            imm12 = ((insn >> 10) & 0xFFF) << (12 if ((insn >> 22) & 1) else 0)
            if rn in adrp:
                regs[rd] = adrp[rn] + imm12
                if rd == 1 and layout.is_in_data(regs[1], 16):
                    table_va = regs[1]
        elif (insn & 0xFF800000) == 0x52800000:
            rd = insn & 0x1F
            regs[rd] = ((insn >> 5) & 0xFFFF) << (((insn >> 21) & 0x3) * 16)
            if rd == 2:
                count = regs[2]
        elif (insn & 0xFC000000) == 0x14000000 or insn == 0xD65F03C0:
            break
    out: Dict[str, int] = {}
    if table_va and 0 < count <= 256 and layout.is_in_data(table_va, count * 16):
        for i in range(count):
            nm_ptr, ex_fn = struct.unpack_from("<QQ", buf, table_va + i * 16)
            if nm_ptr == 0 or ex_fn == 0:
                break
            nm = read_cstr(buf, nm_ptr)
            if not nm:
                break
            if layout.text_start <= ex_fn < layout.text_end:
                out[nm] = ex_fn
    return out


def _is_valid_native_callee(buf: bytes, layout: ElfLayout, callee: int) -> bool:
    if not (layout.text_start <= callee < layout.text_end):
        return False
    if 0x0A6F0000 <= callee <= 0x0A7A0000:
        return False
    fde_sz = lookup_fde_function_size(buf, layout, callee)
    return fde_sz is not None and fde_sz > 0


def _written_gprs(einsn: int) -> Tuple[int, ...]:
    """Return GPR indices (0..30) written by non-MOV/non-unsigned-LDR instructions."""
    if (einsn & 0x1F000000) == 0x11000000:  # ADD/ADDS/SUB/SUBS (imm)
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F000000) == 0x0B000000:  # ADD/ADDS/SUB/SUBS (reg)
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F000000) == 0x10000000:  # ADR/ADRP
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F800000) == 0x12800000:  # MOVZ/MOVN/MOVK
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F800000) == 0x12000000:  # ORR/AND/EOR/ANDS (imm)
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F000000) == 0x0A000000:  # logical shifted reg
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F800000) == 0x13000000:  # UBFM/SBFM/BFM
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1FA00000) == 0x13800000:  # EXTR
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1FE00000) == 0x1A800000:  # CSEL/CSINC/CSINV/CSNEG
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x1F000000) == 0x1B000000:  # MADD/MSUB/etc.
        rd = einsn & 0x1F
        return (rd,) if rd < 31 else ()
    if (einsn & 0x3F000000) == 0x18000000:  # LDR/LDRSW (literal, GPR)
        rt = einsn & 0x1F
        return (rt,) if rt < 31 else ()
    if (einsn & 0x3E400000) == 0x28400000:  # LDP (GPR)
        rt = einsn & 0x1F
        rt2 = (einsn >> 10) & 0x1F
        out: List[int] = []
        if rt < 31:
            out.append(rt)
        if rt2 < 31:
            out.append(rt2)
        return tuple(out)
    if (
        (einsn & 0x3E000000) == 0x38000000
        and (einsn & 0x00C00000) != 0
        and (einsn & 0xFFC00000) != 0xF9800000
    ):  # LDR* (GPR, excludes PRFM)
        rt = einsn & 0x1F
        return (rt,) if rt < 31 else ()
    return ()


def collect_exec_candidates(
    buf: bytes,
    layout: ElfLayout,
    exec_fn: int,
) -> List[Tuple[int, int, int]]:
    """Collect (callee, vt_off, site) candidates in exec_fn where x0 holds `this`."""
    if not (layout.text_start <= exec_fn < layout.text_end):
        return []
    exec_sz = lookup_fde_function_size(buf, layout, exec_fn)
    if exec_sz is None or exec_sz <= 0:
        exec_sz = scan_ret_function_size(buf, layout, exec_fn) or 320
    exec_end = min(exec_fn + exec_sz, layout.text_end, len(buf))

    events: List[Tuple[int, int, int]] = []
    ctx_reg: Optional[int] = None
    x0_is_this = True
    vptr_regs: Set[int] = set()
    vt_slot_by_reg: Dict[int, int] = {}

    for eoff in range(exec_fn, exec_end, 4):
        einsn = struct.unpack_from("<I", buf, eoff)[0]

        if einsn in (0xD65F03C0, 0xD65F0BFF, 0xD65F0FFF):  # RET, RETAA, RETAB
            break

        if (einsn & 0xFC000000) == 0x94000000:  # BL imm26
            simm26 = einsn & 0x03FFFFFF
            if simm26 & (1 << 25):
                simm26 -= 1 << 26
            callee = eoff + (simm26 << 2)
            if x0_is_this and _is_valid_native_callee(buf, layout, callee):
                events.append((callee, 0, eoff))
            x0_is_this = False
            for r in range(19):
                vptr_regs.discard(r)
                vt_slot_by_reg.pop(r, None)
            continue

        if (einsn & 0xFC000000) == 0x14000000:  # B imm26
            simm26 = einsn & 0x03FFFFFF
            if simm26 & (1 << 25):
                simm26 -= 1 << 26
            callee = eoff + (simm26 << 2)
            if callee < exec_fn or callee >= exec_end:
                if x0_is_this and _is_valid_native_callee(buf, layout, callee):
                    events.append((callee, 0, eoff))
                break
            continue

        if (einsn & 0xFFFFFC1F) in (0xD63F0000, 0xD61F0000):  # BLR Xb / BR Xb
            rb = (einsn >> 5) & 0x1F
            if x0_is_this and rb in vt_slot_by_reg:
                events.append((0, vt_slot_by_reg[rb], eoff))
            if (einsn & 0xFFFFFC1F) == 0xD61F0000:
                if rb in vt_slot_by_reg or x0_is_this:
                    break
            else:
                x0_is_this = False
                for r in range(19):
                    vptr_regs.discard(r)
                    vt_slot_by_reg.pop(r, None)
            continue

        if (einsn & 0xFFE0FFE0) == 0xAA0003E0:  # MOV Xd, Xm
            rd = einsn & 0x1F
            rm = (einsn >> 16) & 0x1F
            if rm == 0 and x0_is_this and 19 <= rd <= 28:
                ctx_reg = rd
            new_x0_is_this = (
                (ctx_reg is not None and rm == ctx_reg) or (rm == 0 and x0_is_this)
            )
            rm_is_vptr = rm in vptr_regs
            rm_vt_slot = vt_slot_by_reg.get(rm)
            if rd < 31:
                if rd == 0:
                    x0_is_this = new_x0_is_this
                elif rd == ctx_reg and not (rm == 0 and 19 <= rd <= 28):
                    ctx_reg = None
                vptr_regs.discard(rd)
                vt_slot_by_reg.pop(rd, None)
                if rm_is_vptr:
                    vptr_regs.add(rd)
                elif rm_vt_slot is not None:
                    vt_slot_by_reg[rd] = rm_vt_slot
            continue

        if (einsn & 0xFFC00000) == 0xF9400000:  # LDR Xt, [Xn, #uoff]
            rt = einsn & 0x1F
            rn = (einsn >> 5) & 0x1F
            uoff = ((einsn >> 10) & 0xFFF) * 8
            is_vptr_load = uoff == 0 and (
                (ctx_reg is not None and rn == ctx_reg) or (rn == 0 and x0_is_this)
            )
            new_vt = uoff if (rn in vptr_regs and uoff > 0) else None
            if rt < 31:
                if rt == 0:
                    x0_is_this = False
                if rt == ctx_reg:
                    ctx_reg = None
                vptr_regs.discard(rt)
                vt_slot_by_reg.pop(rt, None)
                if is_vptr_load:
                    vptr_regs.add(rt)
                elif new_vt is not None:
                    vt_slot_by_reg[rt] = new_vt
            continue

        for r in _written_gprs(einsn):
            if r == 0:
                x0_is_this = False
            if r == ctx_reg:
                ctx_reg = None
            vptr_regs.discard(r)
            vt_slot_by_reg.pop(r, None)

    return events


def select_exec_native(
    events: Sequence[Tuple[int, int, int]],
    blacklist: Optional[Set[int]] = None,
) -> Tuple[int, int, int]:
    """Pick the first valid non-blacklisted native call or virtual slot from exec events."""
    for callee, vt_off, site in events:
        if callee:
            if blacklist is None or callee not in blacklist:
                return callee, 0, site
        elif vt_off:
            return 0, vt_off, site
    return 0, 0, 0


def scan_exec_for_native(
    buf: bytes,
    layout: ElfLayout,
    exec_fn: int,
    blacklist: Optional[Set[int]] = None,
) -> Tuple[int, int, int]:
    return select_exec_native(collect_exec_candidates(buf, layout, exec_fn), blacklist)


def extract_z_construct_exec(buf: bytes, layout: ElfLayout, z_va: int) -> Tuple[int, int]:
    if not (layout.text_start <= z_va < layout.text_end) or z_va + 64 > len(buf):
        return 0, 0
    adrp_pages: Dict[int, int] = {}
    for off in range(z_va, min(z_va + 64, layout.text_end, len(buf)), 4):
        insn = struct.unpack_from("<I", buf, off)[0]
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 0x3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= 1 << 21
            adrp_pages[rd] = (off & ~0xFFF) + (imm << 12)
        elif (insn & 0xFF800000) == 0x91000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            if (insn >> 22) & 1:
                imm12 <<= 12
            if rn in adrp_pages:
                target = adrp_pages[rn] + imm12
                if rd == 1 and target + 64 <= len(buf) and layout.is_in_data(target, 64):
                    qwords = struct.unpack_from("<8Q", buf, target)
                    exec_fn = 0
                    for q in qwords[3:]:
                        if layout.text_start <= q < layout.text_end:
                            exec_fn = q
                            break
                    return target, exec_fn
    return 0, 0


def resolve_z_construct_func(buf: bytes, layout: ElfLayout, z_va: int) -> Tuple[int, int, int, int, int]:
    target, exec_fn = extract_z_construct_exec(buf, layout, z_va)
    if not target:
        return 0, 0, 0, 0, 0
    native_fn, vt_off, bl_site = scan_exec_for_native(buf, layout, exec_fn) if exec_fn else (0, 0, 0)
    return target, exec_fn, native_fn, vt_off, bl_site


def match_domains(cls_name: str, domain_specs: Dict[str, List[str]]) -> List[str]:
    matched: List[str] = []
    for dom, patterns in domain_specs.items():
        for pat in patterns:
            if fnmatch.fnmatchcase(cls_name.lower(), pat.lower()) or pat.lower() in cls_name.lower():
                matched.append(dom)
                break
    return matched


def parse_domain_arg(raw_domains: Sequence[str]) -> Dict[str, List[str]]:
    result: Dict[str, List[str]] = {}
    for item in raw_domains:
        for token in item.split(","):
            token = token.strip()
            if not token:
                continue
            if "=" in token:
                dom, pats = token.split("=", 1)
                result[dom.strip()] = [p.strip() for p in pats.split("|") if p.strip()]
            elif token in DEFAULT_DOMAIN_PATTERNS:
                result[token] = DEFAULT_DOMAIN_PATTERNS[token]
            else:
                result[token] = [f"*{token}*"]
    return result or DEFAULT_DOMAIN_PATTERNS


def find_vtable_for_methods(buf: bytes, layout: ElfLayout, method_rvas: List[int]) -> Tuple[int, List[Tuple[int, int]]]:
    valid_methods = [m for m in method_rvas if layout.text_start <= m < layout.text_end]
    if not valid_methods:
        return 0, []
    anchor = valid_methods[0]
    needle = struct.pack("<Q", anchor)
    pos = layout.data_rel_ro_start
    vt_limit = min(layout.data_rel_ro_end, len(buf))
    while pos < vt_limit:
        idx = buf.find(needle, pos, vt_limit)
        if idx == -1:
            break
        if idx % 8 == 0:
            vt_base = idx
            while vt_base >= layout.data_rel_ro_start + 8:
                prev_ptr = struct.unpack_from("<Q", buf, vt_base - 8)[0]
                if layout.text_start <= prev_ptr < layout.text_end:
                    vt_base -= 8
                else:
                    break
            entries: List[Tuple[int, int]] = []
            cur = vt_base
            while cur + 8 <= vt_limit and len(entries) < 256:
                fn_ptr = struct.unpack_from("<Q", buf, cur)[0]
                if not (layout.text_start <= fn_ptr < layout.text_end):
                    break
                entries.append((cur - vt_base, fn_ptr))
                cur += 8
            if len(entries) >= 8:
                return vt_base, entries
        pos = idx + 8
    return 0, []


def dump_domains(
    buf: bytes,
    layout: ElfLayout,
    domain_specs: Dict[str, List[str]],
) -> Dict[str, List[Dict[str, Any]]]:
    out: Dict[str, List[Dict[str, Any]]] = {dom: [] for dom in domain_specs}
    exec_events_cache: Dict[int, List[Tuple[int, int, int]]] = {}
    callee_exec_refs: Dict[int, Set[int]] = {}
    matched_classes: List[Tuple[Any, ...]] = []

    for sec_start, sec_end in layout.data_ranges():
        scan_end = min(sec_end, len(buf) - 80)
        for off in range(sec_start, scan_end, 8):
            try:
                fn_va = struct.unpack_from("<Q", buf, off)[0]
                if not (layout.text_start <= fn_va < layout.text_end):
                    continue
                (
                    cfg_va,
                    cpp_va,
                    dep_arr,
                    fn_arr,
                    prop_arr,
                    iface_arr,
                    num_dep,
                    num_fn,
                    num_prop,
                    num_iface,
                    cls_flags,
                ) = struct.unpack_from("<QQQQQQIIIII", buf, off + 8)
            except struct.error:
                continue

            if not (
                0 < num_dep <= 64
                and num_fn <= 600
                and num_prop <= 600
                and num_iface <= 64
                and layout.is_in_data(dep_arr, num_dep * 8)
            ):
                continue

            cls_name, cls_size, bss_singleton, str_xref_va = decode_static_class(buf, layout, fn_va)
            if not cls_name:
                continue

            matched_domains = match_domains(cls_name, domain_specs)
            if not matched_domains:
                continue

            reg_fn_va = get_static_register_natives_va(buf, layout, fn_va)
            native_lookup = parse_native_lookup(buf, layout, reg_fn_va)
            fn_specs: List[Tuple[int, int, str, int]] = []
            if fn_arr and num_fn > 0 and fn_arr + num_fn * 16 <= len(buf) and layout.is_in_data(fn_arr, num_fn * 16):
                for i in range(num_fn):
                    f_ctor, f_name_ptr = struct.unpack_from("<QQ", buf, fn_arr + i * 16)
                    f_name = read_cstr(buf, f_name_ptr)
                    if not f_name:
                        continue
                    exec_fn = native_lookup.get(f_name, 0)
                    if not exec_fn:
                        _f_params, exec_fn = extract_z_construct_exec(buf, layout, f_ctor)
                    if exec_fn and exec_fn not in exec_events_cache:
                        ev_list = collect_exec_candidates(buf, layout, exec_fn)
                        exec_events_cache[exec_fn] = ev_list
                        for c, _, _ in ev_list:
                            if c:
                                callee_exec_refs.setdefault(c, set()).add(exec_fn)
                    fn_specs.append((f_ctor, f_name_ptr, f_name, exec_fn))

            matched_classes.append(
                (
                    off,
                    fn_va,
                    cls_name,
                    cls_size,
                    bss_singleton,
                    str_xref_va,
                    prop_arr,
                    num_prop,
                    matched_domains,
                    fn_specs,
                )
            )

    blacklist: Set[int] = {callee for callee, execs in callee_exec_refs.items() if len(execs) > 2}

    for (
        off,
        fn_va,
        cls_name,
        cls_size,
        bss_singleton,
        str_xref_va,
        prop_arr,
        num_prop,
        matched_domains,
        fn_specs,
    ) in matched_classes:
        members: List[Dict[str, Any]] = []
        sc_bytes = prologue_hex(buf, fn_va, 16)
        sc_fn_size, sc_size_src = estimate_function_size(buf, layout, fn_va)

        members.append(
            {
                "class": cls_name,
                "member": "StaticClass",
                "rva": f"0x{fn_va:08X}",
                "size": sc_fn_size,
                "size_source": sc_size_src,
                "type": "UClass*()",
                "evidence": (
                    f"xref:0x{str_xref_va:08X} -> FClassParams@0x{off:08X} "
                    f"(ClassSize=0x{cls_size:04X}, Singleton=0x{bss_singleton:08X}) bytes:{sc_bytes}"
                ),
                "confidence": "xref" if str_xref_va else "inferred",
            }
        )

        raw_props: List[Tuple[int, str, int, int, str, str, str, str]] = []
        if prop_arr and num_prop > 0 and prop_arr + num_prop * 8 <= len(buf) and layout.is_in_data(prop_arr, num_prop * 8):
            for i in range(num_prop):
                p_ptr = struct.unpack_from("<Q", buf, prop_arr + i * 8)[0]
                if p_ptr + 0x38 > len(buf) or not layout.is_in_data(p_ptr, 0x38):
                    continue
                p_name_ptr, _rep_func = struct.unpack_from("<QQ", buf, p_ptr)
                p_off = struct.unpack_from("<I", buf, p_ptr + 0x24)[0]
                gen_byte = buf[p_ptr + 0x18]
                prop_type_code = gen_byte & 0x1F
                prop_type_name, default_sz = PROP_GEN_TYPES.get(
                    prop_type_code, (f"Property_0x{prop_type_code:02X}", 0)
                )
                p_name = read_cstr(buf, p_name_ptr)
                if not p_name:
                    continue

                confidence = "string"
                size_src = "reflection" if default_sz > 0 else "none"
                evidence = f'string:0x{p_name_ptr:08X} "{p_name}" <- FPropertyParams@0x{p_ptr:08X}+0x24=0x{p_off:04X}'
                if prop_type_code == 0x0C and p_off == 0:
                    setbit = struct.unpack_from("<Q", buf, p_ptr + 0x28)[0]
                    if not (layout.text_start <= setbit < layout.text_end):
                        setbit = struct.unpack_from("<Q", buf, p_ptr + 0x30)[0]
                    sb_off, sb_sz, sb_ev = decode_setbit(buf, layout, setbit)
                    if sb_off is not None:
                        p_off = sb_off
                        default_sz = sb_sz
                        size_src = "xref"
                        confidence = "xref"
                        evidence = f'{sb_ev} | string:0x{p_name_ptr:08X} "{p_name}" (SetBit=0x{setbit:08X})'
                    else:
                        confidence = "inferred"

                raw_props.append((p_off, p_name, p_ptr, default_sz, size_src, prop_type_name, evidence, confidence))

        raw_props.sort(key=lambda x: x[0])
        for idx, (p_off, p_name, p_ptr, default_sz, size_src, prop_type_name, evidence, confidence) in enumerate(raw_props):
            inferred_sz = default_sz
            resolved_size_src = size_src
            if inferred_sz == 0:
                if idx + 1 < len(raw_props) and raw_props[idx + 1][0] > p_off:
                    inferred_sz = raw_props[idx + 1][0] - p_off
                    resolved_size_src = "inferred"
                elif cls_size > p_off:
                    inferred_sz = cls_size - p_off
                    resolved_size_src = "inferred"
                else:
                    resolved_size_src = "none"
                if confidence != "xref":
                    confidence = "inferred"
            members.append(
                {
                    "class": cls_name,
                    "member": p_name,
                    "rva": f"0x{p_off:04X}",
                    "size": inferred_sz,
                    "size_source": resolved_size_src,
                    "type": prop_type_name,
                    "evidence": evidence,
                    "confidence": confidence,
                }
            )

        native_candidates: List[int] = []
        resolved_fns: List[Tuple[int, int, str, int, int, int, int]] = []
        for f_ctor, f_name_ptr, f_name, exec_fn in fn_specs:
            native_fn, vt_off, bl_site = (
                select_exec_native(exec_events_cache.get(exec_fn, ()), blacklist)
                if exec_fn
                else (0, 0, 0)
            )
            if native_fn:
                native_candidates.append(native_fn)
            resolved_fns.append((f_ctor, f_name_ptr, f_name, exec_fn, native_fn, vt_off, bl_site))

        vt_base, vt_entries = find_vtable_for_methods(buf, layout, native_candidates)
        vt_map = dict(vt_entries)

        for f_ctor, f_name_ptr, f_name, exec_fn, native_fn, vt_off, bl_site in resolved_fns:
            via_vt = False
            if not native_fn and vt_off and vt_off in vt_map:
                native_fn = vt_map[vt_off]
                via_vt = True

            if native_fn:
                target_rva = native_fn
                conf = "xref"
                fn_type = "UFunction"
                fn_size, fn_size_src = estimate_function_size(buf, layout, target_rva)
                fn_bytes = prologue_hex(buf, target_rva, 16)
                vt_note = f", vtable+0x{vt_off:03X}" if via_vt else ""
                ev = (
                    f'xref:0x{bl_site:08X} -> native=0x{native_fn:08X} '
                    f'(exec=0x{exec_fn:08X}{vt_note}, string:0x{f_name_ptr:08X} "{f_name}") bytes:{fn_bytes}'
                )
            elif exec_fn:
                target_rva = exec_fn
                fn_type = "UFunction_Exec"
                conf = "string"
                fn_size, fn_size_src = estimate_function_size(buf, layout, target_rva)
                fn_bytes = prologue_hex(buf, target_rva, 16)
                if vt_off:
                    ev = (
                        f'string:0x{f_name_ptr:08X} "{f_name}" -> exec=0x{exec_fn:08X} '
                        f'[vtable+0x{vt_off:03X} @ 0x{bl_site:08X}] bytes:{fn_bytes}'
                    )
                else:
                    ev = f'string:0x{f_name_ptr:08X} "{f_name}" -> exec=0x{exec_fn:08X} bytes:{fn_bytes}'
            else:
                target_rva = f_ctor
                conf = "inferred"
                fn_type = "UFunction_Thunk"
                fn_size, _ = estimate_function_size(buf, layout, target_rva)
                fn_size_src = "thunk"
                fn_bytes = prologue_hex(buf, target_rva, 16)
                ev = f'string:0x{f_name_ptr:08X} "{f_name}" -> Z_Construct=0x{f_ctor:08X} bytes:{fn_bytes}'

            members.append(
                {
                    "class": cls_name,
                    "member": f_name,
                    "rva": f"0x{target_rva:08X}",
                    "size": fn_size,
                    "size_source": fn_size_src,
                    "type": fn_type,
                    "evidence": ev,
                    "confidence": conf,
                }
            )

        if vt_base:
            members.append(
                {
                    "class": cls_name,
                    "member": "__vtable__",
                    "rva": f"0x{vt_base:08X}",
                    "size": len(vt_entries) * 8,
                    "size_source": "vtable",
                    "type": f"vtable[{len(vt_entries)}]",
                    "evidence": f"xref:0x{vt_base:08X} -> slot[0]=0x{vt_entries[0][1]:08X}",
                    "confidence": "xref",
                }
            )
            for slot_off, slot_fn in vt_entries[:16]:
                slot_bytes = prologue_hex(buf, slot_fn, 16)
                slot_sz, slot_sz_src = estimate_function_size(buf, layout, slot_fn)
                members.append(
                    {
                        "class": cls_name,
                        "member": f"vfunc_+0x{slot_off:03X}",
                        "rva": f"0x{slot_fn:08X}",
                        "size": slot_sz,
                        "size_source": slot_sz_src,
                        "type": "vtable_slot",
                        "evidence": f"xref:0x{vt_base + slot_off:08X} (vtable+0x{slot_off:03X}) bytes:{slot_bytes}",
                        "confidence": "xref",
                    }
                )

        for dom in matched_domains:
            out[dom].extend(members)

    return out


def render_markdown(header: Dict[str, Any], domain_data: Dict[str, List[Dict[str, Any]]]) -> str:
    lines: List[str] = [
        "# UE4 Domain Offset Dump",
        "",
        f"- **libUE4.so SHA-256:** `{header['libue4_sha256']}`",
        f"- **Game Version:** `{header['game_version']}`",
        f"- **Date:** `{header['date']}`",
        f"- **Domains:** `{', '.join(header['domains'])}`",
        "",
    ]
    for dom in header["domains"]:
        entries = domain_data.get(dom, [])
        lines.append(f"## Domain: `{dom}`")
        lines.append("")
        if not entries:
            lines.append("_No matching classes._")
            lines.append("")
            continue
        by_class: Dict[str, List[Dict[str, Any]]] = {}
        for item in entries:
            by_class.setdefault(item["class"], []).append(item)
        for cls_name, cls_items in by_class.items():
            lines.append(f"### `{cls_name}`")
            lines.append("")
            lines.append("| Class | Member | RVA | Size | Size Source | Type | Evidence | Confidence |")
            lines.append("| :--- | :--- | :--- | ---: | :--- | :--- | :--- | :--- |")
            for it in cls_items:
                ev = str(it["evidence"]).replace("|", "\\|")
                lines.append(
                    f"| `{it['class']}` | `{it['member']}` | `{it['rva']}` | {it['size']} | `{it.get('size_source', 'none')}` | `{it['type']}` | `{ev}` | `{it['confidence']}` |"
                )
            lines.append("")
    return "\n".join(lines)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="UE4.27 .i64 / ELF64 Domain Dumper")
    parser.add_argument(
        "--i64",
        type=Path,
        default=Path("binaries/libUE4.so.i64"),
        help="Path to .i64 database inside binaries/",
    )
    parser.add_argument(
        "--so",
        type=Path,
        default=None,
        help="Optional explicit path to companion libUE4.so / libUE4_fixed.so",
    )
    parser.add_argument(
        "--domains",
        nargs="+",
        default=list(DEFAULT_DOMAIN_PATTERNS.keys()),
        help="Domain names or domain=pattern1|pattern2 specifications",
    )
    parser.add_argument(
        "--game-version",
        default="com.netease.dbdena-292949-UE4.27",
        help="Game version identifier for header block",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("docs/offsets"),
        help="Output directory for offsets.json and offsets.md",
    )
    args = parser.parse_args(argv)

    so_path = args.so if args.so is not None else resolve_so_from_i64(args.i64)
    sha256_hex = compute_sha256(so_path)
    domain_specs = parse_domain_arg(args.domains)
    buf = apply_rela_in_memory(so_path.read_bytes())
    layout = parse_elf_layout(buf)

    header = {
        "libue4_sha256": sha256_hex,
        "game_version": args.game_version,
        "date": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "domains": list(domain_specs.keys()),
        "i64_database": str(args.i64),
        "so_binary": str(so_path),
        "elf_sections": {
            "text": [f"0x{layout.text_start:08X}", f"0x{layout.text_end:08X}"],
            "data_rel_ro": [f"0x{layout.data_rel_ro_start:08X}", f"0x{layout.data_rel_ro_end:08X}"],
            "data": [f"0x{layout.data_start:08X}", f"0x{layout.data_end:08X}"],
            "bss": [f"0x{layout.bss_start:08X}", f"0x{layout.bss_end:08X}"],
        },
    }

    domain_data = dump_domains(buf, layout, domain_specs)
    json_payload: Dict[str, Any] = {"_header": header}
    json_payload.update(domain_data)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    json_path = args.out_dir / "offsets.json"
    md_path = args.out_dir / "offsets.md"

    json_path.write_text(json.dumps(json_payload, indent=2), encoding="utf-8")
    md_path.write_text(render_markdown(header, domain_data), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
