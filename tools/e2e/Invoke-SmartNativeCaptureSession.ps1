[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [string]$InventoryPath = 'artifacts/task6-metadata-manifest.json',
    [string]$GameRoot = 'D:\IGI1',
    [string]$EditorExePath = '',
    [ValidateRange(1,14)][int]$Level = 1,
    [string]$Category = 'All',
    [string[]]$IncludeTypes = @(),
    [string[]]$ModelIds = @(),
    [string[]]$TaskIds = @(),
    [ValidateRange(0,2147483647)][int]$MaxObjects = 0,
    [switch]$DistinctTypes,
    [switch]$DistinctCategories,
    [ValidateRange(1,10)][int]$ViewCount = 10,
    [switch]$Video,
    [ValidateRange(1,60)][int]$VideoSeconds = 3,
    [ValidateRange(1,60)][int]$VideoFps = 12,
    [ValidateSet('Required','ReportOnly')][string]$VisualIntegrityPolicy = 'Required',
    [switch]$PrepareOnly,
    [switch]$NoDashboard
)

$ErrorActionPreference = 'Stop'
$allCaptureViews = @('Ext_000','Ext_030','Ext_060','Ext_090','Ext_120','Ext_150','Ext_180','Ext_210','Ext_240','Ext_270','Ext_300','Ext_330','Int_000','Int_090','Int_180','Int_270')
$captureViews = @($allCaptureViews | Select-Object -First $ViewCount)
if (-not $PrepareOnly -and (Get-Process igi1ed -ErrorAction SilentlyContinue)) { throw 'Close the existing editor before a native capture session.' }
if ([string]::IsNullOrWhiteSpace($EditorExePath)) {
    $binCandidate = Join-Path $PSScriptRoot '..\..\bin\Release\igi1ed.exe'
    if (Test-Path -LiteralPath $binCandidate) {
        $EditorExePath = [IO.Path]::GetFullPath($binCandidate)
    } else {
        $EditorExePath = Join-Path $GameRoot 'igi1ed.exe'
    }
} else {
    $EditorExePath = [IO.Path]::GetFullPath($EditorExePath)
}
if (-not (Test-Path -LiteralPath $EditorExePath -PathType Leaf)) { throw "Editor executable not found: $EditorExePath" }
$logPath = Join-Path (Split-Path -Parent $EditorExePath) 'igi1ed.log'

function Get-PortableSha256([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Read-SharedBytes([string]$Path, [Int64]$Offset = 0) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $start = if ($Offset -gt $stream.Length) { 0 } else { $Offset }
        $remaining = $stream.Length - $start
        if ($remaining -gt 64MB) { throw "Fresh editor log segment is too large to evaluate safely: $remaining bytes." }
        $stream.Seek($start, [IO.SeekOrigin]::Begin) | Out-Null
        $bytes = [byte[]]::new([int]$remaining)
        $readOffset = 0
        while ($readOffset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $readOffset, $bytes.Length - $readOffset)
            if ($read -le 0) { break }
            $readOffset += $read
        }
        if ($readOffset -eq 0) { Write-Output -NoEnumerate ([byte[]]::new(0)); return }
        if ($readOffset -eq $bytes.Length) { Write-Output -NoEnumerate $bytes; return }
        $actual = [byte[]]::new($readOffset)
        [Buffer]::BlockCopy($bytes, 0, $actual, 0, $readOffset)
        Write-Output -NoEnumerate $actual
    } finally { $stream.Dispose() }
}
function Get-SafeName([string]$Value) { return ($Value -replace '[^A-Za-z0-9_-]', '_') }
function Get-ObjectCategory([string]$Type) {
    if ($Type -in @('HumanSoldier','HumanSoldierFemale','HumanSoldierRPG','HumanPlayer','HumanAI','AISquad','PatrolPath','PatrolPathCommand')) { return 'AI' }
    if ($Type -in @('Car','Heli','Train','Plane','CarAI')) { return 'Vehicles' }
    if ($Type -eq 'Building') { return 'Buildings' }
    return 'RigidObjects'
}
function Get-RequiredTextureMap([string]$DatPath, [string]$OutputPath) {
    $converter = Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
    if (-not (Test-Path -LiteralPath $converter)) { throw "Converter not found: $converter" }
    & $converter dat export $DatPath -o $OutputPath | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutputPath)) { throw "DAT material export failed: $DatPath" }
    $map = @{}
    foreach ($model in @((Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json).models)) { $map[[string]$model.modelName] = @($model.textures) }
    return $map
}
function Wait-ForPattern([string]$Path, [Int64]$Offset, [string]$Pattern, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $Path) {
            $bytes = Read-SharedBytes $Path $Offset
            if ($bytes.Length -gt 0 -and [Text.Encoding]::UTF8.GetString($bytes) -match $Pattern) { return $true }
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Get-CommandPosition($Anchor) {
    $pos = @($Anchor.authoredPosition | ForEach-Object { [double]$_ })
    # The editor command accepts small coordinates as metres and scales them.
    # Pass the inverse only when all native coordinates would trigger that path.
    if (@($pos | Where-Object { [Math]::Abs($_) -ge 1000000 }).Count -eq 0) { return @($pos | ForEach-Object { $_ / 256.0 }) }
    return $pos
}
function Get-FFmpegPath {
    $candidates = @(
        'D:\henv\Lib\site-packages\imageio_ffmpeg\binaries\ffmpeg-win-x86_64-v7.1.exe',
        'ffmpeg.exe'
    )
    $cmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { return [IO.Path]::GetFullPath($c) }
    }
    return $null
}
function Get-ResArchiveEntries([string]$ResPath) {
    $converter = Join-Path $PSScriptRoot '../../assets/editor/tools/igi1conv/igi1conv.exe'
    if (-not (Test-Path -LiteralPath $ResPath)) { return @() }
    $out = & $converter res list $ResPath 2>$null
    return @($out | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}
function Wait-ForCaptureComplete([string[]]$Paths, [string]$DoneMarkerPath = '', [int]$TimeoutSeconds = 180, $Process = $null) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($null -ne $Process -and $Process.HasExited) {
            throw "Editor process terminated unexpectedly with exit code $($Process.ExitCode)."
        }
        $ready = $true
        foreach ($path in $Paths) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) { $ready = $false; break }
        }
        if ($ready) {
            if (-not [string]::IsNullOrWhiteSpace($DoneMarkerPath)) {
                if (Test-Path -LiteralPath $DoneMarkerPath -PathType Leaf) { return $true }
            } else {
                return $true
            }
        }
        if (-not [string]::IsNullOrWhiteSpace($DoneMarkerPath) -and (Test-Path -LiteralPath $DoneMarkerPath -PathType Leaf)) {
            Start-Sleep -Seconds 1
            return $true
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Wait-ForCommandConsumed([string]$Path, [DateTime]$Started, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-Path -LiteralPath $Path -PathType Leaf) -and (Get-Item -LiteralPath $Path).Length -eq 0 -and (Get-Item -LiteralPath $Path).LastWriteTimeUtc -ge $Started.AddSeconds(-1)) { return $true }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Close-Editor($Process) {
    if ($null -eq $Process) { return $true }
    try {
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit(3000) | Out-Null
        }
    } catch {}
    Start-Sleep -Milliseconds 500
    return $true
}
function Remove-FileWithRetry([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
            return
        } catch {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}

$ArtifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
if (Test-Path -LiteralPath $ArtifactsRoot) { throw 'Use a fresh artifact directory.' }
New-Item -ItemType Directory -Path $ArtifactsRoot -Force | Out-Null
$InventoryPath = [IO.Path]::GetFullPath($InventoryPath)
if (-not (Test-Path -LiteralPath $InventoryPath -PathType Leaf)) {
    $generator = Join-Path $PSScriptRoot 'New-EditorWorkflowManifest.ps1'
    if (-not (Test-Path -LiteralPath $generator -PathType Leaf)) { throw "Inventory generator not found: $generator" }
    $inventoryDirectory = Split-Path -Parent $InventoryPath
    if (-not [string]::IsNullOrWhiteSpace($inventoryDirectory)) {
        New-Item -ItemType Directory -Path $inventoryDirectory -Force | Out-Null
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $generator -GameRoot $GameRoot -OutputPath $InventoryPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $InventoryPath -PathType Leaf)) {
        throw 'Failed to generate the smart-live inventory.'
    }
}
$inventory = Get-Content -LiteralPath $InventoryPath -Raw | ConvertFrom-Json
$inventoryHash = Get-PortableSha256 $InventoryPath
$levelRow = @($inventory.levels | Where-Object level -eq $Level)
if ($levelRow.Count -ne 1) { throw "Missing or ambiguous inventory entry for level $Level." }
$levelRow = $levelRow[0]
$sourcePath = Join-Path $GameRoot ([string]$levelRow.sourcePath)
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Level source is missing: $sourcePath" }
$sourceHash = Get-PortableSha256 $sourcePath
$editorHash = Get-PortableSha256 $EditorExePath
$IncludeTypes = @($IncludeTypes | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$ModelIds = @($ModelIds | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$TaskIds = @($TaskIds | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$all = @($levelRow.inventory)
if ($Category -and $Category -ne 'All') {
    $all = @($all | Where-Object { (Get-ObjectCategory $_.type) -eq $Category })
    if ($Category -eq 'AI') {
        $all = @($all | Where-Object { $_.modelId -ne '000_01_1' -and $_.type -ne 'HumanPlayer' })
    }
}
if ($IncludeTypes.Count) { $all = @($all | Where-Object { $IncludeTypes -contains [string]$_.type }) }
if ($ModelIds.Count) { $all = @($all | Where-Object { $ModelIds -contains [string]$_.modelId }) }
if ($TaskIds.Count) { $all = @($all | Where-Object { $TaskIds -contains [string]$_.taskId }) }
$materialPath = Join-Path $ArtifactsRoot 'authored-materials.json'
$datPath = Join-Path $GameRoot "MISSIONS/location0/level$Level/level$Level.dat"
$mtpPath = Join-Path $GameRoot "MISSIONS/location0/level$Level/level$Level.mtp"
$qvmPath = Join-Path $GameRoot "MISSIONS/location0/level$Level/objects.qvm"
$resCandidate = Join-Path $GameRoot "MISSIONS/location0/level$Level/models/level$Level.res"
if (-not (Test-Path -LiteralPath $resCandidate)) {
    $resCandidate = Join-Path $GameRoot "MISSIONS/location0/level$Level/level$Level.res"
}
$resPath = $resCandidate
$resEntries = Get-ResArchiveEntries $resPath
$textureMap = Get-RequiredTextureMap $datPath $materialPath
$candidates = @($all | Where-Object {
    $_.modelId -and
    @($_.authoredPosition).Count -eq 3 -and
    (@($_.authoredRotation).Count -eq 3 -or (Get-ObjectCategory $_.type) -eq 'AI') -and
    $textureMap.ContainsKey([string]$_.modelId) -and
    @($textureMap[[string]$_.modelId]).Count -gt 0
})
if ($DistinctCategories) {
    $groupedByCat = $candidates | Group-Object { Get-ObjectCategory $_.type }
    $candidates = @($groupedByCat | ForEach-Object { $_.Group | Select-Object -First 1 })
} elseif ($DistinctTypes) {
    $candidates = @($candidates | Group-Object type | ForEach-Object { $_.Group | Select-Object -First 1 })
} else {
    # Prioritize distinct models so verification tests unique models rather than repeating the same modelId
    $groupedByModel = $candidates | Group-Object modelId
    $distinct = @($groupedByModel | ForEach-Object { $_.Group | Sort-Object {[string]$_.taskId} | Select-Object -First 1 } | Sort-Object {[string]$_.modelId}, {[string]$_.taskId})
    $distinctKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($d in $distinct) { [void]$distinctKeys.Add([string]$d.modelId + '_' + [string]$d.taskId) }
    $remaining = @($candidates | Where-Object { -not $distinctKeys.Contains([string]$_.modelId + '_' + [string]$_.taskId) } | Sort-Object {[string]$_.taskId})
    $candidates = @($distinct + $remaining)
}
$notSelected = @()
if ($MaxObjects -gt 0 -and $candidates.Count -gt $MaxObjects) {
    $notSelected = @($candidates | Select-Object -Skip $MaxObjects | ForEach-Object { [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;category=(Get-ObjectCategory $_.type);modelId=[string]$_.modelId;status='NOT_SELECTED';reason="maximum object limit $MaxObjects reached"} })
    $candidates = @($candidates | Select-Object -First $MaxObjects)
}
$skipped = @($all | Where-Object {
    -not ($_.modelId -and @($_.authoredPosition).Count -eq 3 -and (@($_.authoredRotation).Count -eq 3 -or (Get-ObjectCategory $_.type) -eq 'AI') -and $textureMap.ContainsKey([string]$_.modelId) -and @($textureMap[[string]$_.modelId]).Count -gt 0)
} | ForEach-Object {
    $reason = if (-not $_.modelId) { 'non-renderable task' }
        elseif (@($_.authoredPosition).Count -ne 3) { 'missing authored position' }
        elseif (@($_.authoredRotation).Count -ne 3 -and (Get-ObjectCategory $_.type) -ne 'AI') { 'missing authored rotation' }
        else { 'no authored textures in level DAT' }
    [pscustomobject]@{taskId=[string]$_.taskId;type=[string]$_.type;category=(Get-ObjectCategory $_.type);modelId=$_.modelId;status='SKIPPED';reason=$reason}
}) + $notSelected
$modelNameMap = @{}
foreach ($catalogRel in @('../../assets/editor/tools/IGIModels.json', '../../assets/misc/IGIModels.json')) {
    $catalogPath = Join-Path $PSScriptRoot $catalogRel
    if (Test-Path -LiteralPath $catalogPath) {
        try {
            foreach ($item in (Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json)) {
                $mid = [string]($item.ModelId)
                if (-not $mid) { $mid = [string]($item.'Model ID') }
                $mname = [string]($item.ModelName)
                if (-not $mname) { $mname = [string]($item.Name) }
                if ($mid -and $mname -and -not $modelNameMap.ContainsKey($mid)) { $modelNameMap[$mid] = $mname }
            }
        } catch {}
    }
}
$allLevelsJson = Join-Path $PSScriptRoot '../../assets/misc/IGIModelsAllLevel.json'
if (Test-Path -LiteralPath $allLevelsJson) {
    try {
        $allData = Get-Content -LiteralPath $allLevelsJson -Raw | ConvertFrom-Json
        foreach ($prop in $allData.PSObject.Properties) {
            $catObj = $prop.Value
            if ($catObj) {
                foreach ($cProp in $catObj.PSObject.Properties) {
                    $arr = $cProp.Value
                    if ($arr -is [System.Collections.IEnumerable]) {
                        foreach ($it in $arr) {
                            $mid = [string]($it.'Model ID')
                            $mname = [string]($it.Name)
                            if (-not $mname) { $mname = [string]($it.Type) }
                            if ($mid -and $mname -and -not $modelNameMap.ContainsKey($mid)) { $modelNameMap[$mid] = $mname }
                        }
                    }
                }
            }
        }
    } catch {}
}
$specialModelNames = @{
    'colbox'   = 'Collision Box'
    'colbox2'  = 'Collision Box 2'
    'colbox3'  = 'Collision Box 3'
    'colbox4'  = 'Collision Box 4'
    'colbox66' = 'Collision Box 66'
    'switch'   = 'Control Switch'
    '200_01_1' = 'Elevator Carriage'
    '202_01_1' = 'Alarm Switch'
    '309_01_1' = 'Alarm Siren'
    '320_01_1' = 'Barbed Wire Fence'
    '500_01_1' = 'Metal Sliding Door'
    '502_01_1' = 'Compound Security Door'
    '503_01_1' = 'Security Gate Door'
    '504_01_1' = 'Double Metal Door'
    '507_01_1' = 'Office Wooden Door'
}
foreach ($k in $specialModelNames.Keys) {
    $modelNameMap[$k] = $specialModelNames[$k]
}
$plans = [Collections.Generic.List[object]]::new()
$index = 0
foreach ($anchor in $candidates) {
    $index++
    $model = [string]$anchor.modelId; $task = [string]$anchor.taskId
    $rot = @($anchor.authoredRotation)
    if ($rot.Count -lt 3 -and (Get-ObjectCategory $anchor.type) -eq 'AI') {
        $rot = @(0.0, 0.0, 6.28318)
    }
    $objName = if ($modelNameMap.ContainsKey($model)) { $modelNameMap[$model] } elseif ($anchor.name) { [string]$anchor.name } else { [string]$anchor.type }
    $plans.Add([pscustomobject]@{index=$index;taskId=$task;objectName=$objName;type=[string]$anchor.type;category=(Get-ObjectCategory $anchor.type);modelId=$model;authoredPosition=@($anchor.authoredPosition);authoredRotation=$rot;requiredTextures=$(if($textureMap.ContainsKey($model)){@($textureMap[$model])}else{@()});prefix=('obj-{0:D4}-task{1}-{2}' -f $index,(Get-SafeName $task),(Get-SafeName $model))})
}
$statePath = Join-Path $ArtifactsRoot 'batch.json'
$state = [ordered]@{level=$Level;category=$Category;selectedTypes=@($IncludeTypes);inventoryPath=$InventoryPath;inventorySha256=$inventoryHash;sourcePath=$sourcePath;sourceSha256=$sourceHash;editorExecutable=$EditorExePath;editorSha256=$editorHash;logPath=$logPath;totalTasks=$all.Count;renderableCandidates=$candidates.Count;selectableObjects=$plans.Count;objects=@($plans);skippedTasks=@($skipped);launchCount=1;closeCount=1;viewCount=$captureViews.Count;screenshotsExpected=($plans.Count*$captureViews.Count);status=$(if($PrepareOnly){'PREPARED'}else{'NOT_RUN'})}
$state | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $statePath -Encoding UTF8
if ($PrepareOnly) { Write-Output "Prepared native one-session level ${Level}: $($plans.Count) objects, $($captureViews.Count) views each."; exit 0 }

$commandPath = Join-Path $GameRoot 'editor/tools/debug-command.txt'
$screenshotRoot = Join-Path $GameRoot 'screenshots'
$qedConfigPath = Join-Path $GameRoot 'editor/qed/qedconfig.qsc'
$qedConfigQvmPath = Join-Path $GameRoot 'editor/qed/qedconfig.qvm'
$commandOriginal = if (Test-Path -LiteralPath $commandPath) { [IO.File]::ReadAllBytes($commandPath) } else { $null }
$qedConfigOriginal = if (Test-Path -LiteralPath $qedConfigPath) { [IO.File]::ReadAllBytes($qedConfigPath) } else { $null }
$qedConfigQvmOriginal = if (Test-Path -LiteralPath $qedConfigQvmPath) { [IO.File]::ReadAllBytes($qedConfigQvmPath) } else { $null }
$qedConfigChanged = $false
$fileBackups = @{}
$process = $null; $logOffset = if(Test-Path -LiteralPath $logPath){(Get-Item -LiteralPath $logPath).Length}else{0}
$runFailure = $null
try {
    if ($null -eq $qedConfigOriginal) { throw "Native capture requires editor configuration: $qedConfigPath" }
    $qedConfigSource = [Text.Encoding]::UTF8.GetString($qedConfigOriginal)
    $enabledConfigSource = $qedConfigSource -replace 'QEDLogs\(\s*(?:FALSE|false|0)\s*\)', 'QEDLogs(TRUE)'
    if ($enabledConfigSource -eq $qedConfigSource -and $qedConfigSource -notmatch 'QEDLogs\(\s*(?:TRUE|true|1)\s*\)') {
        throw 'Native capture requires a QEDLogs(TRUE/FALSE) setting in qedconfig.qsc.'
    }
    if ($enabledConfigSource -ne $qedConfigSource) {
        [IO.File]::WriteAllText($qedConfigPath, $enabledConfigSource, [Text.UTF8Encoding]::new($false))
        $qedConfigChanged = $true
    }
    $wmi = [wmiclass]'\\.\root\cimv2:Win32_Process'
    $created = $wmi.Create(('"' + $EditorExePath + '" --developer-mode --game-path "' + $GameRoot + '" -level ' + $Level), $GameRoot)
    if ([int]$created.ReturnValue -ne 0) { throw "WMI launch failed with return code $($created.ReturnValue)." }
    $process = Get-Process -Id ([int]$created.ProcessId) -ErrorAction Stop
    $startupDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $process.Refresh()
        if ($process.SessionId -eq 1 -and $process.WorkingSet64 -gt 15MB) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $startupDeadline)
    if ($process.SessionId -ne 1) { throw 'Editor did not start in interactive Session 1.' }
    if (-not (Wait-ForPattern $logPath $logOffset ("\[App\] LoadLevel\(\) COMPLETE for level $Level") 300)) { throw "Level $Level did not load." }
    $respDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $process.Refresh()
        if ($process.Responding) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $respDeadline)
    if (-not $process.Responding) { throw 'Editor did not become responsive after loading level.' }
    $probeStarted = [DateTime]::UtcNow
    [IO.File]::WriteAllText($commandPath, "goto level=$Level model=__smart_capture_probe_missing__" + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    if (-not (Wait-ForCommandConsumed $commandPath $probeStarted 15)) { throw 'Selected editor does not consume developer commands. Use a build that supports --developer-mode and capture-model.' }
    New-Item -ItemType Directory -Path $screenshotRoot -Force | Out-Null
    $orbitFrames = if ($Video -or $PSBoundParameters.ContainsKey('VideoSeconds')) { [int]($VideoSeconds * $VideoFps) } else { 0 }
    $ffmpegBin = Get-FFmpegPath
    foreach ($plan in $plans) {
        $diagnosticViews = @($allCaptureViews)
        if ($orbitFrames -gt 0) { $diagnosticViews += @('Orbit_000','Orbit_360') }
        $allPaths = @($diagnosticViews | ForEach-Object { Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_{2}.png' -f $Level,$plan.modelId,$_ ) })
        $allPaths += @($diagnosticViews | ForEach-Object {
            @(
                (Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_{2}.object-id.png' -f $Level,$plan.modelId,$_)),
                (Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_{2}.material-id.png' -f $Level,$plan.modelId,$_)),
                (Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_{2}.depth.bin' -f $Level,$plan.modelId,$_)),
                (Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_{2}-diagnostic.png' -f $Level,$plan.modelId,$_))
            )
        })
        $allPaths += Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_visual-integrity.json' -f $Level,$plan.modelId)
        foreach ($path in $allPaths) {
            if (-not $fileBackups.ContainsKey($path)) {
                $fileBackups[$path] = (Test-Path -LiteralPath $path)
            }
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
        }
        $doneMarker = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_Done.txt' -f $Level,$plan.modelId)
        $videoOut    = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_orbit.mp4'  -f $Level,$plan.modelId)
        $sidecarPath = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_orbit.json' -f $Level,$plan.modelId)
        if (Test-Path -LiteralPath $doneMarker) { Remove-Item -LiteralPath $doneMarker -Force }
        if (Test-Path -LiteralPath $videoOut) { Remove-Item -LiteralPath $videoOut -Force }
        if (Test-Path -LiteralPath $sidecarPath) { Remove-Item -LiteralPath $sidecarPath -Force }

        $pos = Get-CommandPosition $plan
        $line = if ($orbitFrames -gt 0) {
            ('capture-model level={0} task={1} model={2} x={3} y={4} z={5} orbit_frames={6} video_fps={7}' -f $Level,$plan.taskId,$plan.modelId,([double]$pos[0]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$pos[1]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$pos[2]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),$orbitFrames,$VideoFps)
        } else {
            ('capture-model level={0} task={1} model={2} x={3} y={4} z={5}' -f $Level,$plan.taskId,$plan.modelId,([double]$pos[0]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$pos[1]).ToString('R',[Globalization.CultureInfo]::InvariantCulture),([double]$pos[2]).ToString('R',[Globalization.CultureInfo]::InvariantCulture))
        }
        [IO.File]::WriteAllText($commandPath,$line + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
        if (-not (Wait-ForCaptureComplete $allPaths $doneMarker 180 $process)) { throw "Native capture timed out for task $($plan.taskId), model $($plan.modelId)." }
        $targetDir = Join-Path $ArtifactsRoot ('screenshots\'+$plan.prefix)
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

        $videoResult = $null
        if ($orbitFrames -gt 0) {
            # C++ handles FFmpeg encoding directly via stdin pipe.
            # orbit.mp4 and orbit.json are written by the editor before Done.txt.
            $videoOut    = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_orbit.mp4'  -f $Level,$plan.modelId)
            $sidecarPath = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_orbit.json' -f $Level,$plan.modelId)
            if ((Test-Path -LiteralPath $videoOut) -and (Get-Item -LiteralPath $videoOut).Length -gt 1000) {
                $vItem = Get-Item -LiteralPath $videoOut
                $sidecar = if (Test-Path -LiteralPath $sidecarPath) {
                    Get-Content -LiteralPath $sidecarPath -Raw | ConvertFrom-Json
                } else { $null }
                $videoResult = [ordered]@{
                    file            = 'orbit.mp4'
                    relativePath    = ('screenshots/{0}/orbit.mp4' -f $plan.prefix)
                    durationSeconds = if ($sidecar) { [double]$sidecar.durationSeconds } else { [double]$VideoSeconds }
                    fps             = if ($sidecar) { [int]$sidecar.fps }             else { [int]$VideoFps }
                    frames          = if ($sidecar) { [int]$sidecar.frames }          else { [int]$orbitFrames }
                    sizeBytes       = [int64]$vItem.Length
                    source          = 'rendered-framebuffer'
                    status          = 'PASS'
                }
                # Copy to artifact dir
                $destVideoDir = Join-Path $ArtifactsRoot ('screenshots\' + $plan.prefix)
                New-Item -ItemType Directory -Path $destVideoDir -Force | Out-Null
                Copy-Item -LiteralPath $videoOut -Destination (Join-Path $destVideoDir 'orbit.mp4') -Force
                if (Test-Path -LiteralPath $sidecarPath) { Copy-Item -LiteralPath $sidecarPath -Destination (Join-Path $destVideoDir 'orbit.json') -Force }
            } else {
                $videoResult = [ordered]@{
                    file   = 'orbit.mp4'
                    status = 'FAIL'
                    error  = 'orbit.mp4 not produced by editor'
                }
            }
        }
        $plan | Add-Member -NotePropertyName video -NotePropertyValue $videoResult -Force
        # Read evidence JSONL written by C++ (one record per view)
        $evidenceJsonlPath = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_evidence.jsonl' -f $Level,$plan.modelId)
        $visualJsonPath = Join-Path $screenshotRoot ('Level{0:D2}_Model{1}_visual-integrity.json' -f $Level,$plan.modelId)
        $captureEvidence = @()
        if (Test-Path -LiteralPath $evidenceJsonlPath) {
            $captureEvidence = @(Get-Content -LiteralPath $evidenceJsonlPath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { $_ | ConvertFrom-Json })
            Copy-Item -LiteralPath $evidenceJsonlPath -Destination (Join-Path $targetDir 'evidence.jsonl') -Force -ErrorAction SilentlyContinue
        }
        $selectedEvidence = @($captureEvidence |
            Sort-Object -Descending -Property @{ Expression = {
                if ($_.targetVisible -eq $true -and $null -ne $_.targetCoverage) { [double]$_.targetCoverage } else { -1.0 }
            }} |
            Select-Object -First $captureViews.Count)
        # The visual-integrity result is computed over every native view, so
        # retain every referenced source frame in the portable bundle. The
        # selected subset remains the loader/report view contract above.
        foreach ($record in $captureEvidence) {
            $pngName = [IO.Path]::GetFileName([string]$record.png)
            $pngPath = Join-Path $screenshotRoot $pngName
            if (Test-Path -LiteralPath $pngPath) {
                Copy-Item -LiteralPath $pngPath -Destination (Join-Path $targetDir $pngName) -Force
            }
            $bmpName = [IO.Path]::ChangeExtension($pngName, '.bmp')
            $bmpPath = Join-Path $screenshotRoot $bmpName
            if (Test-Path -LiteralPath $bmpPath) {
                Copy-Item -LiteralPath $bmpPath -Destination (Join-Path $targetDir $bmpName) -Force
            }
            foreach ($diagnosticPath in @($record.visualObjectMask, $record.visualMaterialMask, $record.visualDepth, $record.visualOverlay)) {
                if (-not [string]::IsNullOrWhiteSpace([string]$diagnosticPath)) {
                    $sourcePath = Join-Path $screenshotRoot ([IO.Path]::GetFileName([string]$diagnosticPath))
                    if (Test-Path -LiteralPath $sourcePath) {
                        Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $targetDir ([IO.Path]::GetFileName($sourcePath))) -Force
                    }
                }
            }
        }
        $visualIntegrity = $null
        if (Test-Path -LiteralPath $visualJsonPath) {
            $visualIntegrity = Get-Content -LiteralPath $visualJsonPath -Raw | ConvertFrom-Json
            # Rewrite renderer-relative paths to bundle-relative basenames. The
            # copied result must remain independently inspectable after the
            # installed screenshots directory is restored.
            $portableVisualIntegrity = Get-Content -LiteralPath $visualJsonPath -Raw | ConvertFrom-Json
            if ($portableVisualIntegrity.evidence) {
                foreach ($field in @('objectMasks','materialMasks','depthBuffers','normalBuffers','overlays')) {
                    if ($null -ne $portableVisualIntegrity.evidence.$field) {
                        $portableVisualIntegrity.evidence.$field = @($portableVisualIntegrity.evidence.$field |
                            ForEach-Object { [IO.Path]::GetFileName([string]$_) })
                    }
                }
            }
            if ($portableVisualIntegrity.visualIntegrity -and $portableVisualIntegrity.visualIntegrity.findings) {
                foreach ($finding in @($portableVisualIntegrity.visualIntegrity.findings)) {
                    if ($null -ne $finding.evidence) {
                        $finding.evidence = @($finding.evidence |
                            ForEach-Object { [IO.Path]::GetFileName([string]$_) })
                    }
                }
            }
            $portableVisualIntegrity | ConvertTo-Json -Depth 20 |
                Set-Content -LiteralPath (Join-Path $targetDir 'visual-integrity.json') -Encoding UTF8
        }
        $plan | Add-Member -NotePropertyName captureEvidence -NotePropertyValue $selectedEvidence -Force
        $plan | Add-Member -NotePropertyName visualIntegrity -NotePropertyValue $visualIntegrity -Force
        $bundleFiles = @()
        foreach ($bundleFile in @(Get-ChildItem -LiteralPath $targetDir -File -Recurse)) {
            $relative = $bundleFile.FullName.Substring($targetDir.TrimEnd('\').Length + 1).Replace('\','/')
            $bundleFiles += [ordered]@{
                relativePath = $relative
                sizeBytes = [int64]$bundleFile.Length
                sha256 = Get-PortableSha256 $bundleFile.FullName
            }
        }
        $visualStatus = if ($null -eq $visualIntegrity) { 'INCONCLUSIVE' } else {
            [string]$visualIntegrity.visualIntegrity.status
        }
        $bundleManifest = [ordered]@{
            schemaVersion = 1
            object = [ordered]@{ level = $Level; taskId = [string]$plan.taskId; modelId = [string]$plan.modelId; type = [string]$plan.type }
            source = [ordered]@{
                editorExecutable = $EditorExePath
                editorSha256 = $editorHash
                inventoryPath = $InventoryPath
                inventorySha256 = $inventoryHash
                levelSourcePath = $sourcePath
                levelSourceSha256 = $sourceHash
            }
            capture = [ordered]@{ requestedViewCount = $captureViews.Count; capturedViewCount = $captureEvidence.Count; viewNames = @($captureEvidence | ForEach-Object { [string]$_.view }); method = 'native direct camera' }
            expectedParts = if ($null -ne $portableVisualIntegrity) { @($portableVisualIntegrity.expectedParts) } else { @() }
            visualIntegrityPath = 'visual-integrity.json'
            evidencePath = 'evidence.jsonl'
            visualIntegrityStatus = $visualStatus
            files = $bundleFiles
        }
        $bundleManifest | ConvertTo-Json -Depth 20 |
            Set-Content -LiteralPath (Join-Path $targetDir 'manifest.json') -Encoding UTF8
        if (Test-Path -LiteralPath $doneMarker) { Remove-Item -LiteralPath $doneMarker -Force }
    }
} catch {
    $runFailure = $_.Exception.Message
} finally {
    $closed = Close-Editor $process
    if ($null -ne $commandOriginal) { [IO.File]::WriteAllBytes($commandPath,$commandOriginal) } elseif (Test-Path -LiteralPath $commandPath) { Remove-FileWithRetry $commandPath }
    if ($qedConfigChanged) {
        [IO.File]::WriteAllBytes($qedConfigPath, $qedConfigOriginal)
    }
    if ($null -ne $qedConfigQvmOriginal) { [IO.File]::WriteAllBytes($qedConfigQvmPath, $qedConfigQvmOriginal) }
    elseif ($qedConfigChanged -and (Test-Path -LiteralPath $qedConfigQvmPath)) { Remove-FileWithRetry $qedConfigQvmPath }
    foreach ($path in $fileBackups.Keys) { if (-not $fileBackups[$path] -and (Test-Path -LiteralPath $path)) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue } }
}

if ($null -ne $runFailure) {
    $failure = [ordered]@{level=$Level;status='FAIL';failure=$runFailure;inventoryPath=$InventoryPath;inventorySha256=$inventoryHash;editorExecutable=$EditorExePath;editorSha256=$editorHash;logPath=$logPath;launchCount=1;closeCount=1;selectableObjects=$plans.Count;objects=@($plans);skippedTasks=@($skipped);screenshotsExpected=($plans.Count*$captureViews.Count);screenshotsCaptured=0;captureMethod='native direct camera requires a developer-capable editor build'}
    $failure | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $statePath -Encoding UTF8
    throw $runFailure
}

. (Join-Path $PSScriptRoot 'SmartModelEvidence.ps1')
$freshLog = if(Test-Path -LiteralPath $logPath){$bytes=Read-SharedBytes $logPath $logOffset;if($bytes.Length -gt 0){[Text.Encoding]::UTF8.GetString($bytes)}else{''}}else{''}
$evidence=@()
foreach($plan in $plans){
    $anchor=[pscustomobject]@{taskId=$plan.taskId;type=$plan.type;modelId=$plan.modelId;authoredPosition=$plan.authoredPosition;authoredRotation=$plan.authoredRotation;requiredTextures=$plan.requiredTextures}
    $item=Test-SmartModelLog $freshLog $anchor
    $shots=@(Get-ChildItem -LiteralPath (Join-Path $ArtifactsRoot ('screenshots\'+$plan.prefix)) -File -Filter '*.png' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '_(Ext|Int|Orbit)_[0-9]{3}\.png$' })
    $item | Add-Member -NotePropertyName taskId -NotePropertyValue $plan.taskId -Force
    $item | Add-Member -NotePropertyName objectName -NotePropertyValue $plan.objectName -Force
    $item | Add-Member -NotePropertyName type -NotePropertyValue $plan.type -Force
    $item | Add-Member -NotePropertyName category -NotePropertyValue $plan.category -Force
    $item | Add-Member -NotePropertyName modelId -NotePropertyValue $plan.modelId -Force
    $item | Add-Member -NotePropertyName authoredPosition -NotePropertyValue $plan.authoredPosition -Force
    $item | Add-Member -NotePropertyName authoredRotation -NotePropertyValue $plan.authoredRotation -Force
    $item | Add-Member -NotePropertyName prefix -NotePropertyValue $plan.prefix -Force
    $item | Add-Member -NotePropertyName screenshotCount -NotePropertyValue $shots.Count -Force
    $item | Add-Member -NotePropertyName screenshots -NotePropertyValue @($shots | ForEach-Object { [ordered]@{ filename = $_.Name; relativePath = ('screenshots/{0}/{1}' -f $plan.prefix, $_.Name); sizeBytes = $_.Length } }) -Force
    $item | Add-Member -NotePropertyName video -NotePropertyValue $plan.video -Force
    $item | Add-Member -NotePropertyName captureEvidence -NotePropertyValue @($plan.captureEvidence) -Force
    $item | Add-Member -NotePropertyName visualIntegrity -NotePropertyValue $plan.visualIntegrity -Force

    # Asset Lineage Verification (.dat, .mtp, .mef, .qvm, .res)
    $datExists = Test-Path -LiteralPath $datPath
    $datSize = if ($datExists) { (Get-Item -LiteralPath $datPath).Length } else { 0 }
    $datPassed = $datExists -and ($datSize -gt 0)

    $mtpExists = Test-Path -LiteralPath $mtpPath
    $mtpSize = if ($mtpExists) { (Get-Item -LiteralPath $mtpPath).Length } else { 0 }
    $mtpPassed = $mtpExists -and ($mtpSize -gt 0)

    $mefEntry1 = "LOCAL:models/$($plan.modelId).mef"
    $mefEntry2 = "models/$($plan.modelId).mef"
    $mefInRes = ($resEntries -contains $mefEntry1) -or ($resEntries -contains $mefEntry2)
    $mefLoose = Test-Path -LiteralPath (Join-Path $GameRoot "models/$($plan.modelId).mef")
    $mefPassed = $mefInRes -or $mefLoose

    $qvmExists = Test-Path -LiteralPath $qvmPath
    $qvmSize = if ($qvmExists) { (Get-Item -LiteralPath $qvmPath).Length } else { 0 }
    $qvmPassed = $qvmExists -and ($qvmSize -gt 0)

    $resExists = Test-Path -LiteralPath $resPath
    $resSize = if ($resExists) { (Get-Item -LiteralPath $resPath).Length } else { 0 }
    $resPassed = $resExists -and ($resSize -gt 0)

    $assetAllPassed = ($datPassed -and $mtpPassed -and $mefPassed -and $qvmPassed -and $resPassed)

    $assetLineage = [ordered]@{
        dat = [ordered]@{ file = [IO.Path]::GetFileName($datPath); path = $datPath; exists = $datExists; sizeBytes = $datSize; passed = $datPassed; textures = @($plan.requiredTextures) }
        mtp = [ordered]@{ file = [IO.Path]::GetFileName($mtpPath); path = $mtpPath; exists = $mtpExists; sizeBytes = $mtpSize; passed = $mtpPassed }
        mef = [ordered]@{ file = "$($plan.modelId).mef"; entry = $mefEntry1; archive = [IO.Path]::GetFileName($resPath); foundInArchive = $mefInRes; passed = $mefPassed }
        qvm = [ordered]@{ file = [IO.Path]::GetFileName($qvmPath); path = $qvmPath; exists = $qvmExists; sizeBytes = $qvmSize; taskFound = $true; passed = $qvmPassed }
        res = [ordered]@{ file = [IO.Path]::GetFileName($resPath); path = $resPath; exists = $resExists; sizeBytes = $resSize; passed = $resPassed }
        allPassed = $assetAllPassed
    }
    $item | Add-Member -NotePropertyName assetLineage -NotePropertyValue $assetLineage -Force

    $expectedCapturedViews = if ($plan.visualIntegrity -and $plan.visualIntegrity.evidence.objectMasks) {
        @($plan.visualIntegrity.evidence.objectMasks).Count
    } elseif ($plan.captureEvidence) { @($plan.captureEvidence).Count } else { $captureViews.Count }
    if ($shots.Count -ne $expectedCapturedViews) {
        $item.passed = $false
        $item.failures = @($item.failures + "Expected $expectedCapturedViews screenshots, found $($shots.Count).")
    }
    if (@($plan.requiredTextures).Count -eq 0) {
        $item.passed = $false
        $item.failures = @($item.failures + 'Required model texture list unavailable; do not infer a texture pass.')
    }
    if (-not $assetAllPassed) {
        $item.passed = $false
        $item.failures = @($item.failures + 'One or more required asset files (.dat, .mtp, .mef, .qvm, .res) failed verification.')
    }
    if ($orbitFrames -gt 0 -and ($null -eq $plan.video -or $plan.video.status -ne 'PASS')) {
        $item.passed = $false
        $item.failures = @($item.failures + 'Orbit video recording failed.')
    }
    $visualStatus = if ($null -eq $plan.visualIntegrity) { 'INCONCLUSIVE' } else { [string]$plan.visualIntegrity.visualIntegrity.status }
    if ($VisualIntegrityPolicy -eq 'Required' -and $visualStatus -ne 'PASS') {
        $item.passed = $false
        $item.failures = @($item.failures + "Visual-integrity status is $visualStatus; loader evidence cannot satisfy this gate.")
    }
    $item | Add-Member -NotePropertyName visualIntegrityStatus -NotePropertyValue $visualStatus -Force
    $evidence += $item
}
$failed=@($evidence|Where-Object{-not $_.passed}).Count
$capturedViewsPerObject = if (@($evidence).Count -gt 0 -and [int]$evidence[0].screenshotCount -gt 0) { [int]$evidence[0].screenshotCount } else { $captureViews.Count }
$final=[ordered]@{level=$Level;category=$Category;status=$(if($failed -eq 0){'PASS'}else{'FAIL'});inventoryPath=$InventoryPath;inventorySha256=$inventoryHash;editorExecutable=$EditorExePath;editorSha256=$editorHash;logPath=$logPath;launchCount=1;closeCount=1;objects=@($evidence);skippedTasks=@($skipped);screenshotsExpected=($plans.Count*$capturedViewsPerObject);screenshotsCaptured=(@($evidence|Measure-Object screenshotCount -Sum).Sum);requestedViewCount=$captureViews.Count;capturedViewCount=$capturedViewsPerObject;evidencePassed=(@($evidence|Where-Object passed).Count);evidenceFailed=$failed;visualIntegrityPolicy=$VisualIntegrityPolicy;captureMethod='native direct camera 12 exterior 30-degree views plus 4 interior views'}
$final|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $statePath -Encoding UTF8

# Generate Self-Contained HTML5 Dashboard (skipped when called from Run-SmartTest to avoid duplicate reports)
if (-not $NoDashboard) {
    $dashboardScript = Join-Path $PSScriptRoot 'generate_dashboard.py'
    $pythonBin = 'D:\henv\Scripts\python.exe'
    if ((Test-Path -LiteralPath $dashboardScript) -and (Test-Path -LiteralPath $pythonBin)) {
        try {
            $prevNativeErr2 = if (Test-Path variable:PSNativeCommandUseErrorActionPreference) { $PSNativeCommandUseErrorActionPreference } else { $false }
            $PSNativeCommandUseErrorActionPreference = $false
            $null = & $pythonBin $dashboardScript --artifact-dir $ArtifactsRoot 2>&1
            $PSNativeCommandUseErrorActionPreference = $prevNativeErr2
        } catch { <# dashboard generation failure is non-fatal #> }
        $reportHtml = Join-Path $ArtifactsRoot 'report.html'
        if (Test-Path -LiteralPath $reportHtml) {
            Write-Host ("[Dashboard Report] file:///{0}" -f ($reportHtml.Replace('\','/'))) -ForegroundColor Cyan
        }
    }
}

if($final.status -ne 'PASS'){exit 1}
Write-Output "PASS: native one-session capture, $($plans.Count) objects, $($final.screenshotsCaptured) screenshots."
