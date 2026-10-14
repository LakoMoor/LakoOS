# LakoOS Mobile shell (experimental)

Branch: `experimental/mobile`. Works on amd64 and arm64 (same shell code).

Enable:

    reg add HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced /v ShellStyle /t REG_DWORD /d 12 /f

Then open Start. Back to desktop style: set `ShellStyle` to 11 (centered), 10 (list and tiles) or 7 (classic).

## Done

- Full-screen Start with a grid of large coloured tiles (2 columns portrait, 4 landscape), wheel scrolling, power button.

## Not done

- Touch gestures and on-screen keyboard auto-show.
- Bottom navigation bar (Back, Start, Search) instead of the taskbar.
- Maximised-only app windows and an app switcher.
- Orientation and scaling rules for small screens.
