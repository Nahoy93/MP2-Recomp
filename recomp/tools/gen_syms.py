#!/usr/bin/env python3
"""Generate the symbol file N64Recomp reads, straight from the user's ROM.

N64Recomp needs to know where every function starts and how long it is. The
usual way to give it that is the ELF of a matching decompilation build, which
needs the IDO/KMC MIPS toolchain and, on Windows, WSL. This script avoids all of
that: it runs splat (the decomp's own splitter, pure Python) over the ROM with
the decomp's marioparty2.yaml and symbol_addrs.txt, then reads the function
boundaries out of the disassembly splat writes and emits N64Recomp's
"symbol file" format:

    [[section]]
    name = "main"
    rom = 0x1060
    vram = 0x80000460
    size = 0xB8130
    functions = [
        { name = "func_80000460_1060", vram = 0x80000460, size = 0x40 },
        ...
    ]

Nothing in the decompilation is modified. splat runs in a scratch directory
with an empty src/ so that it disassembles every function, decompiled or not.

Usage:
    python gen_syms.py --rom <your rom> --out <dir>

Writes into <dir>:
    baserom.z64        the ROM in big-endian (.z64) byte order
    mp2.us.syms.toml   the symbol file
    rom_info.h         the ROM's XXH3 hash for the launcher's ROM check
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

EXPECTED_SHA1 = "166eda1c05670d337e2c3f15a5db528ae1e5d6e3"  # Mario Party 2 (USA)

RECOMP_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = RECOMP_DIR.parent
DECOMP_YAML = REPO_DIR / "marioparty2.yaml"
DECOMP_SYMBOLS = REPO_DIR / "symbol_addrs.txt"
EXTRA_SYMBOLS = RECOMP_DIR / "config" / "extra_symbol_addrs.txt"


# --------------------------------------------------------------------- ROM --

def to_big_endian(data: bytes) -> bytes:
    """Return the ROM in .z64 (big-endian) order whatever order it came in."""
    magic = data[:4]
    if magic == b"\x80\x37\x12\x40":  # .z64
        return data
    if magic == b"\x37\x80\x40\x12":  # .v64, 16-bit byteswapped
        out = bytearray(len(data))
        out[0::2] = data[1::2]
        out[1::2] = data[0::2]
        return bytes(out)
    if magic == b"\x40\x12\x37\x80":  # .n64, 32-bit little-endian
        out = bytearray(len(data))
        out[0::4] = data[3::4]
        out[1::4] = data[2::4]
        out[2::4] = data[1::4]
        out[3::4] = data[0::4]
        return bytes(out)
    raise SystemExit("Det här ser inte ut som en N64-ROM (okänd header). / Not an N64 ROM.")


def xxh3_64(data: bytes) -> int | None:
    try:
        import xxhash  # type: ignore
    except ImportError:
        return None
    return xxhash.xxh3_64_intdigest(data)


# ------------------------------------------------------------------- splat --

def write_splat_config(work: Path) -> Path:
    """Copy the decomp's yaml into the scratch dir with paths pointed there."""
    import yaml  # PyYAML, a splat dependency

    with open(DECOMP_YAML, "r", encoding="utf-8") as f:
        config = yaml.safe_load(f)

    config.pop("sha1", None)  # checked by us, before splat runs
    opts = config["options"]
    opts["base_path"] = "."
    opts["target_path"] = "baserom.z64"
    symbol_files = [str(DECOMP_SYMBOLS)]
    if EXTRA_SYMBOLS.exists():
        symbol_files.append(str(EXTRA_SYMBOLS))
    opts["symbol_addrs_path"] = symbol_files
    opts["src_path"] = "src"          # empty: every function gets disassembled
    opts["asm_path"] = "asm"
    opts["build_path"] = "build"
    opts["ld_script_path"] = "marioparty2.ld"
    opts["undefined_funcs_auto_path"] = "undefined_funcs_auto.txt"
    opts["undefined_syms_auto_path"] = "undefined_syms_auto.txt"
    # Fixed label macros so the parser below does not depend on defaults.
    opts["asm_function_macro"] = "glabel"
    opts["asm_function_alt_macro"] = "alabel"
    opts["asm_jtbl_label_macro"] = "jlabel"
    opts["asm_data_macro"] = "dlabel"
    opts["asm_end_label"] = "endlabel"
    opts.pop("extensions_path", None)

    out = work / "mp2.splat.yaml"
    with open(out, "w", encoding="utf-8") as f:
        yaml.safe_dump(config, f, sort_keys=False)
    return out


def run_splat(work: Path, config_path: Path) -> None:
    cmd = [sys.executable, "-m", "splat", "split", config_path.name]
    print("[gen_syms] kör splat (det tar en stund) ...", flush=True)
    result = subprocess.run(cmd, cwd=work)
    if result.returncode != 0:
        raise SystemExit("splat misslyckades / splat failed (exit %d)" % result.returncode)


# ------------------------------------------------------------- disassembly --

INSTR_RE = re.compile(
    r"^\s*/\*\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*\*/\s*(\S+)"
)
LABEL_RE = re.compile(r"^\s*(glabel|endlabel)\s+(\S+)")
SECTION_RE = re.compile(r"^\s*\.section\s+(\S+)")


@dataclass
class Function:
    name: str
    rom: int
    vram: int
    size: int


def parse_asm_file(path: Path, out: dict[str, Function]) -> None:
    current: str | None = None
    first: tuple[int, int] | None = None
    last_vram = 0
    in_text = True

    def close() -> None:
        nonlocal current, first
        if current is not None and first is not None:
            rom, vram = first
            fn = Function(current, rom, vram, last_vram + 4 - vram)
            prev = out.get(current)
            if prev is None or prev.size < fn.size:
                out[current] = fn
        current = None
        first = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = SECTION_RE.match(line)
            if m:
                close()
                in_text = m.group(1).startswith(".text")
                continue
            m = LABEL_RE.match(line)
            if m:
                if m.group(1) == "glabel":
                    close()
                    if in_text:
                        current = m.group(2)
                else:
                    close()
                continue
            if current is None:
                continue
            m = INSTR_RE.match(line)
            if not m or m.group(4).startswith("."):
                continue
            rom = int(m.group(1), 16)
            vram = int(m.group(2), 16)
            if first is None:
                first = (rom, vram)
            last_vram = vram
    close()


def collect_functions(asm_dir: Path) -> dict[str, Function]:
    funcs: dict[str, Function] = {}
    for root, _dirs, files in os.walk(asm_dir):
        for name in files:
            if name.endswith(".s"):
                parse_asm_file(Path(root) / name, funcs)
    return funcs


# ---------------------------------------------------------------- sections --

@dataclass
class Segment:
    name: str
    rom_start: int
    rom_end: int
    vram: int
    functions: list[Function] = field(default_factory=list)


def read_code_segments() -> list[Segment]:
    import yaml

    with open(DECOMP_YAML, "r", encoding="utf-8") as f:
        config = yaml.safe_load(f)

    starts: list[tuple[int, dict | None]] = []
    for seg in config["segments"]:
        if isinstance(seg, dict):
            starts.append((int(seg["start"]), seg))
        elif isinstance(seg, list) and seg:
            starts.append((int(seg[0]), None))

    segments: list[Segment] = []
    for i, (start, seg) in enumerate(starts):
        if seg is None or seg.get("type") != "code":
            continue
        end = starts[i + 1][0] if i + 1 < len(starts) else start
        segments.append(Segment(str(seg["name"]), start, end, int(seg["vram"])))
    return segments


def assign(funcs: dict[str, Function], segments: list[Segment]) -> list[Function]:
    orphans: list[Function] = []
    for fn in funcs.values():
        for seg in segments:
            if seg.rom_start <= fn.rom < seg.rom_end:
                if fn.vram - seg.vram != fn.rom - seg.rom_start:
                    raise SystemExit(f"{fn.name}: rom/vram does not match segment {seg.name}")
                seg.functions.append(fn)
                break
        else:
            orphans.append(fn)
    return orphans


def write_symbol_file(path: Path, segments: list[Segment]) -> tuple[int, int]:
    sections = 0
    total = 0
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Generated by recomp/tools/gen_syms.py from your ROM. Do not edit.\n\n")
        for seg in segments:
            if not seg.functions:
                continue
            seg.functions.sort(key=lambda fn: fn.vram)
            first = seg.functions[0]
            end = max(fn.vram + fn.size for fn in seg.functions)
            f.write("[[section]]\n")
            f.write(f'name = "{seg.name}"\n')
            f.write(f"rom = 0x{first.rom:X}\n")
            f.write(f"vram = 0x{first.vram:08X}\n")
            f.write(f"size = 0x{end - first.vram:X}\n")
            f.write("functions = [\n")
            for fn in seg.functions:
                f.write(f'    {{ name = "{fn.name}", vram = 0x{fn.vram:08X}, size = 0x{fn.size:X} }},\n')
            f.write("]\n\n")
            sections += 1
            total += len(seg.functions)
    return sections, total


# -------------------------------------------------------------------- main --

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom", required=True, type=Path, help="Mario Party 2 (USA) ROM (.z64/.n64/.v64)")
    parser.add_argument("--out", required=True, type=Path, help="output directory")
    parser.add_argument("--keep-work", action="store_true", help="keep splat's scratch directory")
    parser.add_argument("--allow-wrong-rom", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    data = to_big_endian(args.rom.read_bytes())
    sha1 = hashlib.sha1(data).hexdigest()
    if sha1 != EXPECTED_SHA1:
        msg = (f"Fel ROM: SHA-1 är {sha1}, förväntad {EXPECTED_SHA1} (Mario Party 2, USA).\n"
               f"Wrong ROM: SHA-1 {sha1}, expected {EXPECTED_SHA1}.")
        if not args.allow_wrong_rom:
            raise SystemExit(msg)
        print("[gen_syms] VARNING: " + msg, file=sys.stderr)

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "baserom.z64").write_bytes(data)

    rom_hash = xxh3_64(data)
    with open(out / "rom_info.h", "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by recomp/tools/gen_syms.py. Do not edit.\n#pragma once\n")
        if rom_hash is None:
            f.write("#define MP2_ROM_HASH_KNOWN 0\n#define MP2_ROM_HASH 0ULL\n")
        else:
            f.write(f"#define MP2_ROM_HASH_KNOWN 1\n#define MP2_ROM_HASH 0x{rom_hash:016X}ULL\n")
    if rom_hash is None:
        print("[gen_syms] VARNING: python-paketet 'xxhash' saknas; ROM-kontrollen i spelet "
              "kommer att be dig välja ROM varje gång. (pip install xxhash)", file=sys.stderr)

    work = out / "splat_work"
    if work.exists():
        shutil.rmtree(work)
    (work / "src").mkdir(parents=True)
    shutil.copyfile(out / "baserom.z64", work / "baserom.z64")

    config_path = write_splat_config(work)
    run_splat(work, config_path)

    funcs = collect_functions(work / "asm")
    segments = read_code_segments()
    orphans = assign(funcs, segments)
    if orphans:
        print(f"[gen_syms] {len(orphans)} funktioner utanför kodsegment ignoreras "
              f"(t.ex. {orphans[0].name})", file=sys.stderr)

    if not any(fn.rom == 0x1000 for seg in segments for fn in seg.functions):
        raise SystemExit("Hittade ingen funktion på ROM 0x1000 (entrypoint). / No entrypoint found.")

    sections, total = write_symbol_file(out / "mp2.us.syms.toml", segments)
    print(f"[gen_syms] klart: {total} funktioner i {sections} sektioner -> {out / 'mp2.us.syms.toml'}")

    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
