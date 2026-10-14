#!/usr/bin/env python3
"""Static compatibility check for Windows drivers against a LakoOS build.

Usage: driver-compat.py BUILD_DIR driver.sys [more.sys ...]

For every import of the driver it looks the function up in the matching module of the
build (ntoskrnl.exe, hal.dll, ndis.sys, ...). It reports imports that are missing.
This proves nothing about run-time behaviour; it only finds drivers that cannot load.
"""
import os, sys
import pefile

MACHINE = {0x8664: "amd64", 0xAA64: "arm64", 0x14C: "i386"}

def build_exports(build):
    mods = {}
    for root, _, files in os.walk(build):
        if "/symbols" in root or "_wow64" in root or "CMakeFiles" in root:
            continue
        for f in files:
            if f.lower().endswith((".sys", ".dll", ".exe")):
                mods.setdefault(f.lower(), os.path.join(root, f))
    return mods

_cache = {}
def exports_of(path):
    if path in _cache:
        return _cache[path]
    names, ords = set(), set()
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        d = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
        if d:
            base = d.struct.Base
            for s in d.symbols:
                ords.add(s.ordinal)
                if s.name:
                    names.add(s.name.decode())
            _cache[path] = (names, ords)
            return _cache[path]
    except Exception:
        pass
    _cache[path] = (names, ords)
    return _cache[path]

def alias(dll, mods):
    d = dll.lower()
    for cand in (d, d + ".sys", d + ".dll", os.path.splitext(d)[0] + ".sys", os.path.splitext(d)[0] + ".dll",
                 os.path.splitext(d)[0] + ".exe"):
        if cand in mods:
            return mods[cand]
    return None

def check(build_mods, drv):
    pe = pefile.PE(drv)
    arch = MACHINE.get(pe.FILE_HEADER.Machine, hex(pe.FILE_HEADER.Machine))
    res = {"file": os.path.basename(drv), "arch": arch, "missing": [], "unknown_modules": [], "imports": 0}
    pe.parse_data_directories()
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) + getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
        dll = entry.dll.decode()
        path = alias(dll, build_mods)
        if path is None:
            res["unknown_modules"].append(dll)
            continue
        names, ords = exports_of(path)
        for imp in entry.imports:
            res["imports"] += 1
            if imp.name:
                if imp.name.decode() not in names:
                    res["missing"].append("%s!%s" % (dll, imp.name.decode()))
            elif imp.ordinal not in ords:
                res["missing"].append("%s!#%d" % (dll, imp.ordinal))
    return res

if __name__ == "__main__":
    build = sys.argv[1]
    mods = build_exports(build)
    for drv in sys.argv[2:]:
        d = os.path.dirname(os.path.abspath(drv))
        for f in os.listdir(d):
            if f.lower().endswith((".sys", ".dll")):
                mods.setdefault(f.lower(), os.path.join(d, f))
    bad = 0
    nt = mods.get("ntoskrnl.exe")
    build_arch = MACHINE.get(pefile.PE(nt, fast_load=True).FILE_HEADER.Machine) if nt else None
    for drv in sys.argv[2:]:
        r = check(mods, drv)
        verdict = "OK" if not r["missing"] and not r["unknown_modules"] else "INCOMPATIBLE"
        if build_arch and r["arch"] != build_arch:
            verdict = "WRONG-ARCH"
        if verdict != "OK":
            bad += 1
        print("%-12s %-28s %-6s imports=%d missing=%d" % (verdict, r["file"], r["arch"], r["imports"], len(r["missing"])))
        for m in sorted(set(r["unknown_modules"])):
            print("     module not in build:", m)
        for m in r["missing"][:40]:
            print("     missing:", m)
    sys.exit(1 if bad else 0)
