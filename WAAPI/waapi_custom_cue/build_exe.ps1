$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$DistRoot = Join-Path $Root "dist"
$PackageDir = Join-Path $DistRoot "WwiseCustomCueTool"
$ZipPath = Join-Path $DistRoot "WwiseCustomCueTool_Windows_x64.zip"
$EntryPoint = Join-Path $Root "music_segment_custom_cue_gui.py"

if (Test-Path -LiteralPath $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
if (Test-Path -LiteralPath $PackageDir) {
    Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null

python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --name WwiseCustomCueTool `
    --distpath $PackageDir `
    --workpath $BuildDir `
    --specpath $BuildDir `
    --collect-all waapi `
    --collect-all autobahn `
    $EntryPoint

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE."
}

Copy-Item -LiteralPath (Join-Path $Root "custom_cue_event_types.json") -Destination $PackageDir -Force
Copy-Item -LiteralPath (Join-Path $Root "README_STANDALONE.md") -Destination (Join-Path $PackageDir "README.md") -Force
Compress-Archive -Path (Join-Path $PackageDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal

Remove-Item -LiteralPath $BuildDir -Recurse -Force

$ExePath = Join-Path $PackageDir "WwiseCustomCueTool.exe"
$Hash = (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash
Write-Host "Built: $ExePath"
Write-Host "Package: $ZipPath"
Write-Host "SHA256: $Hash"
