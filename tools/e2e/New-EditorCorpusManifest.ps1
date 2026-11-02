#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameRoot = 'D:\IGI1',
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [ValidateSet('smoke','nightly','exhaustive')][string]$Profile = 'exhaustive'
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw [System.InvalidOperationException]::new($Message) }
function Full([string]$Path) { return [System.IO.Path]::GetFullPath($Path) }
function Relative([string]$Path, [string]$Root) {
    $rootUri = [Uri]((Full $Root).TrimEnd('\') + '\')
    $pathUri = [Uri](Full $Path)
    return [Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}
function RequiredFile([string]$LevelRoot, [string]$RelativePath) {
    $full = Join-Path $LevelRoot $RelativePath
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { Fail "Missing required corpus file: $full" }
    $length = (Get-Item -LiteralPath $full).Length
    if ($length -le 0) { Fail "Required corpus file is empty: $full" }
    $relativePath = Relative $full $GameRoot
    return [ordered]@{
        path=$relativePath
        kind='file'
        bytes=[int64]$length
        sha256=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
        minBytes=[int][Math]::Min($length, 2147483647)
    }
}

function Get-InventoryHash($Scenarios) {
    $records = foreach ($scenario in @($Scenarios | Sort-Object name)) {
        foreach ($file in @($scenario.corpusFiles | Where-Object { $_.kind -eq 'file' } | Sort-Object path)) {
            "$($file.path)|$($file.bytes)|$($file.sha256)"
        }
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($records -join "`n"))
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

$GameRoot = Full $GameRoot
$locationRoot = Join-Path $GameRoot 'missions\location0'
if (-not (Test-Path -LiteralPath $locationRoot -PathType Container)) { Fail "Location corpus missing: $locationRoot" }

$levelDirs = @(Get-ChildItem -LiteralPath $locationRoot -Directory | Where-Object { $_.Name -match '^level([0-9]+)$' } | Sort-Object { [int]$_.Name.Substring(5) })
if ($levelDirs.Count -ne 14) { Fail "Expected exactly 14 location0 levels, discovered $($levelDirs.Count)." }
$numbers = @($levelDirs | ForEach-Object { [int]$_.Name.Substring(5) })
if (($numbers -join ',') -ne '1,2,3,4,5,6,7,8,9,10,11,12,13,14') { Fail "Discovered levels were '$($numbers -join ',')', expected 1 through 14." }

$scenarios = @()
foreach ($levelDir in $levelDirs) {
    $level = [int]$levelDir.Name.Substring(5)
    $levelName = $levelDir.Name
    $required = @(
        (RequiredFile $levelDir.FullName 'objects.qvm'),
        (RequiredFile $levelDir.FullName "$levelName.dat"),
        (RequiredFile $levelDir.FullName "models\$levelName.res"),
        (RequiredFile $levelDir.FullName "textures\$levelName.res"),
        (RequiredFile $levelDir.FullName 'lightmaps\lightmaps.res')
    )
    $terrain = Join-Path $levelDir.FullName 'terrain'
    if (-not (Test-Path -LiteralPath $terrain -PathType Container)) { Fail "Missing terrain directory: $terrain" }
    $corpusAssertions = @($required)
    $corpusAssertions += [ordered]@{ path=(Relative $terrain $GameRoot); kind='directory'; minBytes=0 }
    foreach ($optionalDirectory in @('graphs','ai','envmaps','heightmaps')) {
        $optionalPath = Join-Path $levelDir.FullName $optionalDirectory
        if (Test-Path -LiteralPath $optionalPath -PathType Container) {
            $corpusAssertions += [ordered]@{ path=(Relative $optionalPath $GameRoot); kind='directory'; minBytes=0 }
        }
    }

    $sceneRegion = @(300, 80, 900, 700)
    $pauseRegion = @(760, 0, 776, 864)
    $loadPattern = "\[App\] LoadLevel\(\) COMPLETE for level $level"
    # Keep the assertion ASCII-only so a UTF-8/ANSI boundary cannot corrupt the
    # optional explanatory suffix in the generated JSON manifest.
    $weatherPattern = '\[App\] (WeatherEffect resolved: active=1|No active RainEffect in level)'
    $fatalPattern = '\[App\] (?:FATAL|Failed to load level|Unknown error loading level|Out of memory)'
    # Spline waypoint placeholders intentionally have no texture. Exclude that
    # sentinel while keeping real resolver misses and import-pack misses fatal.
    $texturePattern = '\[TEX\] Texture NOT FOUND: (?!waypoint\b)|AddModelToLevelRes: texture .* not found on disk or in any level''s \.res'
    $steps = @(
        [ordered]@{ id='launch'; type='launch_editor'; level=$level },
        [ordered]@{ id='window'; type='wait_for_window'; timeoutSeconds=60 },
        [ordered]@{ id='loaded'; type='wait_for_log'; pattern=$loadPattern; timeoutSeconds=120 },
        [ordered]@{ id='healthy'; type='assert_process' },
        [ordered]@{ id='weather-resolved'; type='assert_log'; pattern=$weatherPattern; timeoutSeconds=2 },
        [ordered]@{ id='no-load-fatal'; type='assert_log'; pattern=$fatalPattern; mustNotMatch=$true; timeoutSeconds=2 },
        [ordered]@{ id='no-texture-miss'; type='assert_log'; pattern=$texturePattern; mustNotMatch=$true; timeoutSeconds=2 },
        [ordered]@{ id='scene'; type='screenshot'; name='scene' },
        [ordered]@{ id='scene-visible'; type='assert_screenshot_region'; region=$sceneRegion; minNonBlackRatio=0.15; minMeanLuma=20 },
        [ordered]@{ id='open-pause'; type='key'; key='ESC' },
        [ordered]@{ id='pause-settle'; type='wait'; seconds=0.5 },
        [ordered]@{ id='pause'; type='screenshot'; name='pause' },
        [ordered]@{ id='pause-visible'; type='assert_screenshot_region'; region=$pauseRegion; minNonBlackRatio=0.03; minMeanLuma=10 },
        [ordered]@{ id='cursor-in-pause'; type='assert_cursor_visible' },
        [ordered]@{ id='close-pause'; type='key'; key='ESC' },
        [ordered]@{ id='cursor-after-pause'; type='assert_cursor_hidden' },
        [ordered]@{ id='close'; type='close_editor' }
    )
    $pathSteps = @()
    $pathIndex = 0
    foreach ($assertion in $corpusAssertions) {
        $safeId = ([System.IO.Path]::GetFileName($assertion.path) -replace '[^A-Za-z0-9]+','-').Trim('-').ToLowerInvariant()
        if ([string]::IsNullOrWhiteSpace($safeId)) { $safeId = "path-$pathIndex" }
        $pathSteps += [ordered]@{ id="corpus-$safeId-$pathIndex"; type='assert_path'; path=$assertion.path; kind=$assertion.kind; minBytes=$assertion.minBytes }
        $pathIndex++
    }
    $steps = @($steps[0..3] + $pathSteps + $steps[4..($steps.Count - 1)])
    $scenarios += [ordered]@{
        name="level$level-corpus-load-render"
        level=$level
        requiresMutation=$false
        corpusFiles=$corpusAssertions
        steps=$steps
    }

    $scenarios += [ordered]@{
        name="level$level-terrain-shortcut"
        level=$level
        requiresMutation=$false
        steps=@(
            [ordered]@{ id='launch'; type='launch_editor'; level=$level },
            [ordered]@{ id='window'; type='wait_for_window'; timeoutSeconds=60 },
            [ordered]@{ id='loaded'; type='wait_for_log'; pattern=$loadPattern; timeoutSeconds=120 },
            [ordered]@{ id='healthy'; type='assert_process' },
            [ordered]@{ id='terrain-shortcut'; type='key'; key='T' },
            [ordered]@{ id='terrain-settle'; type='wait'; seconds=0.5 },
            [ordered]@{ id='terrain-palette'; type='screenshot'; name='terrain-palette' },
            [ordered]@{ id='terrain-edit-visible'; type='assert_screenshot_color_ratio'; region=@(1440,580,90,270); rgb=@(242,140,26); tolerance=45; minRatio=0.01 },
            [ordered]@{ id='close'; type='close_editor' }
        )
    }
}

$allScenarios = @($scenarios)
# Profiles are selected from authored scenario metadata and stable level order;
# numeric IDs never decide pass/fail, only deterministic breadth.
switch ($Profile) {
    'smoke' {
        $scenarios = @($allScenarios | Where-Object { $_.name -like '*-corpus-load-render' })
        $scenarios += @($allScenarios | Where-Object { $_.name -like '*-terrain-shortcut' } | Select-Object -First 1)
    }
    'nightly' { $scenarios = @($allScenarios) }
    'exhaustive' { $scenarios = @($allScenarios) }
}
$manifest = [ordered]@{
    schemaVersion=1
    generatedBy='New-EditorCorpusManifest.ps1'
    generatedFrom=$GameRoot
    profile=$Profile
    inventoryHash=(Get-InventoryHash $allScenarios)
    selectionHash=(Get-InventoryHash $scenarios)
    coverage=[ordered]@{
        levels=14
        planned=$allScenarios.Count
        applicable=$allScenarios.Count
        selected=$scenarios.Count
        featureCombinations=@('corpus-load-render','terrain-shortcut')
        actions=@('load','render','process-health','corpus-file-presence','authored-weather-resolution','missing-texture-detection','pause-menu','cursor-visibility','graceful-quit','terrain-edit-shortcut')
    }
    scenarios=$scenarios
}
$OutputPath = Full $OutputPath
$parent = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Generated $($scenarios.Count) corpus scenarios at $OutputPath"
