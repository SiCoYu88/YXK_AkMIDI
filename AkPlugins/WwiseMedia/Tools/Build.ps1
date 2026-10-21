param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [Parameter(Mandatory=$true)][string]$WwisePluginDirectory,
    [Parameter(Mandatory=$true)][string]$WwiseSoundEnginePluginDirectory,
    [string]$HostDirectory,
    [switch]$RunAutomationTests
)
$ErrorActionPreference = 'Stop'
$pluginRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildTool = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$editorTool = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
foreach ($requiredFile in @($buildTool, (Join-Path $WwisePluginDirectory 'Wwise.uplugin'), (Join-Path $WwiseSoundEnginePluginDirectory 'WwiseSoundEngine.uplugin'))) {
    if (-not (Test-Path -LiteralPath $requiredFile)) { throw "Missing dependency: $requiredFile" }
}
if (-not $HostDirectory) {
    $HostDirectory = Join-Path ([IO.Path]::GetTempPath()) ('WwiseMediaHost-' + [Guid]::NewGuid().ToString('N'))
}
$HostDirectory = [IO.Path]::GetFullPath($HostDirectory)
$hostPlugins = Join-Path $HostDirectory 'Plugins'
New-Item -ItemType Directory -Path $hostPlugins -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $HostDirectory 'Content') -Force | Out-Null
$dependencies = @{
    WwiseMedia = $pluginRoot
    Wwise = [IO.Path]::GetFullPath($WwisePluginDirectory)
    WwiseSoundEngine = [IO.Path]::GetFullPath($WwiseSoundEnginePluginDirectory)
}
foreach ($entry in $dependencies.GetEnumerator()) {
    $linkPath = Join-Path $hostPlugins $entry.Key
    if (Test-Path -LiteralPath $linkPath) { throw "Use a fresh HostDirectory; already exists: $linkPath" }
    New-Item -ItemType Junction -Path $linkPath -Target $entry.Value | Out-Null
}
$projectPath = Join-Path $HostDirectory 'WwiseMediaHost.uproject'
$descriptor = @{
    FileVersion = 3
    DisableEnginePluginsByDefault = $true
    Plugins = @(
        @{ Name='WwiseMedia'; Enabled=$true },
        @{ Name='Wwise'; Enabled=$true },
        @{ Name='WwiseSoundEngine'; Enabled=$true },
        # The installed Wwise AkAudioSampler module imports Niagara DLLs.
        @{ Name='Niagara'; Enabled=$true }
    )
}
[IO.File]::WriteAllText($projectPath, ($descriptor | ConvertTo-Json -Depth 8))
$foreignPlugin = Join-Path $hostPlugins 'WwiseMedia/WwiseMedia.uplugin'
& $buildTool UnrealEditor Win64 Development "-Project=$projectPath" "-Plugin=$foreignPlugin" -BuildPluginAsLocal -NoHotReloadFromIDE -NoEngineChanges -NoUBA -NoXGE -MaxParallelActions=4 -NoUBTMakefiles
if ($LASTEXITCODE -ne 0) { throw "WwiseMedia build failed with exit code $LASTEXITCODE" }
if ($RunAutomationTests) {
    $reportPath = Join-Path $HostDirectory 'AutomationReport'
    & $editorTool $projectPath -unattended -nop4 -nullrhi -nosound -nosplash -NoLiveCoding '-ExecCmds=Automation RunTests WwiseMedia.Bridge' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportPath"
    if ($LASTEXITCODE -ne 0) { throw "WwiseMedia automation failed with exit code $LASTEXITCODE" }
    $report = Get-Content -LiteralPath (Join-Path $reportPath 'index.json') -Raw | ConvertFrom-Json
    foreach ($testPath in @('WwiseMedia.Bridge.ResampleAndPause', 'WwiseMedia.Bridge.FlushRejectsOldAudio')) {
        $passed = @($report.tests | Where-Object { $_.fullTestPath -eq $testPath -and $_.state -eq 'Success' })
        if ($passed.Count -ne 1) { throw "Required automation test did not pass: $testPath" }
    }
}
Write-Output "WwiseMedia build host retained at $HostDirectory"
