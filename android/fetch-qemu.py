#!/usr/bin/env python3
"""Builds app/src/main/assets/qemu.zip: QEMU and its libraries for Android (aarch64),
taken from the Termux package repository (GPL; sources are published by Termux and QEMU).
Needs: python3, curl, ar, tar, zip, readelf-like tool (llvm-readobj or readelf)."""
import os, re, shutil, subprocess, sys, tempfile

REPO = "https://packages.termux.dev/apt/termux-main/"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "app/src/main/assets/qemu.zip")
READ = shutil.which("llvm-readobj") or shutil.which("readelf")
work = tempfile.mkdtemp()
idx = subprocess.run(["curl", "-sL", REPO + "dists/stable/main/binary-aarch64/Packages"], capture_output=True, text=True).stdout
pk = {}
for b in idx.split("\n\n"):
    m = re.search(r"^Package: (.+)$", b, re.M)
    if m:
        d = re.search(r"^Depends: (.+)$", b, re.M)
        pk[m.group(1)] = (re.search(r"^Filename: (.+)$", b, re.M).group(1),
                          [x.split("|")[0].strip().split(" ")[0] for x in d.group(1).split(",")] if d else [])
seen, todo = [], ["qemu-system-aarch64-headless", "qemu-system-x86-64-headless"]
while todo:
    n = todo.pop()
    if n in seen or n not in pk:
        continue
    seen.append(n)
    todo += pk[n][1]
root = os.path.join(work, "root"); os.makedirs(root)
for n in seen:
    f = pk[n][0]
    deb = os.path.join(work, os.path.basename(f).replace(":", "_"))
    subprocess.run(["curl", "-sL", "-o", deb, REPO + f.replace(":", "%3a")], check=True)
    t = tempfile.mkdtemp()
    subprocess.run(["ar", "x", deb], cwd=t, check=True)
    for x in os.listdir(t):
        if x.startswith("data.tar"):
            subprocess.run(["tar", "-xf", os.path.join(t, x), "-C", root], check=True)
P = root + "/data/data/com.termux/files/usr"
libs = {}
for dp, _, fn in os.walk(P + "/lib"):
    for f in fn:
        if ".so" in f:
            libs[f] = os.path.join(dp, f)
def needed(f):
    o = subprocess.run([READ, "--needed-libs" if "llvm" in READ else "-d", f], capture_output=True, text=True).stdout
    return re.findall(r"(lib[\w.+-]+\.so[\w.]*)", o)
need, todo = set(), [P + "/bin/qemu-system-aarch64", P + "/bin/qemu-system-x86_64"]
while todo:
    f = todo.pop()
    for n in needed(f):
        if n not in need and n in libs:
            need.add(n); todo.append(os.path.realpath(libs[n]))
pay = os.path.join(work, "payload")
for d in ("bin", "lib", "share/qemu"):
    os.makedirs(os.path.join(pay, d))
for n in need:
    shutil.copy2(os.path.realpath(libs[n]), pay + "/lib/" + n)
for b in ("qemu-system-aarch64", "qemu-system-x86_64"):
    shutil.copy2(P + "/bin/" + b, pay + "/bin/" + b)
sh = P + "/share/qemu"
for f in ("edk2-aarch64-code.fd", "edk2-arm-vars.fd", "edk2-x86_64-code.fd", "edk2-i386-vars.fd", "bios-256k.bin",
          "vgabios-stdvga.bin", "vgabios-bochs-display.bin", "vgabios.bin", "efi-virtio.rom", "efi-e1000.rom",
          "kvmvapic.bin", "linuxboot_dma.bin"):
    if os.path.exists(sh + "/" + f):
        shutil.copy2(sh + "/" + f, pay + "/share/qemu/" + f)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
if os.path.exists(OUT):
    os.remove(OUT)
subprocess.run(["zip", "-q", "-r", "-9", OUT, "bin", "lib", "share"], cwd=pay, check=True)
print("wrote", OUT)
