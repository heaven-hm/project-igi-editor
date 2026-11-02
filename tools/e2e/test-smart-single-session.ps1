$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'Invoke-SmartSingleSession.ps1'
$inventoryPath = Join-Path $PSScriptRoot '../../artifacts/task6-metadata-manifest.json'
$tokens = $null; $errors = $null
[Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw "Single-session runner has PowerShell parse errors: $($errors[0].Message)" }

$inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
$fixture = $null
foreach ($level in @($inventory.levels)) {
    $candidate = @($level.inventory | Where-Object {
        [string]$_.taskId -match '^\d+$' -and $_.modelId -and
        @($_.authoredPosition).Count -eq 3 -and @($_.authoredRotation).Count -eq 3
    } | Select-Object -First 1)
    if ($candidate.Count -eq 1) {
        $fixture = [pscustomobject]@{level=[int]$level.level;type=[string]$candidate[0].type}
        break
    }
}
if ($null -eq $fixture) { throw 'Inventory has no named renderable fixture for the single-session contract test.' }

$root = Join-Path ([IO.Path]::GetTempPath()) ('igi-single-session-' + [Guid]::NewGuid().ToString('N'))
try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $scriptPath -ArtifactsRoot $root -InventoryPath $inventoryPath -GameRoot 'D:\IGI1' -Level $fixture.level -IncludeTypes $fixture.type -MaxObjects 1 -ViewCount 3 -PrepareOnly
    if ($LASTEXITCODE -ne 0) { throw "Prepare-only runner returned $LASTEXITCODE." }
    $state = Get-Content (Join-Path $root 'batch.json') -Raw | ConvertFrom-Json
    $manifest = Get-Content (Join-Path $root 'single-session-manifest.json') -Raw | ConvertFrom-Json
    $scenario = @($manifest.scenarios)[0]
    $launches = @($scenario.steps | Where-Object type -eq 'launch_editor')
    $closes = @($scenario.steps | Where-Object type -eq 'close_editor')
    $orbits = @($scenario.steps | Where-Object type -eq 'orbit_camera')
    if (-not $manifest.singleSession -or [int]$manifest.launchCount -ne 1 -or [int]$manifest.closeCount -ne 1) { throw 'Manifest single-session contract is incorrect.' }
    if ($launches.Count -ne 1 -or $closes.Count -ne 1) { throw "Expected one launch and one close; got $($launches.Count)/$($closes.Count)." }
    if ($state.selectableObjects -ne 1 -or $orbits.Count -ne 3) { throw "Expected one selected object and three orbit views; got $($state.selectableObjects)/$($orbits.Count)." }
    if (@($orbits | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.screenshotAfter) }).Count -ne 0) { throw 'Every orbit must produce a screenshot.' }
    'PASS: single-session manifest has one launch, one close, and per-object orbit screenshots.'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}
