# LakoOS Mobile shell (experimental)

Branch: `experimental/mobile`. Works on amd64 and arm64 (same shell code).

Enable:

    reg add HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced /v ShellStyle /t REG_DWORD /d 12 /f

Then open Start. Back to desktop style: set `ShellStyle` to 11 (centered), 10 (list and tiles) or 7 (classic).

## Done

- Home screen with a large clock and date, coloured rounded tiles (a wide first tile, 2 columns portrait, 4 landscape), search bar at the bottom, wheel scrolling and a power button.
- Navigation bar in place of the taskbar: Back (Alt+Left), Home (Start) and Recents (Alt+Tab).
- Test frame: set `MobileWidth` and `MobileHeight` (DWORD, under `Explorer\Advanced`) to show the phone layout, for example 480x690, on a landscape screen.

## Not done

- Touch gestures and on-screen keyboard auto-show.
- Maximised-only app windows and an app switcher.
- Orientation and scaling rules for small screens.
