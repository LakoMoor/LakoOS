# Xiaomi POCO F1 (beryllium, Snapdragon 845)

Status: profile only. Nothing here has been run on a phone. The only test was the
arm64 image in QEMU, which shows that the profile settings are applied.

## Boot path

The phone has no UEFI of its own. The community UEFI port for SDM845
([edk2-sdm845](https://github.com/wode2016501/edk2-sdm845), device `beryllium`)
is booted with fastboot and then loads an ARM64 UEFI OS from storage. LakoOS arm64
uses UEFI, so the same path applies:

1. Unlock the bootloader.
2. Build the image with the phone profile (below).
3. `fastboot boot` the edk2 image for beryllium (nothing is written to the phone).
4. Put the LakoOS ISO or a disk image on a USB OTG stick and boot from it.

Reference for the Windows ARM64 version of this setup:
[Renegade Project edk2 overview](https://renegade-doc.readthedocs.io/en/latest/edk2/Overview.html).

## Build

    ./configure.sh -a arm64 -DSDM845_SUPPORT=ON
    cd output-Clang-arm64-debug && ninja livecd

`SDM845_SUPPORT` adds `boot/bootdata/hivesys_sdm845.inf`:

| Setting | Value | Why |
|---|---|---|
| System DPI (`LogPixels`) | 240 (250 %) | 2246x1080 panel at about 400 ppi |
| `ShellStyle` | 12 | full-screen tile Start (needs the `experimental/mobile` branch) |

Verified in QEMU arm64: the whole shell scales to 250 %.

## What is missing for real hardware

| Area | State |
|---|---|
| Display | GOP framebuffer from edk2 is used; no panel or backlight driver |
| Storage | UFS controller driver does not exist; USB mass storage works as in QEMU |
| Touch | No driver for the Focaltech/Novatek panel controller; USB mouse and keyboard through OTG |
| USB | DWC3 driver does not exist |
| GPU | None (Adreno 630) |
| Wi-Fi, modem, audio, sensors, battery | None |
| Interrupts and timers | GICv3 and the generic timer are used by the arm64 port; not tried on this SoC |

Support for the board is therefore a long list of drivers, starting with UFS and DWC3.
