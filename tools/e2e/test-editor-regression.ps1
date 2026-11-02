[CmdletBinding()]
param(
    [string]$GameRoot = 'D:\IGI1',
    [string]$ArtifactsRoot = '',
    [switch]$ValidateOnly,
    [switch]$AllowGameDataMutation
)
$ErrorActionPreference = 'Stop'

$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
$focused = Join-Path $PSScriptRoot 'scenarios\editor-regression.json'
$visual = Join-Path $PSScriptRoot 'scenarios\editor-visual-regressions.json'
if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    $ArtifactsRoot = Join-Path (Join-Path $PSScriptRoot '..\..\artifacts') ('e2e\regression-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}

foreach ($manifest in @($focused, $visual)) {
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ValidateOnly -ScenarioPath $manifest
    if ($LASTEXITCODE -ne 0) { throw "Manifest validation failed: $manifest" }
}
if ($ValidateOnly) {
    Write-Host 'Editor regression pipeline validation: PASS'
    exit 0
}

New-Item -ItemType Directory -Path $ArtifactsRoot -Force | Out-Null
$runs = @(
    @{ Name = 'focused'; Manifest = $focused; AllowMutation = $true },
    @{ Name = 'visual'; Manifest = $visual; AllowMutation = $false }
)
$failed = 0
foreach ($run in $runs) {
    $output = Join-Path $ArtifactsRoot $run.Name
    $arguments = @('-GameRoot', $GameRoot, '-ScenarioPath', $run.Manifest, '-ArtifactsRoot', $output)
    if ($run.AllowMutation -and $AllowGameDataMutation) { $arguments += '-AllowGameDataMutation' }
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $runner @arguments
    if ($LASTEXITCODE -ne 0) { $failed++ }
}
if ($failed -gt 0) {
    Write-Error "$failed editor regression run(s) failed. See $ArtifactsRoot."
    exit 1
}
Write-Host 'Editor regression pipeline: PASS'
exit 0
