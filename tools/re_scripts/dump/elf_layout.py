#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import List, Optional, Tuple

try:
    import ida_funcs  # type: ignore
    HAS_IDA = True
except ImportError:
    HAS_IDA = False


PT_LOAD = 1
PT_GNU_EH_FRAME = 0x6474E550
SHT_RELA = 4
R_AARCH64_RELATIVE = 1027


def apply_rela_in_memory(buf: bytes) -> bytes:
    if len(buf) < 64 or buf[:4] != b"\x7fELF" or buf[4] != 2:
        return buf

    e_shoff = struct.unpack_from("<Q", buf, 40)[0]
    e_shentsize, e_shnum = struct.unpack_from("<HH", buf, 58)
    if not e_shoff or not e_shnum or e_shoff + e_shnum * e_shentsize > len(buf):
        return buf

    mutable_buf: Optional[bytearray] = None
    buf_len = len(buf)

    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        _sh_name, sh_type, _sh_flags, _sh_addr, sh_offset, sh_size = struct.unpack_from(
            "<IIQQQQ", buf, sh
        )
        if sh_type != SHT_RELA:
            continue
        sec_end = min(buf_len, int(sh_offset + sh_size))
        for off in range(int(sh_offset), sec_end - 23, 24):
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", buf, off)
            if (r_info & 0xFFFFFFFF) == R_AARCH64_RELATIVE and 0 <= r_offset <= buf_len - 8:
                if struct.unpack_from("<Q", buf, r_offset)[0] == 0:
                    if mutable_buf is None:
                        mutable_buf = bytearray(buf)
                    struct.pack_into("<Q", mutable_buf, r_offset, r_addend & 0xFFFFFFFFFFFFFFFF)

    return bytes(mutable_buf) if mutable_buf is not None else buf



@dataclass(frozen=True)
class ElfLayout:
    text_start: int
    text_end: int
    plt_start: int
    plt_end: int
    data_rel_ro_start: int
    data_rel_ro_end: int
    data_start: int
    data_end: int
    bss_start: int
    bss_end: int
    bss_va_start: int
    bss_va_end: int
    va_delta: int
    eh_frame_hdr_off: int
    eh_frame_hdr_size: int
    fde_count: int

    def offset_to_va(self, file_offset: int) -> int:
        return file_offset + self.va_delta

    def data_ranges(self) -> List[Tuple[int, int]]:
        ranges: List[Tuple[int, int]] = []
        if self.data_rel_ro_start >= 0 and self.data_rel_ro_end > self.data_rel_ro_start:
            ranges.append((self.data_rel_ro_start, self.data_rel_ro_end))
        if self.data_start >= 0 and self.data_end > self.data_start:
            ranges.append((self.data_start, self.data_end))
        return ranges

    def is_in_data(self, offset: int, min_bytes: int = 1) -> bool:
        for start, end in self.data_ranges():
            if start <= offset and offset + min_bytes <= end:
                return True
        return False


def parse_elf_layout(buf: bytes) -> ElfLayout:
    if len(buf) < 64 or buf[:4] != b"\x7fELF" or buf[4] != 2:
        raise ValueError("Target binary is not a valid 64-bit ELF file")

    e_phoff, e_shoff = struct.unpack_from("<QQ", buf, 32)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHHHH", buf, 54)

    text_start = -1
    text_end = -1
    plt_start = -1
    plt_end = -1
    data_rel_ro_start = -1
    data_rel_ro_end = -1
    data_start = -1
    data_end = -1
    bss_start = -1
    bss_end = -1
    bss_va_start = -1
    bss_va_end = -1
    va_delta: Optional[int] = None
    eh_hdr_off = -1
    eh_hdr_sz = 0

    if e_shoff and e_shnum and e_shstrndx < e_shnum and e_shoff + e_shnum * e_shentsize <= len(buf):
        shstr_hdr = e_shoff + e_shstrndx * e_shentsize
        shstr_off = struct.unpack_from("<Q", buf, shstr_hdr + 24)[0]
        for i in range(e_shnum):
            sh = e_shoff + i * e_shentsize
            sh_name, _sh_type, _sh_flags, sh_addr, sh_offset, sh_size = struct.unpack_from("<IIQQQQ", buf, sh)
            name_end = buf.find(b"\x00", shstr_off + sh_name, min(len(buf), shstr_off + sh_name + 64))
            sec_name = (
                buf[shstr_off + sh_name : name_end].decode("ascii", errors="ignore")
                if name_end != -1
                else ""
            )
            if sec_name == ".text":
                text_start = int(sh_offset)
                text_end = int(sh_offset + sh_size)
                if va_delta is None:
                    va_delta = int(sh_addr) - int(sh_offset)
            elif sec_name == ".plt":
                plt_start = int(sh_offset)
                plt_end = int(sh_offset + sh_size)
            elif sec_name == ".data.rel.ro":
                data_rel_ro_start = int(sh_offset)
                data_rel_ro_end = int(sh_offset + sh_size)
                if va_delta is None:
                    va_delta = int(sh_addr) - int(sh_offset)
            elif sec_name == ".data":
                data_start = int(sh_offset)
                data_end = int(sh_offset + sh_size)
            elif sec_name == ".bss":
                bss_start = int(sh_offset)
                bss_end = int(sh_offset + sh_size)
                if sh_addr > 0 or sh_size == 0:
                    bss_va_start = int(sh_addr)
                    bss_va_end = int(sh_addr + sh_size)
            elif sec_name == ".eh_frame_hdr":
                eh_hdr_off = int(sh_offset)
                eh_hdr_sz = int(sh_size)

    if (text_start < 0 or data_rel_ro_start < 0 or bss_va_start < 0) and e_phoff and e_phnum:
        rw_segments: List[Tuple[int, int, int, int]] = []
        for i in range(e_phnum):
            ph = e_phoff + i * e_phentsize
            if ph + 56 > len(buf):
                break
            p_type, p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, _p_align = struct.unpack_from(
                "<IIQQQQQQ", buf, ph
            )
            if p_type == PT_LOAD:
                if (p_flags & 0x1) and text_start < 0:
                    text_start = int(p_offset)
                    text_end = int(p_offset + p_filesz)
                    if va_delta is None:
                        va_delta = int(p_vaddr) - int(p_offset)
                elif (p_flags & 0x2) and p_filesz > 0:
                    rw_segments.append(
                        (
                            int(p_offset),
                            int(p_offset + p_filesz),
                            int(p_vaddr + p_filesz),
                            int(p_vaddr + p_memsz),
                        )
                    )
            elif p_type == PT_GNU_EH_FRAME and eh_hdr_off < 0:
                eh_hdr_off = int(p_offset)
                eh_hdr_sz = int(p_filesz)
        if rw_segments:
            if data_rel_ro_start < 0:
                data_rel_ro_start, data_rel_ro_end, _, _ = rw_segments[0]
            if len(rw_segments) > 1:
                if data_start < 0:
                    data_start, data_end, _, _ = rw_segments[-1]
                if bss_va_start < 0:
                    _, seg_file_end, seg_bss_va_start, seg_bss_va_end = rw_segments[-1]
                    bss_va_start = seg_bss_va_start
                    bss_va_end = seg_bss_va_end
                    bss_start = seg_file_end
                    bss_end = seg_file_end + max(0, seg_bss_va_end - seg_bss_va_start)

    if text_start < 0 or data_rel_ro_start < 0:
        raise ValueError("Failed to derive .text and .data.rel.ro ranges from ELF headers")

    if text_end > data_rel_ro_start:
        text_end = data_rel_ro_start

    resolved_va_delta = va_delta if va_delta is not None else 0

    fde_cnt = 0
    if eh_hdr_off >= 0 and eh_hdr_off + 12 <= len(buf):
        ver, eh_ptr_enc, fde_cnt_enc, tab_enc = struct.unpack_from("<BBBB", buf, eh_hdr_off)
        if ver == 1 and eh_ptr_enc == 0x1B and fde_cnt_enc == 0x03 and tab_enc == 0x3B:
            fde_cnt = int(struct.unpack_from("<I", buf, eh_hdr_off + 8)[0])

    resolved_bss_start = bss_start if bss_start >= 0 else (data_end if data_end >= 0 else data_rel_ro_end)
    resolved_bss_va_start = (
        bss_va_start if bss_va_start >= 0 else (resolved_bss_start + resolved_va_delta)
    )
    resolved_bss_va_end = bss_va_end if bss_va_end >= resolved_bss_va_start else resolved_bss_va_start
    resolved_bss_end = (
        bss_end
        if bss_end >= resolved_bss_start
        else resolved_bss_start + (resolved_bss_va_end - resolved_bss_va_start)
    )

    return ElfLayout(
        text_start=text_start,
        text_end=text_end,
        plt_start=plt_start,
        plt_end=plt_end,
        data_rel_ro_start=data_rel_ro_start,
        data_rel_ro_end=data_rel_ro_end,
        data_start=data_start,
        data_end=data_end,
        bss_start=resolved_bss_start,
        bss_end=resolved_bss_end,
        bss_va_start=resolved_bss_va_start,
        bss_va_end=resolved_bss_va_end,
        va_delta=resolved_va_delta,
        eh_frame_hdr_off=eh_hdr_off,
        eh_frame_hdr_size=eh_hdr_sz,
        fde_count=fde_cnt,
    )


def lookup_fde_entry(buf: bytes, layout: ElfLayout, fn_va: int) -> Tuple[Optional[int], bool]:
    if layout.fde_count <= 0 or layout.eh_frame_hdr_off < 0:
        return None, False
    tab_base = layout.eh_frame_hdr_off + 12
    lo = 0
    hi = layout.fde_count - 1
    hdr_off = layout.eh_frame_hdr_off
    aborted_early = False
    while lo <= hi:
        mid = (lo + hi) >> 1
        entry_off = tab_base + mid * 8
        if entry_off + 8 > len(buf):
            aborted_early = True
            break
        init_rel, fde_rel = struct.unpack_from("<ii", buf, entry_off)
        pc_begin = hdr_off + init_rel
        if pc_begin == fn_va:
            fde_addr = hdr_off + fde_rel
            if 0 <= fde_addr and fde_addr + 16 <= len(buf):
                _pc_rel, pc_range = struct.unpack_from("<ii", buf, fde_addr + 8)
                if 0 < pc_range <= 0x100000:
                    return int(pc_range), False
            return None, False
        elif pc_begin < fn_va:
            lo = mid + 1
        else:
            hi = mid - 1

    if not aborted_early and hi >= 0:
        prev_off = tab_base + hi * 8
        if prev_off + 8 <= len(buf):
            init_rel, fde_rel = struct.unpack_from("<ii", buf, prev_off)
            pc_begin = hdr_off + init_rel
            fde_addr = hdr_off + fde_rel
            if 0 <= fde_addr and fde_addr + 16 <= len(buf):
                _pc_rel, pc_range = struct.unpack_from("<ii", buf, fde_addr + 8)
                if 0 < pc_range <= 0x100000 and pc_begin < fn_va < pc_begin + pc_range:
                    return None, True
    return None, False


def lookup_fde_function_size(buf: bytes, layout: ElfLayout, fn_va: int) -> Optional[int]:
    fde_size, _mid_fde = lookup_fde_entry(buf, layout, fn_va)
    return fde_size


def scan_ret_function_size(buf: bytes, layout: ElfLayout, fn_va: int) -> Optional[int]:
    max_scan = min(fn_va + 0x1000, layout.text_end, len(buf))
    for off in range(fn_va + 4, max_scan, 4):
        insn = struct.unpack_from("<I", buf, off)[0]
        if insn in (0xD65F03C0, 0xD65F0FFF):
            return (off + 4) - fn_va
    return None


def estimate_function_size(buf: bytes, layout: ElfLayout, fn_va: int) -> Tuple[int, str]:
    if not (layout.text_start <= fn_va < layout.text_end):
        return 0, "none"
    fde_sz = lookup_fde_function_size(buf, layout, fn_va)
    if fde_sz is not None and fde_sz > 0:
        return fde_sz, "fde"
    if HAS_IDA:
        f = ida_funcs.get_func(fn_va)
        if f is not None and f.end_ea > f.start_ea:
            return int(f.end_ea - f.start_ea), "ida"
    ret_sz = scan_ret_function_size(buf, layout, fn_va)
    if ret_sz is not None and ret_sz > 0:
        return ret_sz, "ret"
    return 0, "none"
