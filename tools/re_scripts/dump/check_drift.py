#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any, Dict, Optional, Sequence, Tuple

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from elf_layout import (  # type: ignore
        ElfLayout,
        apply_rela_in_memory,
        lookup_fde_entry,
        parse_elf_layout,
    )
else:
    from .elf_layout import (
        ElfLayout,
        apply_rela_in_memory,
        lookup_fde_entry,
        parse_elf_layout,
    )


RE_FPROP = re.compile(r"FPropertyParams@0x([0-9A-Fa-f]+)\+0x24=0x([0-9A-Fa-f]+)")
RE_SETBIT = re.compile(r"xref:0x([0-9A-Fa-f]+)\s+ldr[bh]?/str[bh]?\s+\[x0,#0x([0-9A-Fa-f]+)\]")
RE_STRING = re.compile(r'string:0x([0-9A-Fa-f]+)\s+"([^"]+)"')
RE_VTABLE_ROOT = re.compile(r"slot\[0\]=0x([0-9A-Fa-f]+)")
RE_VTABLE_SLOT = re.compile(r"xref:0x([0-9A-Fa-f]+)\s+\(vtable\+0x([0-9A-Fa-f]+)\)")


def compute_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def compute_sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_cstr(buf: bytes, va: int, max_len: int = 160) -> str:
    if va <= 0 or va >= len(buf):
        return ""
    end = buf.find(b"\x00", va, min(len(buf), va + max_len))
    if end == -1 or end == va:
        return ""
    try:
        return buf[va:end].decode("ascii")
    except Exception:
        return ""


def verify_fde_function(
    buf: bytes,
    layout: ElfLayout,
    rva: int,
    expected_size: int,
    size_source: str,
    confidence: str,
    label: str,
) -> Tuple[str, str]:
    if rva < 0 or rva + 4 > len(buf) or not (layout.text_start <= rva < layout.text_end):
        return (
            "MISSING",
            f"{label} function RVA 0x{rva:08X} outside .text [0x{layout.text_start:08X}..0x{layout.text_end:08X})",
        )
    insn = struct.unpack_from("<I", buf, rva)[0]
    if insn in (0, 0xFFFFFFFF):
        return "MISSING", f"{label} invalid instruction 0x{insn:08X} at 0x{rva:08X}"

    fde_size, is_mid_fde = lookup_fde_entry(buf, layout, rva)
    if size_source == "fde":
        if fde_size is None:
            if is_mid_fde and confidence == "inferred":
                return "UNVERIFIED", f"{label} RVA 0x{rva:08X} lands inside an existing FDE (inferred)"
            return "CHANGED", f"{label} RVA 0x{rva:08X} is not an FDE start in .eh_frame_hdr"
        if fde_size != expected_size:
            return (
                "CHANGED",
                f"{label} FDE@0x{rva:08X} pc_range expected {expected_size} (0x{expected_size:X}), got {fde_size} (0x{fde_size:X})",
            )
    elif is_mid_fde:
        if confidence == "inferred":
            return "UNVERIFIED", f"{label} RVA 0x{rva:08X} lands inside an existing FDE (inferred)"
        return "CHANGED", f"{label} RVA 0x{rva:08X} lands inside an existing FDE instead of its start"
    return "OK", ""


def verify_member(buf: bytes, layout: ElfLayout, item: Dict[str, Any]) -> Tuple[str, str]:
    cls_name = item.get("class", "<unknown>")
    member = item.get("member", "<unknown>")
    rva_str = item.get("rva", "0x0")
    mtype = item.get("type", "")
    expected_size = int(item.get("size", 0) or 0)
    size_source = str(item.get("size_source", "none"))
    confidence = str(item.get("confidence", "inferred")).lower()
    evidence = str(item.get("evidence", ""))
    label = f"{cls_name}::{member}"

    try:
        rva = int(rva_str, 16)
    except ValueError:
        return "MISSING", f"{label} invalid RVA '{rva_str}'"

    m_str = RE_STRING.search(evidence)
    if m_str:
        str_va = int(m_str.group(1), 16)
        expected_str = m_str.group(2)
        actual_str = read_cstr(buf, str_va)
        if actual_str != expected_str:
            return "CHANGED", f'{label} string@0x{str_va:08X} expected "{expected_str}", got "{actual_str}"'

    if mtype == "UClass*()" or mtype.startswith("UFunction"):
        return verify_fde_function(buf, layout, rva, expected_size, size_source, confidence, label)

    if mtype.startswith("vtable["):
        if rva < 0 or rva + 8 > len(buf) or not (layout.data_rel_ro_start <= rva < layout.data_rel_ro_end):
            return (
                "MISSING",
                f"{label} vtable RVA 0x{rva:08X} outside .data.rel.ro [0x{layout.data_rel_ro_start:08X}..0x{layout.data_rel_ro_end:08X})",
            )
        slot0 = struct.unpack_from("<Q", buf, rva)[0]
        m_root = RE_VTABLE_ROOT.search(evidence)
        if not m_root:
            return "UNVERIFIED", f"{label} vtable@0x{rva:08X} missing slot[0] anchor in evidence"
        expected_slot0 = int(m_root.group(1), 16)
        if slot0 != expected_slot0:
            return "CHANGED", f"{label} vtable@0x{rva:08X} slot[0] expected 0x{expected_slot0:08X}, got 0x{slot0:08X}"
        if not (layout.text_start <= slot0 < layout.text_end):
            return "CHANGED", f"{label} vtable@0x{rva:08X} slot[0]=0x{slot0:08X} outside .text"
        return "OK", ""

    if mtype == "vtable_slot":
        m_slot = RE_VTABLE_SLOT.search(evidence)
        if not m_slot:
            return "UNVERIFIED", f"{label} missing vtable slot xref in evidence"
        slot_va = int(m_slot.group(1), 16)
        if slot_va < 0 or slot_va + 8 > len(buf) or not (layout.data_rel_ro_start <= slot_va < layout.data_rel_ro_end):
            return "MISSING", f"{label} slot address 0x{slot_va:08X} outside .data.rel.ro"
        actual_fn = struct.unpack_from("<Q", buf, slot_va)[0]
        if actual_fn != rva:
            return "CHANGED", f"{label} slot@0x{slot_va:08X} expected 0x{rva:08X}, got 0x{actual_fn:08X}"
        return verify_fde_function(buf, layout, rva, expected_size, size_source, confidence, label)

    m_fprop = RE_FPROP.search(evidence)
    if m_fprop:
        prop_ptr = int(m_fprop.group(1), 16)
        if prop_ptr < 0 or prop_ptr + 0x28 > len(buf) or not layout.is_in_data(prop_ptr, 0x28):
            return "MISSING", f"{label} FPropertyParams@0x{prop_ptr:08X} outside .data.rel.ro/.data"
        actual_off = struct.unpack_from("<I", buf, prop_ptr + 0x24)[0]
        if actual_off != rva:
            return "CHANGED", f"{label} offset at 0x{prop_ptr:08X}+0x24 expected 0x{rva:04X}, got 0x{actual_off:04X}"
        return "OK", ""

    m_sb = RE_SETBIT.search(evidence)
    if m_sb:
        insn_va = int(m_sb.group(1), 16)
        if insn_va < 0 or insn_va + 4 > len(buf) or not (layout.text_start <= insn_va < layout.text_end):
            return "MISSING", f"{label} SetBit instruction@0x{insn_va:08X} outside .text"
        insn = struct.unpack_from("<I", buf, insn_va)[0]
        if (insn & 0xFFC003E0) not in (0x39400000, 0x39000000, 0x79400000, 0x79000000, 0xB9400000, 0xB9000000):
            return "CHANGED", f"{label} SetBit instruction@0x{insn_va:08X} mutated (0x{insn:08X})"
        return "OK", ""

    return "UNVERIFIED", f"{label} no verifiable evidence pattern matched ({evidence})"


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Verify offsets.json against current libUE4.so binary")
    parser.add_argument(
        "--offsets",
        type=Path,
        default=Path("docs/offsets/offsets.json"),
        help="Path to offsets.json",
    )
    parser.add_argument(
        "--so",
        type=Path,
        default=None,
        help="Path to current .so binary (defaults to _header.so_binary or binaries/libUE4_fixed.so)",
    )
    parser.add_argument(
        "--allow-unverified",
        action="store_true",
        help="Treat UNVERIFIED xref/string members as warnings instead of build failures",
    )
    args = parser.parse_args(argv)

    if not args.offsets.is_file():
        print(f"WARN: offsets file not found at {args.offsets}; skipping drift check.")
        return 0

    try:
        payload = json.loads(args.offsets.read_text(encoding="utf-8"))
        header = payload.get("_header", {})

        so_path = args.so
        if so_path is None:
            hdr_so = header.get("so_binary")
            if hdr_so and Path(hdr_so).is_file():
                so_path = Path(hdr_so)
            elif Path("binaries/libUE4_fixed.so").is_file():
                so_path = Path("binaries/libUE4_fixed.so")
            else:
                so_path = Path("binaries/libUE4.so")

        if not so_path.is_file():
            print(f"MISSING: target ELF binary not found at {so_path}")
            return 1

        buf = apply_rela_in_memory(so_path.read_bytes())
        layout = parse_elf_layout(buf)
        drift_found = False

        expected_sha = header.get("libue4_sha256")
        actual_sha = compute_sha256(so_path)
        if expected_sha and expected_sha != actual_sha:
            patched_sha = compute_sha256_bytes(buf)
            if expected_sha != patched_sha:
                print(f"CHANGED: binary SHA-256 expected {expected_sha}, got {actual_sha}")
                drift_found = True

        checked = 0
        changed_count = 0
        missing_count = 0
        unverified_strict_count = 0
        unverified_inferred_count = 0

        for domain, items in payload.items():
            if domain.startswith("_") or not isinstance(items, list):
                continue
            for item in items:
                checked += 1
                confidence = str(item.get("confidence", "inferred")).lower()
                status, detail = verify_member(buf, layout, item)
                if status == "CHANGED":
                    changed_count += 1
                    print(f"CHANGED: [{domain}] {detail}")
                    drift_found = True
                elif status == "MISSING":
                    missing_count += 1
                    print(f"MISSING: [{domain}] {detail}")
                    drift_found = True
                elif status == "UNVERIFIED":
                    if confidence in ("xref", "string"):
                        unverified_strict_count += 1
                        print(f"UNVERIFIED({confidence}): [{domain}] {detail}")
                        if not args.allow_unverified:
                            drift_found = True
                    else:
                        unverified_inferred_count += 1
                        print(f"WARN(inferred-unverified): [{domain}] {detail}")
    except (ValueError, OSError, struct.error) as exc:
        print(f"INTERNAL ERROR: {exc}")
        return 2

    if drift_found:
        print(
            f"DRIFT DETECTED: checked={checked} changed={changed_count} "
            f"missing={missing_count} unverified_strict={unverified_strict_count} "
            f"unverified_inferred={unverified_inferred_count}"
        )
        return 1

    print(
        f"OK: {checked} members verified against {so_path} "
        f"(changed=0, missing=0, unverified_strict={unverified_strict_count}, "
        f"unverified_inferred={unverified_inferred_count})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
