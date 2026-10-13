#!/bin/sh
set -e
out="${1:-LakoWave}"
rev="${2:-$(curl -s https://commondatastorage.googleapis.com/chromium-browser-snapshots/Win_x64/LAST_CHANGE)}"
tmp="$(mktemp -d)"
curl -fL -o "$tmp/c.zip" "https://commondatastorage.googleapis.com/chromium-browser-snapshots/Win_x64/$rev/chrome-win.zip"
mkdir -p "$out"
unzip -q "$tmp/c.zip" -d "$tmp/x"
cp -r "$tmp/x/chrome-win/." "$out/"
rm -rf "$tmp" "$out"/*tests.exe "$out"/setup.exe
cp "$out/chrome.exe" "$out/lakowave.exe"
mkdir -p "$out/policies/managed"
cat > "$out/master_preferences" <<'J'
{
  "homepage": "https://lakomoor.github.io/LakoOS/",
  "homepage_is_newtabpage": false,
  "browser": {"show_home_button": true, "check_default_browser": false},
  "session": {"restore_on_startup": 4, "startup_urls": ["https://lakomoor.github.io/LakoOS/"]},
  "distribution": {"skip_first_run_ui": true, "suppress_first_run_default_browser_prompt": true, "make_chrome_default": false, "show_welcome_page": false}
}
J
cat > "$out/LakoWave.cmd" <<'J'
@echo off
start "" "%~dp0lakowave.exe" --no-sandbox --disable-gpu --user-data-dir="%LOCALAPPDATA%\LakoWave" %*
J
echo "Lako Wave packaged in $out (Chromium r$rev). Start it with LakoWave.cmd"
