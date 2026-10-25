# Build PS4 Save Remote .pkg via Docker.
# Output: .\dist\IV0000-PSRM00001_00-SAVEREMOTE000PS4.pkg

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host "Docker not found. See docs/BUILD.md"
    exit 1
}

docker info 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Docker engine not running. Start Docker Desktop and retry."
    exit 1
}

New-Item -ItemType Directory -Force -Path dist | Out-Null
Write-Host "Building image (first run downloads toolchain — can take 20-40+ min)..."
docker build --target build -t ps4-save-remote .
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

docker rm -f psr-out 2>$null | Out-Null
docker create --name psr-out ps4-save-remote | Out-Null
docker cp psr-out:/out/. ./dist/
docker rm psr-out | Out-Null

Get-ChildItem dist\*.pkg
if (-not (Get-ChildItem dist\*.pkg -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: no .pkg in dist/"
    exit 1
}
Write-Host "Done."
