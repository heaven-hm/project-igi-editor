[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CameraPlan,
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [ValidateRange(1,14)][int]$Level = 1,
    [ValidatePattern('^(\d+|-1#\d+)$')][string]$TaskId = '1105',
    [ValidateRange(1,10)][int]$ViewCount = 1,
    [string]$ViewName = '',
    [ValidateRange(0,60)][int]$CooldownSeconds = 5,
    [switch]$SkipObjectSelection,
    [switch]$HideTerrainDiagnostic,
    [switch]$AllowConfigMutation
)
$ErrorActionPreference = 'Stop'
if (-not $AllowConfigMutation) { throw 'Requires -AllowConfigMutation; QED config is temporarily changed and restored.' }
if (Get-Process igi1ed -ErrorAction SilentlyContinue) { throw 'Close the existing editor before the serial pilot.' }
if ($CooldownSeconds -gt 0) { Start-Sleep -Seconds $CooldownSeconds }
. (Join-Path $PSScriptRoot 'SmartModelEvidence.ps1')
function Get-PortableSha256([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','') }
    finally { $sha.Dispose() }
}
$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$levelInventory = @($inventory.levels | Where-Object level -eq $Level)
if ($levelInventory.Count -ne 1) { throw 'Missing or ambiguous level inventory.' }
$anchors = @($levelInventory[0].inventory | Where-Object taskId -eq $TaskId)
if ($anchors.Count -ne 1) { throw 'Missing or ambiguous object anchor.' }
$anchor = $anchors[0]
$sourcePath = Join-Path $GameRoot $levelInventory[0].sourcePath
if ((Get-PortableSha256 $sourcePath) -ne $anchor.sourceHash) { throw 'Stale object inventory.' }
$plan = Get-Content -LiteralPath $CameraPlan -Raw | ConvertFrom-Json
if (@($plan.views).Count -ne 10) { throw 'Expected ten planned camera views.' }
foreach ($view in $plan.views) {
    if ($view.name -notmatch '^(azimuth-(0|45|90|135|180|225|270|315)|above|below)$' -or @($view.position).Count -ne 3) { throw 'Invalid camera view.' }
    foreach ($number in @($view.position)+@($view.yaw,$view.pitch,$view.roll)) {
        $value = [double]$number
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw 'Non-finite camera pose.' }
    }
}
if (@($plan.views.name | Select-Object -Unique).Count -ne 10) { throw 'Duplicate camera views.' }
$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
if (Test-Path $ArtifactsRoot) { throw 'Use a fresh artifact directory; recovery backups must not be overwritten.' }
New-Item -ItemType Directory $ArtifactsRoot | Out-Null
$converter = Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
$materialPath = Join-Path $ArtifactsRoot 'authored-materials.json'
& $converter dat export (Join-Path $GameRoot "MISSIONS/location0/level$Level/level$Level.dat") --filter $anchor.modelId -o $materialPath
if ($LASTEXITCODE -ne 0) { throw 'Cannot read authored model materials.' }
$materials = Get-Content $materialPath -Raw | ConvertFrom-Json
$modelMaterials = @($materials.models | Where-Object modelName -eq $anchor.modelId)
if ($modelMaterials.Count -ne 1 -or @($modelMaterials[0].textures).Count -eq 0) { throw 'Required model texture list unavailable; do not infer a pass.' }
$anchor | Add-Member -NotePropertyName requiredTextures -NotePropertyValue @($modelMaterials[0].textures) -Force
$backup = Join-Path $ArtifactsRoot 'qed-backup'
New-Item -ItemType Directory $backup | Out-Null
$qed = Join-Path $GameRoot 'editor/qed'
$files = @(Get-ChildItem $qed -File | Where-Object Extension -in @('.qsc','.qvm'))
foreach ($file in @($files | Where-Object Extension -eq '.qsc')) {
    if (-not (Test-Path ([IO.Path]::ChangeExtension($file.FullName,'.qvm')))) { throw 'Compile missing QED siblings before capture so restoration covers every generated file.' }
}
$snapshots = @(foreach ($file in $files) {
    Copy-Item -LiteralPath $file.FullName -Destination $backup
    [pscustomobject]@{name=$file.Name;sha256=(Get-PortableSha256 $file.FullName)}
})
$snapshots | ConvertTo-Json | Set-Content (Join-Path $backup 'hashes.json') -Encoding UTF8
$config = Join-Path $qed 'qedconfig.qsc'
$original = Get-Content -LiteralPath $config -Raw
$invariant = [Globalization.CultureInfo]::InvariantCulture
try {
    $views = if ($ViewName) { @($plan.views | Where-Object name -eq $ViewName) } else { @($plan.views | Select-Object -First $ViewCount) }
    if ($views.Count -eq 0) { throw 'Requested camera view is absent.' }
    $viewIndex = 0
    foreach ($view in $views) {
        if ($viewIndex -gt 0 -and $CooldownSeconds -gt 0) { Start-Sleep -Seconds $CooldownSeconds }
        $viewIndex++
        $text = $original
        $position = @($view.position | ForEach-Object { ([double]$_).ToString('R',$invariant) }) -join ', '
        # An all-zero orientation means "use spawn orientation" in the editor.
        # 360 degrees is the same heading as zero but explicitly selects the pose.
        $yaw = if ($view.yaw -eq 0 -and $view.pitch -eq 0 -and $view.roll -eq 0) { 360 } else { $view.yaw }
        $orientation = @($yaw,$view.pitch,$view.roll | ForEach-Object { ([double]$_).ToString('R',$invariant) }) -join ', '
        $settings = @{QEDSetCameraPosition=$position;QEDSetCameraOrientation=$orientation;QEDAutoSaveEnabled='FALSE';QEDSaveConfigOnExit='FALSE'}
        foreach ($name in $settings.Keys) {
            $pattern = '(?m)^\s*'+[regex]::Escape($name)+'\([^\r\n;]*\);'
            if ([regex]::Matches($text,$pattern).Count -ne 1) { throw "Expected exactly one $name config declaration." }
            $text = [regex]::Replace($text,$pattern,($name+'('+$settings[$name]+');'))
        }
        [IO.File]::WriteAllText($config,$text,[Text.UTF8Encoding]::new($false))
        $steps = @(
            @{id='launch';type='launch_editor';level=$Level;drawParts=$(if($HideTerrainDiagnostic){-2}else{-1})},
            @{id='loaded';type='wait_for_log';pattern="\[App\] LoadLevel\(\) COMPLETE for level $Level";timeoutSeconds=120},
            @{id='health';type='assert_process'},
            @{id='capture';type='screenshot';name=$view.name;client=$true},
            @{id='window';type='capture_window_state'},
            @{id='close';type='close_editor';force=$false}
        )
        if (-not $SkipObjectSelection) {
            $steps = @($steps[0..2] + @(
                @{id='find';type='key';key='CTRL+SHIFT+I'},
                @{id='id';type='type_text';text=$TaskId},
                @{id='select';type='key';key='ENTER'},
                @{id='settle';type='wait';seconds=2}
            ) + $steps[3..($steps.Count-1)])
        }
        $manifest = Join-Path $ArtifactsRoot ($view.name+'.json')
        @{diagnosticTerrainHidden=[bool]$HideTerrainDiagnostic;scenarios=@(@{name=$view.name;level=$Level;requiresMutation=$false;steps=$steps})} | ConvertTo-Json -Depth 10 | Set-Content $manifest -Encoding UTF8
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'editor-e2e.ps1') -GameRoot $GameRoot -ScenarioPath $manifest -ArtifactsRoot (Join-Path $ArtifactsRoot $view.name)
        if ($LASTEXITCODE -ne 0) { throw "Capture failed for $($view.name)." }
        Start-Sleep -Milliseconds 400
        # Retain a stable log snapshot before the next launch appends to it.
        Copy-Item (Join-Path $GameRoot 'igi1ed.log') (Join-Path $ArtifactsRoot ($view.name+'.log'))
        $run = Get-Content (Join-Path (Join-Path $ArtifactsRoot $view.name) 'run.json') -Raw | ConvertFrom-Json
        $logBytes = [IO.File]::ReadAllBytes((Join-Path $ArtifactsRoot ($view.name+'.log')))
        $offset = [int]$run.results[0].logOffset
        $freshLog = [Text.Encoding]::UTF8.GetString($logBytes,$offset,$logBytes.Length-$offset)
        $evidence = Test-SmartModelLog $freshLog $anchor
        $evidence | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $ArtifactsRoot ($view.name+'-model-evidence.json')) -Encoding UTF8
        if (-not $evidence.passed) { throw "Model evidence failed: $($evidence.failures -join '; ')" }
    }
} finally {
    $remaining = @(Get-Process igi1ed -ErrorAction SilentlyContinue)
    foreach ($process in $remaining) {
        & taskkill.exe /PID $process.Id 2>$null | Out-Null
        if (-not $process.WaitForExit(4000)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            if (-not $process.WaitForExit(3000)) { throw "Editor still running after forced cleanup; retained recovery backup at $backup." }
        }
    }
    foreach ($snapshot in $snapshots) {
        $dest = Join-Path $qed $snapshot.name
        Copy-Item (Join-Path $backup $snapshot.name) $dest -Force
        if ((Get-PortableSha256 $dest) -ne $snapshot.sha256) { throw "Restore mismatch: $dest; backup retained at $backup" }
    }
    'Original QED files restored and hashes verified.'
    if ((Get-PortableSha256 $sourcePath) -ne $anchor.sourceHash) { throw 'Level source changed during capture; investigate before further runs.' }
}
