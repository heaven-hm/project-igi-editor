[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [ValidateRange(1,14)][int[]]$Levels = @(1),
    [switch]$AllLevels,
    [switch]$AllObjects,
    [string[]]$Categories = @('All'),
    [string[]]$ObjectTypes = @(),
    [ValidateRange(0,2147483647)][int]$MaxObjects = 0,
    [switch]$PrepareOnly,
    [switch]$Resume,
    [switch]$AllowConfigMutation,
    [ValidateRange(0,1)][int]$RetryCount = 1,
    [switch]$DistinctTypes,
    [ValidateRange(1,10)][int]$ViewCount = 10
)
$ErrorActionPreference = 'Stop'
if (-not $PrepareOnly -and -not $AllowConfigMutation) { throw 'Live capture requires -AllowConfigMutation.' }
$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
$matrixPath = Join-Path $ArtifactsRoot 'matrix.json'
if ($Resume) {
    if (-not (Test-Path -LiteralPath $matrixPath)) { throw 'Resume requires an existing matrix.json.' }
} elseif (Test-Path -LiteralPath $ArtifactsRoot) { throw 'Use a fresh matrix artifact directory.' }
else { New-Item -ItemType Directory -Path $ArtifactsRoot | Out-Null }

$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$availableLevels = @($inventory.levels.level | Sort-Object -Unique)
if ($AllLevels) { $Levels = $availableLevels }
$Levels = @($Levels | Sort-Object -Unique)
foreach ($level in $Levels) { if ($availableLevels -notcontains $level) { throw "Level $level is absent from the inventory." } }
$categoryTypes = @{
    All = @()
    Buildings = @('Building','Door','Terminal','Switch','AlarmControl')
    RigidObjects = @('EditRigidObj','Static','Dynamic','SplineObj','SplineObjWaypoint','ExplodeObject')
    Vehicles = @('Car','Heli','Train')
    AI = @('HumanAI','HumanSoldier','AISquad','AIGraph','PatrolPath','PatrolPathCommand')
}
$filterTypes = [System.Collections.Generic.List[string]]::new()
foreach ($category in $Categories) {
    if (-not $categoryTypes.ContainsKey($category)) { throw "Unknown category '$category'. Use All, Buildings, RigidObjects, Vehicles, or AI." }
    foreach ($type in $categoryTypes[$category]) { if (-not $filterTypes.Contains($type)) { $filterTypes.Add($type) } }
}
foreach ($type in $ObjectTypes) { if (-not $filterTypes.Contains($type)) { $filterTypes.Add($type) } }
if ($Categories -contains 'All') { $filterTypes.Clear() }
$levelTool = Join-Path $PSScriptRoot 'Invoke-SmartLevelModelBatch.ps1'
$matrixResults = @()
$previousPrepareOnly = $null
$previousMaxObjects = $null
if ($Resume) {
    $prior = Get-Content -LiteralPath $matrixPath -Raw | ConvertFrom-Json
    if ($null -ne $prior.PSObject.Properties['prepareOnly']) { $previousPrepareOnly = [bool]$prior.prepareOnly }
    if ($null -ne $prior.PSObject.Properties['maxObjects']) { $previousMaxObjects = [int]$prior.maxObjects }
    foreach ($old in @($prior.results | Where-Object { $null -ne $_.PSObject.Properties['level'] })) {
        $oldBatchPath = Join-Path (Join-Path $ArtifactsRoot ('level'+[int]$old.level)) 'batch.json'
        if (Test-Path -LiteralPath $oldBatchPath) {
            $oldBatch = Get-Content -LiteralPath $oldBatchPath -Raw | ConvertFrom-Json
            $oldStatus = if (@($oldBatch.results | Where-Object status -eq 'FAIL').Count -eq 0) { 'PASS' } else { 'FAIL' }
            $matrixResults += [pscustomobject]@{level=[int]$old.level;status=$oldStatus;candidateCount=[int]$oldBatch.renderableInstances;totalTasks=[int]$oldBatch.totalTasks;preparedTasks=@($oldBatch.results | Where-Object status -eq 'PREPARED').Count;skippedTasks=@($oldBatch.skippedTasks).Count;failedTasks=@($oldBatch.results | Where-Object status -eq 'FAIL').Count;artifactRoot=(Join-Path $ArtifactsRoot ('level'+[int]$old.level));failure=$(if($oldStatus -eq 'FAIL'){'Inspect level batch.json'}else{$null})}
        } else { $matrixResults += $old }
    }
}

function Save-Matrix {
    [pscustomobject]@{
        levels=@($Levels)
        allObjects=[bool]$AllObjects
        categories=@($Categories)
        objectTypes=@($filterTypes.ToArray())
        maxObjects=$MaxObjects
        prepareOnly=[bool]$PrepareOnly
        results=@($matrixResults)
        scope='All selected task records are enumerated. Only records with a model, authored position, and authored rotation receive 360-degree model captures; non-model selected records are reported as not applicable to this 3D check.'
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $matrixPath -Encoding UTF8
}

Save-Matrix
$remaining = $MaxObjects
foreach ($level in $Levels) {
    $priorResult = @($matrixResults | Where-Object level -eq $level)
    if ($priorResult.Count -eq 1 -and $priorResult[0].status -eq 'PASS' -and $null -ne $previousPrepareOnly -and $previousPrepareOnly -eq [bool]$PrepareOnly -and $null -ne $previousMaxObjects -and $previousMaxObjects -eq $MaxObjects) { Write-Output "Level $level RESUMED-PASS"; continue }
    $levelRoot = Join-Path $ArtifactsRoot ('level'+$level)
    $levelRows = @($inventory.levels | Where-Object level -eq $level)[0].inventory
    if ($filterTypes.Count) { $levelRows = @($levelRows | Where-Object { $filterTypes -contains [string]$_.type }) }
    $candidateCount = @($levelRows | Where-Object { $_.modelId -and $_.authoredPosition -and $_.authoredRotation -and $_.renderable -ne $false -and $_.helperModel -ne $true -and $_.modelResolved -ne $false }).Count
    if ($MaxObjects -gt 0 -and $remaining -le 0) {
        $matrixResults += [pscustomobject]@{level=$level;status='NOT_RUN_LIMIT_REACHED';candidateCount=$candidateCount;failure=$null}
        Save-Matrix
        continue
    }
    $levelLimit = if ($MaxObjects -eq 0) { 0 } else { [Math]::Min($remaining,$candidateCount) }
    $args = @('-ArtifactsRoot',$levelRoot,'-InventoryPath',$InventoryPath,'-GameRoot',$GameRoot,'-Level',$level,'-MaxObjects',$levelLimit,'-RetryCount',$RetryCount,'-ViewCount',$ViewCount)
    if ($DistinctTypes) { $args += '-DistinctTypes' }
    if ($filterTypes.Count) { $args += '-IncludeTypes'; $args += ($filterTypes -join ',') }
    if ($PrepareOnly) { $args += '-PrepareOnly' } else { $args += '-AllowConfigMutation' }
    if ($Resume -and (Test-Path -LiteralPath (Join-Path $levelRoot 'batch.json'))) { $args += '-Resume' }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $levelTool @args
    $status = if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }
    $matrixResults = @($matrixResults | Where-Object level -ne $level)
    $batch = Get-Content -LiteralPath (Join-Path $levelRoot 'batch.json') -Raw | ConvertFrom-Json
    $matrixResults += [pscustomobject]@{level=$level;status=$status;candidateCount=[int]$batch.renderableInstances;totalTasks=[int]$batch.totalTasks;preparedTasks=@($batch.results | Where-Object status -eq 'PREPARED').Count;skippedTasks=@($batch.skippedTasks).Count;failedTasks=@($batch.results | Where-Object status -eq 'FAIL').Count;artifactRoot=$levelRoot;failure=$(if($status -eq 'FAIL'){'Inspect level batch.json'}else{$null})}
    if ($MaxObjects -gt 0) { $remaining -= $levelLimit }
    Save-Matrix
    if ($status -eq 'FAIL') { exit 1 }
}
Save-Matrix
