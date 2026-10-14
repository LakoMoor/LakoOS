# LakoOS VM for Android

One APK that runs LakoOS (arm64 or amd64 image) in QEMU on an Android phone or tablet.

- QEMU 11.1 (TCG emulation, no KVM needed), UEFI firmware (EDK II) and all libraries are inside the APK.
- Built-in VNC viewer: touch is a mouse (two fingers = right click), the Keyboard button opens the soft keyboard.
- Choose an ISO from storage or download the live image from the LakoOS GitHub release.
- Optional 16 GB virtual disk for installing.

## Build

    python3 fetch-qemu.py          # downloads QEMU for Android into app/src/main/assets/qemu.zip
    gradle assembleRelease         # Android SDK 34, NDK not needed

## Notes

- Targets Android 8 (API 26) and newer, arm64 devices only. It targets SDK 28 on purpose so that the
  bundled QEMU may be executed from the app's private storage.
- Emulation is slow (software CPU). Expect minutes to boot. 3 GB or more of free memory recommended.
- Not tested on a physical device yet; built and checked only as an APK.
- QEMU is GPL-2.0 and comes from the Termux package repository
  (https://github.com/termux/termux-packages); QEMU sources: https://www.qemu.org.
