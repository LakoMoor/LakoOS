param(
    [string]$Arch = "amd64",
    [string]$Target = "livecd",
    [switch]$Release,
    [switch]$EnvOnly
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Write-Step($Text) { Write-Host "`n==> $Text" -ForegroundColor Cyan }

if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
    throw "WSL was not found. Run 'wsl --install -d Ubuntu' in an administrator terminal, restart, then run this script again."
}

$Distros = (wsl -l -q) -replace "`0", "" | Where-Object { $_.Trim() -ne "" }
if (-not $Distros) {
    Write-Step "Installing Ubuntu for WSL"
    wsl --install -d Ubuntu --no-launch
    throw "Ubuntu was installed. Finish its first-run setup, then run this script again."
}

Write-Step "Copying the source into the WSL file system"
$Linux = (wsl wslpath -a "$Repo").Trim()
$Work = '$HOME/lakoos'
wsl bash -lc "mkdir -p $Work && (command -v rsync >/dev/null && rsync -a --delete --exclude 'output-*' '$Linux/' $Work/ || cp -a '$Linux/.' $Work/)"

$Flags = "--arch $Arch --target $Target"
if ($Release) { $Flags += " --release" }
if ($EnvOnly) { $Flags += " --env-only" }

Write-Step "Building the toolchain and the system inside WSL"
wsl bash -lc "cd $Work && ./buildenv/build.sh $Flags"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $EnvOnly) {
    Write-Step "Copying the images back"
    $Out = Join-Path $Repo "dist"
    New-Item -ItemType Directory -Force $Out | Out-Null
    $OutLinux = (wsl wslpath -a "$Out").Trim()
    wsl bash -lc "cp -f $Work/output-*-$Arch-*/*.iso $Work/output-*-$Arch-*/*.img $Work/output-*-$Arch-*/*.vhd '$OutLinux/' 2>/dev/null; true"
    Write-Host "Images are in $Out"
}
