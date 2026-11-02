[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$TargetsPath = (Join-Path $PSScriptRoot 'scenarios/smart-three-object-trial.json'),
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [switch]$PrepareOnly,
    [switch]$AllowConfigMutation
)
$ErrorActionPreference='Stop'
$targets=@((Get-Content $TargetsPath -Raw | ConvertFrom-Json).targets)
if ($targets.Count -ne 3 -or @($targets.level | Select-Object -Unique).Count -ne 3 -or @($targets.type | Select-Object -Unique).Count -ne 3) { throw 'This trial requires exactly three objects, three levels and three types.' }
if (-not $PrepareOnly -and -not $AllowConfigMutation) { throw 'Live capture requires -AllowConfigMutation.' }
$inventory=Get-Content $InventoryPath -Raw | ConvertFrom-Json
$ArtifactsRoot=[IO.Path]::GetFullPath($ArtifactsRoot)
if(Test-Path $ArtifactsRoot){throw 'Use a fresh trial directory.'}
New-Item -ItemType Directory $ArtifactsRoot | Out-Null
$converter=Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
$results=@()
foreach($target in $targets){
    $id="level$($target.level)-task$($target.taskId)"
    $status='FAIL'; $failure=$null
    try {
        $level=@($inventory.levels|Where-Object level -eq $target.level)
        if($level.Count -ne 1){throw 'Missing level inventory.'}
        $anchor=@($level[0].inventory|Where-Object {$_.taskId -eq $target.taskId -and $_.type -eq $target.type})
        if($anchor.Count -ne 1){throw 'Missing or ambiguous target.'}
        $anchor=$anchor[0]
        if ((Get-FileHash (Join-Path $GameRoot $level[0].sourcePath)).Hash -ne $anchor.sourceHash){throw 'Stale inventory.'}
        if($anchor.modelId -notmatch '^[A-Za-z0-9_-]+$'){throw 'Unsupported model identifier.'}
        $assets=Join-Path $ArtifactsRoot ($id+'-assets')
        New-Item -ItemType Directory $assets | Out-Null
        $archive=Join-Path $GameRoot "MISSIONS/location0/level$($target.level)/models/level$($target.level).res"
        & $converter res extract $archive --file "LOCAL:models/$($anchor.modelId).mef" -o $assets
        $mef=Join-Path $assets ($anchor.modelId+'.mef')
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $mef)){throw 'Model extraction failed; no fallback geometry allowed.'}
        $obj=Join-Path $assets ($anchor.modelId+'.obj')
        & $converter mef export $mef -o $obj
        if($LASTEXITCODE -ne 0 -or -not(Test-Path $obj)){throw 'Geometry export failed.'}
        $camera=Join-Path $assets 'camera-plan.json'
        & (Join-Path $PSScriptRoot 'New-SmartCameraPlan.ps1') -ObjPath $obj -Position $anchor.authoredPosition -Rotation $anchor.authoredRotation -OutputPath $camera | Out-Null
        if($PrepareOnly){$status='PREPARED'}else{
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Invoke-SmartCameraPilot.ps1') -CameraPlan $camera -ArtifactsRoot (Join-Path $ArtifactsRoot $id) -InventoryPath $InventoryPath -GameRoot $GameRoot -Level $target.level -TaskId $target.taskId -ViewCount 10 -AllowConfigMutation
            if($LASTEXITCODE -ne 0){throw 'Capture/state checks failed; inspect the retained object evidence.'}
            $status='CAPTURE_AND_STATE_PASS'
        }
    }catch{$failure=$_.Exception.Message}
    $results += [pscustomobject]@{level=$target.level;taskId=$target.taskId;type=$target.type;status=$status;failure=$failure;visualAcceptance='UNVERIFIED'}
    $results|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ArtifactsRoot 'trial.json') -Encoding UTF8
    if(Get-Process igi1ed -ErrorAction SilentlyContinue){throw 'Editor remains running; stop serial trial before another launch.'}
}
$results|Format-Table
if(@($results|Where-Object status -eq 'FAIL').Count){exit 1}
