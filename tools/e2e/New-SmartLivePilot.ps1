[CmdletBinding()]
param(
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$OutputPath = 'artifacts/e2e/smart-live-pilot.json',
    [int]$Level = 1,
    [string]$TaskId = '1105'
)
$ErrorActionPreference = 'Stop'
$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$levelData = @($inventory.levels | Where-Object level -eq $Level)
if ($levelData.Count -ne 1) { throw 'Pilot level must identify one inventory entry.' }
$anchor = @($levelData[0].inventory | Where-Object taskId -eq $TaskId)
if ($anchor.Count -ne 1 -or -not $anchor[0].renderable) { throw 'Pilot must identify one renderable task.' }
$anchor = $anchor[0]
$source = Join-Path $inventory.generatedFrom $levelData[0].sourcePath
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $anchor.sourceHash) { throw 'Stale inventory: regenerate before the pilot.' }
$steps = @(
    @{id='source-before';type='assert_file_hash';path=$levelData[0].sourcePath;sha256=$anchor.sourceHash},
    @{id='launch';type='launch_editor';level=$Level},
    @{id='window';type='wait_for_window';timeoutSeconds=45},
    @{id='loaded';type='wait_for_log';pattern="\[App\] LoadLevel\(\) COMPLETE for level $Level";timeoutSeconds=120},
    @{id='open-find';type='key';key='CTRL+SHIFT+I'},
    @{id='find-id';type='type_text';text=$TaskId},
    @{id='find-confirm';type='key';key='ENTER'},
    @{id='find-settle';type='wait';seconds=1},
    @{id='focus';type='key';key='F11'},
    @{id='focus-settle';type='wait';seconds=1},
    @{id='initial';type='screenshot';name='initial'}
)
foreach ($angle in @('front','front-right','right','back-right','back','back-left','left','front-left')) {
    $steps += @{id="orbit-$angle";type='orbit_camera';angle=$angle;pixels=12;distance=300}
    $steps += @{id="shot-$angle";type='screenshot';name=$angle}
}
$steps += @{id='window-evidence';type='capture_window_state'}
$steps += @{id='source-after';type='assert_file_hash';path=$levelData[0].sourcePath;sha256=$anchor.sourceHash}
$steps += @{id='close';type='close_editor';force=$true}
$scenario = @{name="smart-pilot-level$Level-task$TaskId";level=$Level;anchor=$anchor;requiresMutation=$false;steps=$steps}
# Capture success is not acceptance: a separate evidence review must prove
# framing, poses, authored transforms, material assignment, and attachments.
$manifest = @{pilotOnly=$true;acceptance='UNVERIFIED';scenarios=@($scenario)}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
"Pilot only: level=$Level task=$TaskId model=$($anchor.modelId); acceptance remains UNVERIFIED."
