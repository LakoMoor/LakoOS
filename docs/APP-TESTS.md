# Application test log

Run in QEMU (x86-64, TCG, 2 GB RAM, e1000 user networking) from the LakoOS live image.
Applications were placed on a FAT disk image and started from `cmd.exe`.

| Application | Result |
|---|---|
| Chocolate Doom 3.0.1 (32-bit, SDL2, WoW64) | Runs and is playable with `SDL_RENDER_DRIVER=software`. Without it: "Couldn't find matching render driver". |
| OpenTTD 14.1 (64-bit, Win32 video driver) | Runs. Downloads its graphics over HTTPS. Reports "no compatible GPU found, hardware acceleration disabled". |
| Chromium r1717716 (158.0, 64-bit) | Starts with `--no-sandbox --disable-gpu`, shows the first-run page, loads HTTPS pages. Slow (software rendering). |
| Steam installer (32-bit, NSIS) | Installs. The client then stops with "Steam needs to be online to update". Network itself works (ipconfig shows address and gateway; Chromium and OpenTTD download). Cause not found yet. |

## Findings

1. No GPU acceleration: OpenGL and D3D renderers are not available to applications. SDL2 apps need the software renderer forced.
2. Shift works in the Chromium window when it is idle (tested with a quick and a 1.5 s hold). Capitals were lost only while Chromium was busy loading a page. A key-state race under load is suspected; not isolated.
3. With 4 GB of RAM the live image stops with `0x7E` in `usbport.sys` during boot (2 GB boots). Not yet investigated.
4. `secur32_winetest schannel`: 224 tests, 27 failures. The provider reports TLS 1.0/1.1/1.2 client only; inbound credentials, renegotiation and DTLS are missing.
5. `schannel_winetest`: 36 tests, 0 failures.
6. `winhttp_winetest winhttp` did not finish within 5 minutes.
7. East Asian glyphs render as boxes in the Steam setup language list (no CJK font).
8. The tests were built with `-DENABLE_ROSTESTS=TRUE` and run with `secur32_winetest.exe schannel`.

## Steam: network check

`nettest client-update.steamstatic.com` (new tool, `base/applications/cmdutils/nettest`) shows
WinINet and WinHTTP, over HTTP and HTTPS, reach the Steam update server (HTTP 403 for `/`, which is
what the server answers). So the "needs to be online" error does not come from these APIs.
Steam uses its own network stack; the cause is still open.
