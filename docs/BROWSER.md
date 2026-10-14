# Lako Wave

Lako Wave is the LakoOS browser: a Chromium build packaged with LakoOS defaults
(start page, no first-run prompts, per-user profile in `%LOCALAPPDATA%\LakoWave`).

Chromium itself is about 360 MB, so it is not stored in the ISO. Build the package
on any machine:

    tools/package-browser.sh LakoWave

Copy the `LakoWave` folder to the target and run `LakoWave.cmd`.

## Status

- Verified: Chromium r1717716 (158.0) starts on LakoOS amd64, renders pages and loads HTTPS sites with `--no-sandbox --disable-gpu`.
- Known problems: no GPU acceleration (no accelerated OpenGL/D3D path yet), the sandbox does not start, capital letters can be lost while the browser is busy loading a page (Shift state race under load), software rendering is slow.
- Not done: a Chromium rebuilt from source with LakoOS branding, an installer, update channel.
