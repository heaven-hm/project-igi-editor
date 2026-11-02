[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [ValidateRange(1,14)][int]$Level = 1,
    [string[]]$IncludeTypes = @(),
    [ValidateRange(0,2147483647)][int]$MaxObjects = 0,
    [switch]$DistinctTypes,
    [ValidateRange(1,10)][int]$ViewCount = 10,
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
if (-not $PrepareOnly -and (Get-Process igi1ed -ErrorAction SilentlyContinue)) {
    throw 'Close the existing editor before a single-session capture.'
}

function Get-PortableSha256([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Get-SafeName([string]$Value) {
    return ($Value -replace '[^A-Za-z0-9_-]', '_')
}

function Get-RequiredTextureMap([string]$DatPath, [string]$OutputPath) {
    $converter = Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
    if (-not (Test-Path -LiteralPath $converter)) { throw "Converter not found: $converter" }
    & $converter dat export $DatPath -o $OutputPath | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutputPath)) { throw "DAT material export failed: $DatPath" }
    $json = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
    $map = @{}
    foreach ($model in @($json.models)) { $map[[string]$model.modelName] = @($model.textures) }
    return $map
}

$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
if (Test-Path -LiteralPath $ArtifactsRoot) { throw 'Use a fresh artifact directory.' }
New-Item -ItemType Directory -Path $ArtifactsRoot -Force | Out-Null

$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$levelRow = @($inventory.levels | Where-Object level -eq $Level)
if ($levelRow.Count -ne 1) { throw "Missing or ambiguous inventory entry for level $Level." }
$levelRow = $levelRow[0]
$sourcePath = Join-Path $GameRoot ([string]$levelRow.sourcePath)
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Level source is missing: $sourcePath" }
$sourceHash = Get-PortableSha256 $sourcePath

$IncludeTypes = @($IncludeTypes | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$all = @($levelRow.inventory)
if ($IncludeTypes.Count) { $all = @($all | Where-Object { $IncludeTypes -contains [string]$_.type }) }
$candidates = @($all | Where-Object {
    $_.modelId -and @($_.authoredPosition).Count -eq 3 -and @($_.authoredRotation).Count -eq 3
})
if ($DistinctTypes) {
    $candidates = @($candidates | Group-Object type | ForEach-Object { $_.Group | Sort-Object {[string]$_.taskId} | Select-Object -First 1 })
}
$synthetic = @($candidates | Where-Object { [string]$_.taskId -notmatch '^\d+$' })
$anchors = @($candidates | Where-Object { [string]$_.taskId -match '^\d+$' } | Sort-Object {[string]$_.taskId})
$notSelected = @()
if ($MaxObjects -gt 0 -and $anchors.Count -gt $MaxObjects) {
    $notSelected = @($anchors | Select-Object -Skip $MaxObjects | ForEach-Object {
        [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;modelId=[string]$_.modelId;status='NOT_SELECTED';reason="maximum object limit $MaxObjects reached"}
    })
    $anchors = @($anchors | Select-Object -First $MaxObjects)
}

$skipped = @($all | Where-Object {
    -not ($_.modelId -and @($_.authoredPosition).Count -eq 3 -and @($_.authoredRotation).Count -eq 3)
} | ForEach-Object {
    $reason = if (-not $_.modelId) { 'non-renderable task' }
        elseif (@($_.authoredPosition).Count -ne 3) { 'missing authored position' }
        else { 'missing authored rotation' }
    [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;modelId=$_.modelId;status='SKIPPED';reason=$reason}
})

$skipped += @($synthetic | ForEach-Object {
    [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;modelId=[string]$_.modelId;status='SKIPPED';reason='anonymous task has no editor find-by-ID target'}
})
$skipped += $notSelected
$selectable = @($anchors)

$datPath = Join-Path $GameRoot "MISSIONS/location0/level$Level/level$Level.dat"
$materialPath = Join-Path $ArtifactsRoot 'authored-materials.json'
$textureMap = Get-RequiredTextureMap $datPath $materialPath
$viewNames = @('front','front-right','right','back-right','back','back-left','left','front-left','top','bottom') | Select-Object -First $ViewCount

$scenarioName = "single-session-level$Level"
$scenarioDir = Join-Path $ArtifactsRoot $scenarioName
$steps = [System.Collections.Generic.List[object]]::new()
$steps.Add([ordered]@{id='launch';type='launch_editor';level=$Level;drawParts=-1})
$steps.Add([ordered]@{id='window';type='wait_for_window';timeoutSeconds=45})
$steps.Add([ordered]@{id='loaded';type='wait_for_log';pattern="\[App\] LoadLevel\(\) COMPLETE for level $Level";timeoutSeconds=120})
$steps.Add([ordered]@{id='health';type='assert_process'})

$objectPlan = [System.Collections.Generic.List[object]]::new()
$ordinal = 0
foreach ($anchor in $selectable) {
    $ordinal++
    $task = [string]$anchor.taskId
    $model = [string]$anchor.modelId
    $prefix = ('obj-{0:D4}-task{1}-{2}' -f $ordinal,(Get-SafeName $task),(Get-SafeName $model))
    $required = @()
    if ($textureMap.ContainsKey($model)) { $required = @($textureMap[$model]) }
    $objectPlan.Add([pscustomobject]@{index=$ordinal;taskId=$task;type=[string]$anchor.type;modelId=$model;requiredTextures=$required;prefix=$prefix;sourceHash=$sourceHash})
    $steps.Add([ordered]@{id="$prefix-find";type='key';key='CTRL+SHIFT+I'})
    $steps.Add([ordered]@{id="$prefix-type";type='type_text';text=$task})
    $steps.Add([ordered]@{id="$prefix-select";type='key';key='ENTER'})
    $steps.Add([ordered]@{id="$prefix-settle";type='wait';seconds=1})
    foreach ($view in $viewNames) {
        $steps.Add([ordered]@{id="$prefix-$view";type='orbit_camera';angle=$view;pixels=12;distance=300;screenshotAfter="$prefix-$view"})
    }
}
$steps.Add([ordered]@{id='window-evidence';type='capture_window_state'})
$steps.Add([ordered]@{id='close';type='close_editor';force=$false})

$manifest = [ordered]@{
    singleSession = $true
    launchCount = 1
    closeCount = 1
    scenarios = @([ordered]@{name=$scenarioName;level=$Level;requiresMutation=$false;steps=@($steps.ToArray())})
}
$manifestPath = Join-Path $ArtifactsRoot 'single-session-manifest.json'
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$state = [ordered]@{
    level=$Level; selectedTypes=@($IncludeTypes); maxObjects=$MaxObjects
    totalTasks=$all.Count; renderableCandidates=$anchors.Count; selectableObjects=$selectable.Count
    skippedTasks=@($skipped); objects=@($objectPlan); launchCount=1; closeCount=1
    viewCount=$viewNames.Count; screenshotsExpected=($selectable.Count * $viewNames.Count)
    status=$(if ($PrepareOnly) { 'PREPARED' } else { 'NOT_RUN' })
}
$statePath = Join-Path $ArtifactsRoot 'batch.json'
$state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $statePath -Encoding UTF8

if ($PrepareOnly) {
    Write-Output "Prepared one-session level ${Level}: $($selectable.Count) selectable objects, $($skipped.Count) skipped, $($viewNames.Count) views each."
    exit 0
}

$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -GameRoot $GameRoot -ScenarioPath $manifestPath -ArtifactsRoot $ArtifactsRoot
$runnerExit = $LASTEXITCODE
$runPath = Join-Path $ArtifactsRoot 'run.json'
$scenarioReport = if (Test-Path -LiteralPath (Join-Path $scenarioDir 'scenario.json')) { Get-Content (Join-Path $scenarioDir 'scenario.json') -Raw | ConvertFrom-Json } else { $null }
$logPath = Join-Path $GameRoot 'igi1ed.log'
$logOffset = if ($scenarioReport -and $null -ne $scenarioReport.logOffset) { [int64]$scenarioReport.logOffset } else { 0 }
$freshLog = if (Test-Path -LiteralPath $logPath) {
    $bytes = [IO.File]::ReadAllBytes($logPath)
    if ($logOffset -lt $bytes.Length) { [Text.Encoding]::UTF8.GetString($bytes,$logOffset,$bytes.Length-$logOffset) } else { '' }
} else { '' }

. (Join-Path $PSScriptRoot 'SmartModelEvidence.ps1')
$evidence = @()
foreach ($plan in $objectPlan) {
    $anchor = @($selectable | Where-Object { [string]$_.taskId -eq $plan.taskId -and [string]$_.modelId -eq $plan.modelId })[0]
    $anchor | Add-Member -NotePropertyName requiredTextures -NotePropertyValue @($plan.requiredTextures) -Force
    $item = Test-SmartModelLog $freshLog $anchor
    $shots = @(Get-ChildItem -LiteralPath $scenarioDir -File -Filter ($plan.prefix + '-*.png') -ErrorAction SilentlyContinue)
    $item | Add-Member -NotePropertyName taskId -NotePropertyValue $plan.taskId -Force
    $item | Add-Member -NotePropertyName type -NotePropertyValue $plan.type -Force
    $item | Add-Member -NotePropertyName modelId -NotePropertyValue $plan.modelId -Force
    $item | Add-Member -NotePropertyName screenshotCount -NotePropertyValue $shots.Count -Force
    if ($shots.Count -ne $viewNames.Count) {
        $item.passed = $false
        $item.failures = @($item.failures + "Expected $($viewNames.Count) screenshots, found $($shots.Count).")
    }
    if (@($plan.requiredTextures).Count -eq 0) {
        $item.passed = $false
        $item.failures = @($item.failures + 'Required model texture list unavailable; do not infer a texture pass.')
    }
    $evidence += $item
}
$passed = @($evidence | Where-Object passed).Count
$failures = @($evidence | Where-Object { -not $_.passed }).Count
$final = [ordered]@{
    level=$Level; status=$(if ($runnerExit -eq 0 -and $failures -eq 0) { 'PASS' } else { 'FAIL' })
    launchCount=1; closeCount=1; selectableObjects=$selectable.Count; skippedTasks=@($skipped)
    screenshotsExpected=($selectable.Count * $viewNames.Count); screenshotsCaptured=(@($evidence | Measure-Object screenshotCount -Sum).Sum)
    evidencePassed=$passed; evidenceFailed=$failures; objects=@($evidence); runnerExit=$runnerExit
}
$final | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $statePath -Encoding UTF8
if ($final.status -ne 'PASS') { exit 1 }
Write-Output "PASS: one editor session, $($selectable.Count) objects, $($final.screenshotsCaptured) screenshots, $passed evidence records."
