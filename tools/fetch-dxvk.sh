#!/bin/sh
set -e
ver="${1:-v2.5.3}"
out="${2:-dxvk-$ver}"
mkdir -p "$out"
curl -fL "https://github.com/doitsujin/dxvk/releases/download/$ver/dxvk-${ver#v}.tar.gz" | tar -xz -C "$out" --strip-components=1
echo "DXVK $ver in $out (x64, x32). Needs a Vulkan driver; see docs/PROTON.md"
