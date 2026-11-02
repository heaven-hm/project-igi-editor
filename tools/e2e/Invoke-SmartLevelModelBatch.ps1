[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [ValidateRange(1,14)][int]$Level = 1,
    [string[]]$IncludeTypes = @(),
    [ValidateRange(0,2147483647)][int]$MaxObjects = 0,
    [switch]$PrepareOnly,
    [switch]$Resume,
    [switch]$AllowConfigMutation,
    [ValidateRange(0,1)][int]$RetryCount = 1,
    [switch]$DistinctTypes,
    [ValidateRange(1,10)][int]$ViewCount = 10,
    [ValidateRange(0,60)][int]$CooldownSeconds = 5
)
$ErrorActionPreference = 'Stop'
if (-not $PrepareOnly -and -not $AllowConfigMutation) { throw 'Live capture requires -AllowConfigMutation.' }
if (Get-Process igi1ed -ErrorAction SilentlyContinue) { throw 'Close the existing editor before the serial batch.' }
function Get-PortableSha256([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','') }
    finally { $sha.Dispose() }
}
$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
$statePath = Join-Path $ArtifactsRoot 'batch.json'
if ($Resume) {
    if (-not (Test-Path -LiteralPath $statePath)) { throw 'Resume requires an existing batch.json.' }
} elseif (Test-Path -LiteralPath $ArtifactsRoot) { throw 'Use a fresh batch artifact directory.' }
else { New-Item -ItemType Directory -Path $ArtifactsRoot | Out-Null }

$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$levelRows = @($inventory.levels | Where-Object level -eq $Level)
if ($levelRows.Count -ne 1) { throw 'Missing or ambiguous level inventory.' }
$all = @($levelRows[0].inventory)
$IncludeTypes = @($IncludeTypes | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($IncludeTypes.Count) { $all = @($all | Where-Object { $IncludeTypes -contains [string]$_.type }) }
$modelAnchors = @($all | Where-Object {
    $_.modelId -and $_.authoredPosition -and $_.authoredRotation -and
    $_.renderable -ne $false -and $_.helperModel -ne $true -and $_.modelResolved -ne $false
})
if ($DistinctTypes) {
    $selected = [System.Collections.Generic.List[object]]::new()
    $groups = @($modelAnchors | Group-Object type)
    foreach ($g in $groups) {
        if ($MaxObjects -gt 0 -and $selected.Count -ge $MaxObjects) { break }
        $selected.Add($g.Group[0])
    }
    $renderable = @($selected)
} else {
    $renderable = @($modelAnchors | Sort-Object {[string]$_.taskId})
    if ($MaxObjects -gt 0) { $renderable = @($renderable | Select-Object -First $MaxObjects) }
}
$skipped = @($all | Where-Object {
    -not ($_.modelId -and $_.authoredPosition -and $_.authoredRotation -and
        $_.renderable -ne $false -and $_.helperModel -ne $true -and $_.modelResolved -ne $false)
} | ForEach-Object {
    $reason = if ($_.modelId -and $_.modelResolved -eq $false) { 'model unresolved in deployed corpus' }
        elseif ($_.helperModel -eq $true -or $_.renderable -eq $false) { 'authored helper/non-renderable model' }
        elseif (-not $_.modelId) { 'non-renderable task' }
        elseif (-not $_.authoredPosition) { 'missing authored position' }
        elseif (-not $_.authoredRotation) { 'missing authored rotation' }
    [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;modelId=$_.modelId;status='SKIPPED';reason=$reason}
})
$converter = Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
$planTool = Join-Path $PSScriptRoot 'New-SmartCameraPlan.ps1'
$pilotTool = Join-Path $PSScriptRoot 'Invoke-SmartCameraPilot.ps1'
$assetCache = Join-Path $ArtifactsRoot 'mesh-cache'
New-Item -ItemType Directory -Path $assetCache -Force | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
if ($Resume) {
    $previous = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ([int]$previous.level -ne $Level) { throw 'Resume batch level does not match requested level.' }
    foreach ($result in @($previous.results | Where-Object status -ne 'FAIL')) { $results.Add($result) }
}

function Save-BatchState {
    param([string]$Path)
    [pscustomobject]@{
        level=$Level
        totalTasks=$all.Count
        modelInstances=$modelAnchors.Count
        renderableInstances=$renderable.Count
        selectedTypes=@($IncludeTypes)
        skippedTasks=@($skipped)
        results=@($results.ToArray())
        acceptance='ten saved-camera views per renderable instance; authored transform, required texture loads, and assignment counts; visual acceptance remains separate'
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
}

Save-BatchState $statePath
$archives = @(
    (Join-Path $GameRoot "MISSIONS/location0/level$Level/models/level$Level.res"),
    (Join-Path $GameRoot 'MISSIONS/location0/COMMON/MODELS/location0.res')
)
$ordered = @($renderable)
$index = 0
foreach ($anchor in $ordered) {
    $index++
    $task = [string]$anchor.taskId
    $model = [string]$anchor.modelId
    $id = ('{0:D4}-task{1}-{2}-{3}' -f $index,$task,([string]$anchor.type -replace '[^A-Za-z0-9_-]','_'),($model -replace '[^A-Za-z0-9_-]','_'))
    $objectRoot = Join-Path $ArtifactsRoot ('objects\'+$id)
    $status = 'FAIL'; $failure = $null; $attempts = 0
    $completed = @($results | Where-Object {
        $_.taskId -eq $task -and $_.type -eq [string]$anchor.type -and $_.modelId -eq $model -and
        ($_.status -eq 'PASS' -or ($PrepareOnly -and $_.status -eq 'PREPARED'))
    })
    if ($completed.Count -eq 1) {
        Write-Output ("[{0}/{1}] task {2} {3} RESUMED-PASS" -f $index,$ordered.Count,$task,$model)
        continue
    }
    try {
        New-Item -ItemType Directory -Path $objectRoot -Force | Out-Null
        $sourcePath = Join-Path $GameRoot $levelRows[0].sourcePath
        if ((Get-PortableSha256 $sourcePath) -ne [string]$anchor.sourceHash) { throw 'Stale object inventory.' }
        if ($model -notmatch '^[A-Za-z0-9_-]+$') { throw 'Unsupported model identifier.' }
        $mesh = Join-Path $assetCache ($model+'.mef')
        $obj = Join-Path $assetCache ($model+'.obj')
        if (-not (Test-Path -LiteralPath $obj)) {
            foreach ($archive in $archives) {
                if (-not (Test-Path -LiteralPath $archive)) { continue }
                & $converter res extract $archive --file "LOCAL:models/$model.mef" -o $assetCache | Out-Null
                if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $mesh)) { break }
            }
            if (-not (Test-Path -LiteralPath $mesh)) { throw "Model extraction failed for $model in level or location-common archives." }
            & $converter mef export $mesh -o $obj | Out-Null
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $obj)) { throw "Geometry export failed for $model." }
        }
        $plan = Join-Path $objectRoot 'camera-plan.json'
        if (-not (Test-Path -LiteralPath $plan)) {
            $global:LASTEXITCODE = 0
            & $planTool -ObjPath $obj -Position ([double[]]$anchor.authoredPosition) -Rotation ([double[]]$anchor.authoredRotation) -OutputPath $plan | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Camera plan generation failed.' }
        }
        if (-not (Test-Path -LiteralPath $plan)) { throw 'Camera plan is absent.' }
        if ($PrepareOnly) { $status='PREPARED' }
        else {
            $existingAttempts = @(Get-ChildItem -LiteralPath $objectRoot -Directory -Filter 'attempt-*' -ErrorAction SilentlyContinue | ForEach-Object {
                if ($_.Name -match '^attempt-(\d+)$') { [int]$Matches[1] }
            })
            $attemptBase = if ($existingAttempts.Count) { ($existingAttempts | Measure-Object -Maximum).Maximum } else { 0 }
            for ($attempt=1; $attempt -le ($RetryCount+1); $attempt++) {
                $attemptNumber = $attemptBase + $attempt
                $attempts=$attemptNumber
                $attemptRoot = Join-Path $objectRoot ('attempt-'+$attemptNumber)
                $pilotArgs = @('-CameraPlan',$plan,'-ArtifactsRoot',$attemptRoot,'-InventoryPath',$InventoryPath,'-GameRoot',$GameRoot,'-Level',$Level,'-TaskId',$task,'-ViewCount',$ViewCount,'-CooldownSeconds',$CooldownSeconds,'-AllowConfigMutation')
                if ($task -notmatch '^\d+$') { $pilotArgs += '-SkipObjectSelection' }
                & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $pilotTool @pilotArgs *> (Join-Path $objectRoot ('attempt-'+$attemptNumber+'.log'))
                if ($LASTEXITCODE -eq 0) { $status='PASS'; break }
                if (Get-Process igi1ed -ErrorAction SilentlyContinue) { throw 'Editor remained running after failed object; safety stop.' }
                $failure = "Pilot failed on attempt $attemptNumber; inspect attempt log."
                if ($attempt -gt $RetryCount) { throw $failure }
            }
        }
    } catch { $failure=$_.Exception.Message }
    $results.Add([pscustomobject]@{index=$index;taskId=$task;type=[string]$anchor.type;modelId=$model;status=$status;attempts=$attempts;failure=$failure})
    Save-BatchState $statePath
    Write-Output ("[{0}/{1}] task {2} {3} {4}" -f $index,$ordered.Count,$task,$model,$status)
}
Save-BatchState $statePath
$failures=@($results|Where-Object status -eq 'FAIL').Count
Write-Output ("Level {0}: {1} renderable instances, {2} skipped tasks, {3} failures." -f $Level,$renderable.Count,$skipped.Count,$failures)
if ($failures) { exit 1 }
