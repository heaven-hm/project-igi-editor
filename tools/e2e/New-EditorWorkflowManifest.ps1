#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameRoot = 'D:\IGI1',
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw [System.InvalidOperationException]::new($Message) }
function Full([string]$Path) { return [System.IO.Path]::GetFullPath($Path) }
function Values($Value) { if ($null -eq $Value) { return @() }; return @($Value) }
function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function RequireFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Fail "Required file is missing: $Path" }
}
function Unquote([string]$Value) {
    $trimmed = $Value.Trim()
    if ($trimmed.Length -ge 2 -and $trimmed.StartsWith('"') -and $trimmed.EndsWith('"')) {
        return $trimmed.Substring(1, $trimmed.Length - 2).Replace('\"', '"').Replace('\\', '\')
    }
    return $trimmed
}
function Get-MatchingCloseParen([string]$Text, [int]$OpenIndex) {
    $depth = 0
    $quoted = $false
    $escaped = $false
    for ($index = $OpenIndex; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        if ($quoted) {
            if ($escaped) { $escaped = $false; continue }
            if ($character -eq '\') { $escaped = $true; continue }
            if ($character -eq '"') { $quoted = $false }
            continue
        }
        if ($character -eq '"') { $quoted = $true; continue }
        if ($character -eq '(') { $depth++; continue }
        if ($character -eq ')') {
            $depth--
            if ($depth -eq 0) { return $index }
        }
    }
    Fail "Unclosed function call at character $OpenIndex."
}
function Split-TopLevelArguments([string]$Text) {
    $result = New-Object System.Collections.Generic.List[string]
    $start = 0
    $depth = 0
    $quoted = $false
    $escaped = $false
    for ($index = 0; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        if ($quoted) {
            if ($escaped) { $escaped = $false; continue }
            if ($character -eq '\') { $escaped = $true; continue }
            if ($character -eq '"') { $quoted = $false }
            continue
        }
        if ($character -eq '"') { $quoted = $true; continue }
        if ($character -eq '(' -or $character -eq '[' -or $character -eq '{') { $depth++; continue }
        if ($character -eq ')' -or $character -eq ']' -or $character -eq '}') { $depth--; continue }
        if ($character -eq ',' -and $depth -eq 0) {
            $result.Add($Text.Substring($start, $index - $start).Trim())
            $start = $index + 1
        }
    }
    $last = $Text.Substring($start).Trim()
    if ($last.Length -gt 0) { $result.Add($last) }
    return $result.ToArray()
}
function Get-FunctionCalls([string]$Text, [string]$Name) {
    $calls = New-Object System.Collections.Generic.List[object]
    $pattern = [regex]::new("\b$([regex]::Escape($Name))\s*\(")
    $startAt = 0
    while ($startAt -lt $Text.Length) {
        $match = $pattern.Match($Text, $startAt)
        if (-not $match.Success) { break }
        $open = $Text.IndexOf('(', $match.Index + $match.Length - 1)
        $close = Get-MatchingCloseParen $Text $open
        $calls.Add([pscustomobject]@{
            start = $match.Index
            end = $close
            arguments = @(Split-TopLevelArguments $Text.Substring($open + 1, $close - $open - 1))
        })
        $startAt = $match.Index + 1
    }
    return $calls.ToArray()
}
function Get-TypeWidth([string]$Type) {
    switch ($Type) {
        'ObjectPos' { return 3 }
        'Real32x3' { return 3 }
        'Real64x3' { return 3 }
        'Real32x9' { return 3 }
        'RGB' { return 3 }
        default { return 1 }
    }
}
function Convert-NumberVector($Arguments, [int]$Offset, [int]$Width) {
    if (($Offset + $Width) -gt $Arguments.Count) { return @() }
    $values = New-Object System.Collections.Generic.List[double]
    for ($index = 0; $index -lt $Width; $index++) {
        $number = 0.0
        if (-not [double]::TryParse(([string]$Arguments[$Offset + $index]).Trim(), [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
            return @()
        }
        $values.Add($number)
    }
    return $values.ToArray()
}
function Get-DeclarationMap([string]$Source) {
    $map = @{}
    foreach ($call in Get-FunctionCalls $Source 'Task_DeclareParameters') {
        if ($call.arguments.Count -lt 1) { continue }
        $typeName = Unquote $call.arguments[0]
        $fields = New-Object System.Collections.Generic.List[object]
        $offset = 3
        for ($index = 1; ($index + 1) -lt $call.arguments.Count; $index += 2) {
            $fieldType = Unquote $call.arguments[$index + 1]
            $width = Get-TypeWidth $fieldType
            $fields.Add([pscustomobject]@{ name=(Unquote $call.arguments[$index]); type=$fieldType; offset=$offset; width=$width })
            $offset += $width
        }
        $map[$typeName] = $fields.ToArray()
    }
    return $map
}
function Get-Field([object[]]$Fields, [string]$Pattern) {
    return @($Fields | Where-Object { $_.name -match $Pattern } | Select-Object -First 1)
}
function Get-FieldText([object[]]$Fields, [object[]]$Arguments, [string]$Pattern) {
    $field = @(Get-Field $Fields $Pattern)
    if ($field.Count -ne 1 -or $field[0].offset -ge $Arguments.Count) { return '' }
    return Unquote ([string]$Arguments[$field[0].offset])
}
function Get-FieldNumber([object[]]$Fields, [object[]]$Arguments, [string]$Pattern, [int]$Default = -1) {
    $field = @(Get-Field $Fields $Pattern)
    if ($field.Count -ne 1 -or $field[0].offset -ge $Arguments.Count) { return $Default }
    $value = $Default
    if ([int]::TryParse((Unquote ([string]$Arguments[$field[0].offset])), [Globalization.NumberStyles]::Integer, [Globalization.CultureInfo]::InvariantCulture, [ref]$value)) {
        return $value
    }
    return $Default
}
function Get-FieldValues([object[]]$Fields, [object[]]$Arguments, [string]$Pattern) {
    $values = New-Object System.Collections.Generic.List[string]
    foreach ($field in @($Fields | Where-Object { $_.name -match $Pattern })) {
        if ($field.offset -lt $Arguments.Count) {
            $value = Unquote ([string]$Arguments[$field.offset])
            if (-not [string]::IsNullOrWhiteSpace($value) -and $value -notmatch '^Task_New\s*\(') { $values.Add($value) }
        }
    }
    return @($values.ToArray() | Sort-Object -Unique)
}
function Get-Sha256Text([string]$Text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Get-FileInventory([string]$Root, [string]$GameRoot) {
    # Keep the source corpus immutable: this is read-only evidence used to
    # reproduce selection and to distinguish an absent asset from an omitted
    # test.  Sort by relative path so filesystem enumeration order cannot
    # change the resulting manifest/hash.
    $records = New-Object System.Collections.Generic.List[object]
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object { Get-RelativePath $_.FullName $GameRoot })) {
        $records.Add([ordered]@{
            path=(Get-RelativePath $file.FullName $GameRoot)
            bytes=[int64]$file.Length
            sha256=(Get-Sha256 $file.FullName)
        })
    }
    return $records.ToArray()
}
function Read-MefAttachments([string]$Path, [string]$ModelId) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    $bytes = [IO.File]::ReadAllBytes($Path)
    $attachments = New-Object System.Collections.Generic.List[object]
    if ($bytes.Length -lt 36 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'ILFF') { return @() }
    $offset = 20
    while (($offset + 16) -le $bytes.Length) {
        $tag = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $size = [BitConverter]::ToUInt32($bytes, $offset + 4)
        $skip = [BitConverter]::ToUInt32($bytes, $offset + 12)
        $data = $offset + 16
        if ($size -gt [uint32]($bytes.Length - $data)) { break }
        if ($tag -eq 'ATTA' -and ($size % 68) -eq 0) {
            for ($record = 0; $record -lt $size; $record += 68) {
                $at = $data + $record
                $name = [Text.Encoding]::ASCII.GetString($bytes, $at, 16).Trim([char]0).Trim()
                $position = @([BitConverter]::ToSingle($bytes, $at + 16), [BitConverter]::ToSingle($bytes, $at + 20), [BitConverter]::ToSingle($bytes, $at + 24))
                $rotation = @()
                for ($r = 0; $r -lt 9; $r++) { $rotation += [BitConverter]::ToSingle($bytes, $at + 28 + ($r * 4)) }
                $attachments.Add([ordered]@{ name=$name; modelId=$ModelId; position=$position; rotation=$rotation; boneId=[BitConverter]::ToInt32($bytes, $at + 64) })
            }
        }
        if ($skip -eq 0) { break }
        if ($skip -lt 16 -or ($offset + $skip) -le $offset -or ($offset + $skip) -gt $bytes.Length) { break }
        $offset += [int]$skip
    }
    return $attachments.ToArray()
}
function Get-RelativePath([string]$Path, [string]$Root) {
    $rootUri = [Uri]((Full $Root).TrimEnd('\') + '\')
    $pathUri = [Uri](Full $Path)
    return [Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}
function Get-ConverterOutput([string]$Converter, [string[]]$Arguments) {
    $output = @(& $Converter @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { Fail "igi1conv failed ($LASTEXITCODE): $($Arguments -join ' ')`n$($output -join [Environment]::NewLine)" }
    return @($output | ForEach-Object { [string]$_ })
}
function Get-ModelLods([string]$Converter, [string[]]$Archives) {
    $lods = @{}
    foreach ($archive in $Archives | Sort-Object -Unique) {
        foreach ($line in Get-ConverterOutput $Converter @('res', 'list', $archive)) {
            $match = [regex]::Match($line, '(?i)([^/\\:]+)_([0-9]+)\.mef$')
            if ($match.Success) {
                $base = $match.Groups[1].Value
                if (-not $lods.ContainsKey($base)) { $lods[$base] = New-Object System.Collections.Generic.List[int] }
                $lods[$base].Add([int]$match.Groups[2].Value)
                continue
            }
            # Bare single-LOD meshes (switch.mef, mapcomputer.mef) carry no LOD
            # suffix; treat them as LOD level 1 so they resolve like any mesh.
            $bareMatch = [regex]::Match($line, '(?i)([^/\\:]+)\.mef$')
            if ($bareMatch.Success) {
                $base = $bareMatch.Groups[1].Value
                if (-not $lods.ContainsKey($base)) { $lods[$base] = New-Object System.Collections.Generic.List[int] }
                $lods[$base].Add(1)
            }
        }
    }
    foreach ($key in @($lods.Keys)) { $lods[$key] = @($lods[$key] | Sort-Object -Unique) }
    return $lods
}
function Get-GraphMetadata([string]$Converter, [string]$GraphPath, [string]$ScratchRoot) {
    $jsonPath = Join-Path $ScratchRoot ('graph-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        Get-ConverterOutput $Converter @('graph', 'export', $GraphPath, '-o', $jsonPath) | Out-Null
        RequireFile $jsonPath
        $document = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
        $nodes = @($document.nodes | ForEach-Object {
            $criteria = [string]$_.criteria
            $class = if ($criteria -match '(?i)DOOR') { 'Door' }
                elseif ($criteria -match '(?i)STAIR') { 'Stair' }
                elseif ($criteria -match '(?i)VIEW') { 'View' }
                else { 'Default' }
            [ordered]@{
                id=[int]$_.id; x=[double]$_.x; y=[double]$_.y; z=[double]$_.z
                gamma=[double]$_.gamma; radius=[double]$_.radius; material=[int]$_.material
                criteria=$criteria; class=$class; link1=[int]$_.link1; link2=[int]$_.link2
            }
        })
        $edges = @($document.edges | ForEach-Object {
            [ordered]@{ from=[int]$_.from; to=[int]$_.to; linkType=[int]$_.link_type }
        })
        if ($nodes.Count -eq 0) { Fail "Graph export reported no nodes for $GraphPath." }
        if ([int]$document.node_count -ne $nodes.Count -or [int]$document.edge_count -ne $edges.Count) {
            Fail "Graph export counts do not match records for $GraphPath."
        }
        $criteriaClasses = @($nodes | ForEach-Object { $_.class } | Sort-Object -Unique)
        $criteriaCounts = [ordered]@{}
        $criterionSamples = @()
        foreach ($class in $criteriaClasses) {
            $matching = @($nodes | Where-Object { $_.class -eq $class })
            $criteriaCounts[$class] = $matching.Count
            $criterionSamples += [ordered]@{
                class=$class; nodeId=[int]$matching[0].id; criteria=[string]$matching[0].criteria
            }
        }
        return [pscustomobject]@{
            nodeCount=$nodes.Count; edgeCount=$edges.Count; nodes=$nodes; edges=$edges
            criteriaClasses=$criteriaClasses; criteriaCounts=$criteriaCounts
            criterionSamples=$criterionSamples
            sampleNodeId=[int]$nodes[0].id; sampleNodeCriteria=[string]$nodes[0].criteria
            sampleNodeClass=[string]$nodes[0].class
        }
    } finally {
        Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
    }
}
function Get-AiScriptMetadata([string]$Converter, [string]$AiPath, [string]$ScratchRoot) {
    $qscPath = Join-Path $ScratchRoot ('ai-' + [guid]::NewGuid().ToString('N') + '.qsc')
    try {
        Get-ConverterOutput $Converter @('qvm', 'decompile', $AiPath, '-o', $qscPath) | Out-Null
        RequireFile $qscPath
        $source = Get-Content -LiteralPath $qscPath -Raw
        $animationIds = @([regex]::Matches($source, 'AIAction_PlayAnimation\s*\(\s*(-?[0-9]+)') |
            ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object -Unique)
        $patrolMatch = [regex]::Match($source, 'AIAction_Patrol\s*\(\s*(-?[0-9]+)')
        $patrolPathId = if ($patrolMatch.Success) { [int]$patrolMatch.Groups[1].Value } else { -1 }
        return [pscustomobject]@{ animationIds=$animationIds; patrolPathId=$patrolPathId }
    } finally {
        Remove-Item -LiteralPath $qscPath -Force -ErrorAction SilentlyContinue
    }
}
function Add-Scenario($Scenarios, $ScenarioKeys, $ActionMap, [string]$Action, [int]$Level, [string]$TaskId, [string]$Type, $Anchor) {
    $key = "$Level|$TaskId|$Action"
    if ($ScenarioKeys.ContainsKey($key)) { Fail "Duplicate scenario anchor generated: $key" }
    $ScenarioKeys[$key] = $true
    $catalogueAction = $ActionMap[$Action]
    $Scenarios.Add([ordered]@{
        action=$Action
        workflow=$Action
        level=$Level
        taskId=$TaskId
        type=$Type
        anchor=$Anchor
        requiredSteps=@($catalogueAction.requiredSteps)
        uiOracle=$catalogueAction.uiOracle
        stateOracle=$catalogueAction.stateOracle
    })
}

$GameRoot = Full $GameRoot
$cataloguePath = Join-Path $PSScriptRoot 'editor-workflow-catalogue.json'
RequireFile $cataloguePath
$catalogue = Get-Content -LiteralPath $cataloguePath -Raw | ConvertFrom-Json
$actionMap = @{}
foreach ($action in Values $catalogue.actions) { $actionMap[$action.name] = $action }
if ($actionMap.Count -eq 0) { Fail 'Workflow catalogue contains no actions.' }

$converter = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'assets\editor\tools\igi1conv\igi1conv.exe'
RequireFile $converter
$locationRoot = Join-Path $GameRoot 'MISSIONS\location0'
if (-not (Test-Path -LiteralPath $locationRoot -PathType Container)) { Fail "Location corpus is missing: $locationRoot" }
$levelDirectories = @(Get-ChildItem -LiteralPath $locationRoot -Directory | Where-Object { $_.Name -match '^level([0-9]+)$' } | Sort-Object { [int]$_.Name.Substring(5) })
if ($levelDirectories.Count -ne 14) { Fail "Expected 14 levels, found $($levelDirectories.Count)." }
$levelNumbers = @($levelDirectories | ForEach-Object { [int]$_.Name.Substring(5) })
if (($levelNumbers -join ',') -ne '1,2,3,4,5,6,7,8,9,10,11,12,13,14') { Fail "Expected levels 1 through 14, found '$($levelNumbers -join ',')'." }

$disposableRoot = Join-Path $env:TEMP ('igi-editor-workflow-catalogue-' + [guid]::NewGuid().ToString('N'))
$levels = New-Object System.Collections.Generic.List[object]
$scenarios = New-Object System.Collections.Generic.List[object]
$exclusions = New-Object System.Collections.Generic.List[object]
$scenarioKeys = @{}
try {
    New-Item -ItemType Directory -Path $disposableRoot -Force | Out-Null
    $modelExtractRoot = Join-Path $disposableRoot 'models'
    New-Item -ItemType Directory -Path $modelExtractRoot -Force | Out-Null
    $extractedModelPaths = @{}
    # Extract archives below the disposable root once.  This gives the
    # independent MEF parser access to authored ATTA records without ever
    # writing beside the installed corpus.  First occurrence wins, while all
    # archive paths remain represented in the level file inventory.
    $archivesToExtract = @(Get-ChildItem -LiteralPath $locationRoot -Directory |
        Where-Object { $_.Name -match '^level([0-9]+)$' } |
        ForEach-Object { Join-Path $_.FullName ("models\" + $_.Name + '.res') } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Sort-Object -Unique)
    foreach ($archive in $archivesToExtract) {
        $archiveName = [IO.Path]::GetFileNameWithoutExtension((Split-Path -Leaf $archive))
        $extractPath = Join-Path $modelExtractRoot $archiveName
        New-Item -ItemType Directory -Path $extractPath -Force | Out-Null
        Get-ConverterOutput $converter @('res', 'extract', $archive, '-o', $extractPath) | Out-Null
        foreach ($modelFile in @(Get-ChildItem -LiteralPath $extractPath -Recurse -Filter '*.mef' -File | Sort-Object Name)) {
            $key = $modelFile.BaseName.ToLowerInvariant()
            if (-not $extractedModelPaths.ContainsKey($key)) { $extractedModelPaths[$key] = $modelFile.FullName }
        }
    }
    $mefAttachmentCache = @{}
    foreach ($directory in $levelDirectories) {
        $level = [int]$directory.Name.Substring(5)
        $objectsQvm = Join-Path $directory.FullName 'objects.qvm'
        RequireFile $objectsQvm
        $decompiled = Join-Path $disposableRoot ("level$level-objects.qsc")
        Get-ConverterOutput $converter @('qvm', 'decompile', $objectsQvm, '-o', $decompiled) | Out-Null
        RequireFile $decompiled
        $source = Get-Content -LiteralPath $decompiled -Raw
        $fileInventory = Get-FileInventory $directory.FullName $GameRoot
        $declarations = Get-DeclarationMap $source
        $calls = @(Get-FunctionCalls $source 'Task_New')
        if ($calls.Count -eq 0) { Fail "No Task_New instances found in $objectsQvm." }

        $archiveCandidates = @(
            (Join-Path $directory.FullName ("models\level$level.res")),
            (Join-Path $locationRoot 'COMMON\models\location0.res')
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
        # A level may reference a model physically packed in another level's
        # archive (the editor auto-imports it into the level .res on save).
        # Scan every level archive plus the common archive so LOD discovery
        # and corpus resolution cover the full importable set.
        $allModelArchives = @(Get-ChildItem -LiteralPath $locationRoot -Directory | Where-Object { $_.Name -match '^level([0-9]+)$' } | ForEach-Object { Join-Path $_.FullName ("models\" + $_.Name + '.res') } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }) + $archiveCandidates
        $modelLods = Get-ModelLods $converter $allModelArchives
        $sourceHash = Get-Sha256 $objectsQvm
        $inventory = New-Object System.Collections.Generic.List[object]
        $taskIds = New-Object System.Collections.Generic.List[string]
        $taskCallData = @{}
        $knownTaskIds = @{}
        $anonymous = 0
        foreach ($call in $calls) {
            if ($call.arguments.Count -lt 3) { Fail "Malformed Task_New in level $level at character $($call.start)." }
            $rawTaskId = Unquote ([string]$call.arguments[0])
            if ($rawTaskId -eq '-1') { $anonymous++; $taskId = "-1#$anonymous" } else { $taskId = $rawTaskId }
            if ($knownTaskIds.ContainsKey($taskId)) { Fail "Duplicate Task_New ID '$taskId' in level $level." }
            $knownTaskIds[$taskId] = $true
            $taskIds.Add($taskId)
            $type = Unquote ([string]$call.arguments[1])
            $fields = @($declarations[$type])
            $taskCallData[$taskId] = [pscustomobject]@{ fields=$fields; arguments=@($call.arguments) }
            $positionField = @(Get-Field $fields '(?i)^(Position|Position start|Graph position|Start position|Holder Position)$')
            $rotationField = @(Get-Field $fields '(?i)^Orientation$')
            $position = if ($positionField.Count -eq 1) { Convert-NumberVector $call.arguments $positionField[0].offset $positionField[0].width } else { @() }
            $rotation = if ($rotationField.Count -eq 1) { Convert-NumberVector $call.arguments $rotationField[0].offset $rotationField[0].width } else { @() }
            $modelId = Get-FieldText $fields $call.arguments '(?i)^(Model|Holder Model|Waypoint Model)$'
            # Get-FieldValues unrolls a single-element result to a scalar
            # string; wrapping in @() keeps array indexing safe so the first
            # fallback value is the whole model id, never its first character.
            $models = @(Get-FieldValues $fields $call.arguments '(?i)model')
            if ([string]::IsNullOrWhiteSpace($modelId) -and $models.Count -gt 0) { $modelId = $models[0] }
            $textures = New-Object System.Collections.Generic.List[string]
            foreach ($value in @(Get-FieldValues $fields $call.arguments '(?i)(texture|bitmap|sprite)')) { $textures.Add($value) }
            foreach ($argument in $call.arguments) {
                $value = Unquote ([string]$argument)
                if ($value -match '(?i)\.(tex|tga|jpg|bmp)$') { $textures.Add($value) }
            }
            $sounds = @(Get-FieldValues $fields $call.arguments '(?i)sound')
            $lodKey = $modelId -replace '(?i)\.mef$', ''
            $lodBase = if ($lodKey -match '^(.*)_[0-9]+$') { $Matches[1] } else { $lodKey }
            $lods = if ($modelLods.ContainsKey($lodBase)) { @($modelLods[$lodBase]) } else { @() }
            $hasModel = -not [string]::IsNullOrWhiteSpace($modelId)
            $modelKey = ($modelId -replace '(?i)\.mef$', '').ToLowerInvariant()
            $attachments = @()
            if ($hasModel -and $extractedModelPaths.ContainsKey($modelKey)) {
                if (-not $mefAttachmentCache.ContainsKey($modelKey)) {
                    $mefAttachmentCache[$modelKey] = @(Read-MefAttachments $extractedModelPaths[$modelKey] $modelId)
                }
                $attachments = @($mefAttachmentCache[$modelKey])
            }
            # Helper models are authored collision/logic placeholders rather
            # than renderable meshes: waypoint (spline marker), colbox/collision
            # boxes, joint_fixer, and bare numeric spline indices.  They are
            # intentionally absent from the model archives and must not count
            # as corpus misses.  A real mesh reference resolves only when at
            # least one LOD file is discoverable in the importable archive set.
            $hasModel = -not [string]::IsNullOrWhiteSpace($modelId)
            $hasPosition = @($position).Count -eq 3
            $hasRotation = @($rotation).Count -ge 1
            $helperModel = $modelId -imatch '^(waypoint|colbox[0-9]*|joint_fixer[0-9]*|[0-9]+)$'
            $modelResolved = (-not $hasModel) -or $helperModel -or (@($lods).Count -gt 0)
            # Renderable = the editor can place AND view this instance as a
            # mesh.  Helper models (collision boxes, spline waypoints,
            # joint fixers) carry a position but no renderable geometry, so
            # they are placed but not orbit-able.
            $renderable = $hasModel -and $hasPosition -and -not $helperModel
            $inventory.Add([ordered]@{
                level=$level
                taskId=$taskId
                type=$type
                modelId=$modelId
                authoredPosition=@($position)
                authoredRotation=@($rotation)
                textures=@($textures | Sort-Object -Unique)
                lods=@($lods)
                modelVariants=@($lods | ForEach-Object { "$lodBase`_$_.mef" })
                attachments=$attachments
                attachmentCount=$attachments.Count
                aspectCoverage=[ordered]@{
                    position=($hasPosition)
                    orientation=($hasRotation)
                    model=$hasModel
                    lod=(@($lods).Count -gt 0)
                    attachments=($attachments.Count -gt 0)
                    textures=(@($textures).Count -gt 0)
                    sounds=(@($sounds).Count -gt 0)
                }
                sounds=@($sounds)
                sourceHash=$sourceHash
                renderable=$renderable
                hasModel=$hasModel
                hasPosition=$hasPosition
                hasRotation=$hasRotation
                helperModel=$helperModel
                modelResolved=$modelResolved
            })
        }

        $graphFiles = @(Get-ChildItem -LiteralPath (Join-Path $directory.FullName 'graphs') -Filter 'graph*.dat' -File -ErrorAction SilentlyContinue | ForEach-Object { Get-RelativePath $_.FullName $GameRoot } | Sort-Object)
        $aiFiles = @(Get-ChildItem -LiteralPath (Join-Path $directory.FullName 'ai') -Filter '*.qvm' -File -ErrorAction SilentlyContinue | ForEach-Object { Get-RelativePath $_.FullName $GameRoot } | Sort-Object)
        $lightmapFiles = @(Get-ChildItem -LiteralPath (Join-Path $directory.FullName 'lightmaps') -File -ErrorAction SilentlyContinue | ForEach-Object { Get-RelativePath $_.FullName $GameRoot } | Sort-Object)
        $animationTasks = @($inventory | Where-Object { $_.type -match '^(AnimTask|HumanAI)$' })
        $weatherTasks = @($inventory | Where-Object { $_.type -eq 'RainEffect' })

        foreach ($entry in $inventory) { Add-Scenario $scenarios $scenarioKeys $actionMap 'inspect-task' $level $entry.taskId $entry.type ([ordered]@{ sourceHash=$entry.sourceHash }) }
        foreach ($entry in @($inventory | Where-Object { -not [string]::IsNullOrWhiteSpace($_.modelId) })) { Add-Scenario $scenarios $scenarioKeys $actionMap 'model-picker' $level $entry.taskId $entry.type ([ordered]@{ modelId=$entry.modelId; lods=@($entry.lods) }) }
        foreach ($entry in @($inventory | Where-Object { @($_.textures).Count -gt 0 })) { Add-Scenario $scenarios $scenarioKeys $actionMap 'texture-resolution' $level $entry.taskId $entry.type ([ordered]@{ textures=@($entry.textures) }) }
        foreach ($entry in @($inventory | Where-Object { @($_.sounds).Count -gt 0 })) { Add-Scenario $scenarios $scenarioKeys $actionMap 'sound-resolution' $level $entry.taskId $entry.type ([ordered]@{ sounds=@($entry.sounds) }) }
        # ---- Per-instance object visual orbit coverage (Task 4) ----
        # Every renderable instance (model + 3-component position) gets one
        # orbit anchor carrying the ten deterministic views and its discovered
        # LODs so a failure can identify level/taskId/modelId/angle/lod.
        $renderableViews = @('front','back','left','right','top','bottom','front-left','front-right','back-left','back-right')
        foreach ($entry in @($inventory | Where-Object { $_.renderable })) {
            Add-Scenario $scenarios $scenarioKeys $actionMap 'object-visual-orbit' $level $entry.taskId $entry.type ([ordered]@{
                modelId=$entry.modelId
                authoredPosition=@($entry.authoredPosition)
                authoredRotation=@($entry.authoredRotation)
                lods=@($entry.lods)
                views=$renderableViews
            })
        }
        # The graph overlay opens only for a selected AIGraph task, whose
        # numeric taskId names its file graph<taskId>.dat.  Anchor each
        # graph-overlay scenario to the real AIGraph task so a runner can
        # select it, and carry the parsed node/edge population.
        $graphIdToFile = @{}
        foreach ($graphFile in $graphFiles) {
            $graphId = [IO.Path]::GetFileNameWithoutExtension($graphFile) -replace '^graph', ''
            $graphIdToFile[$graphId] = $graphFile
        }
        $graphTasks = @($inventory | Where-Object { $_.type -eq 'AIGraph' -and $graphIdToFile.ContainsKey([string]$_.taskId) })
        $matchedGraphIds = @{}
        foreach ($entry in $graphTasks) {
            $graphFile = $graphIdToFile[[string]$entry.taskId]
            $graphMetadata = Get-GraphMetadata $converter (Join-Path $GameRoot $graphFile) $disposableRoot
            $matchedGraphIds[[string]$entry.taskId] = $true
            Add-Scenario $scenarios $scenarioKeys $actionMap 'graph-overlay' $level $entry.taskId $entry.type ([ordered]@{
                graphFile=$graphFile
                nodeCount=$graphMetadata.nodeCount
                edgeCount=$graphMetadata.edgeCount
                graphTaskId=$entry.taskId
                nodes=$graphMetadata.nodes
                edges=$graphMetadata.edges
                criteriaClasses=$graphMetadata.criteriaClasses
                criteriaCounts=$graphMetadata.criteriaCounts
                criterionSamples=$graphMetadata.criterionSamples
                sampleNodeId=$graphMetadata.sampleNodeId
                sampleNodeCriteria=$graphMetadata.sampleNodeCriteria
                sampleNodeClass=$graphMetadata.sampleNodeClass
            })
        }
        # Discovered graph files with no matching AIGraph task are recorded as
        # orphan corpus files (nothing selects them) so they never silently
        # vanish from coverage accounting.
        $orphanGraphFiles = @($graphIdToFile.Keys | Where-Object { -not $matchedGraphIds.ContainsKey([string]$_) } | ForEach-Object { $graphIdToFile[[string]$_] } | Sort-Object)
        # Graph files with no matching AIGraph task stay discovered corpus
        # files but get no scenario (nothing selects them).  The generic
        # action-presence loop below records the exclusion when no AIGraph
        # task has a matching file.
        foreach ($entry in $animationTasks) {
            $callData = $taskCallData[[string]$entry.taskId]
            $animationIds = @()
            $animationSource = ''
            $graphTaskId = -1
            $patrol = [ordered]@{ status='not-applicable'; scriptPath=''; pathIds=@(); exclusionReason='' }
            if ($entry.type -eq 'HumanAI') {
                $graphTaskId = Get-FieldNumber $callData.fields $callData.arguments '^Graph ID$' -1
                $aiRelative = "MISSIONS\location0\level$level\ai\$($entry.taskId).qvm"
                $aiPath = Join-Path $GameRoot $aiRelative
                $animationSource = $aiRelative
                $patrol = [ordered]@{
                    status='none'; scriptPath=$aiRelative; pathIds=@(); exclusionReason='AI script contains no statically resolvable AIAction_Patrol path.'
                }
                if (Test-Path -LiteralPath $aiPath -PathType Leaf) {
                    $aiMetadata = Get-AiScriptMetadata $converter $aiPath $disposableRoot
                    $animationIds = @($aiMetadata.animationIds)
                    if ($aiMetadata.patrolPathId -ge 0) {
                        $patrol.status = 'authored'
                        $patrol.pathIds = @([int]$aiMetadata.patrolPathId)
                        $patrol.exclusionReason = ''
                    }
                } else {
                    $patrol.exclusionReason = 'Referenced HumanAI QVM is absent from the installed level corpus.'
                }
            } else {
                $animationSource = "objects.qsc:AnimData($($entry.taskId))"
                $animField = @(Get-Field $callData.fields '^AnimData$')
                if ($animField.Count -eq 1) {
                    # AnimData is an opaque authored blob. Preserve its literal
                    # integer records as evidence without claiming a schema the
                    # editor does not expose in the QSC declaration.
                    $animationIds = @($callData.arguments | Select-Object -Skip $animField[0].offset |
                        ForEach-Object { $number=0; if ([int]::TryParse((Unquote ([string]$_)), [Globalization.NumberStyles]::Integer, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) { $number } } |
                        Where-Object { $_ -ge -1 -and $_ -le 4096 } | Select-Object -Unique)
                }
            }
            Add-Scenario $scenarios $scenarioKeys $actionMap 'ai-animation' $level $entry.taskId $entry.type ([ordered]@{
                aiFiles=$aiFiles; animationClass=$entry.type; animationIds=$animationIds
                animationSource=$animationSource; graphTaskId=$graphTaskId; patrol=$patrol
            })
            if ($entry.type -eq 'HumanAI' -and $patrol.status -eq 'none') {
                $exclusions.Add([ordered]@{
                    action='ai-patrol'; level=$level; taskId=$entry.taskId
                    reason=$patrol.exclusionReason
                })
            }
        }
        foreach ($entry in $weatherTasks) { Add-Scenario $scenarios $scenarioKeys $actionMap 'weather-classification' $level $entry.taskId $entry.type ([ordered]@{ weather='RainEffect' }) }
        if ($lightmapFiles.Count -gt 0) { Add-Scenario $scenarios $scenarioKeys $actionMap 'lightmap-inspection' $level $inventory[0].taskId $inventory[0].type ([ordered]@{ lightmaps=$lightmapFiles }) }

        # ---- Per-level editor control workflows (Task 3) ----
        # These anchor to the editor itself rather than to a task/model; each
        # control applies to every level unless the catalogue applicability or
        # the discovered corpus says otherwise.  Controls that cannot run on a
        # level are recorded as explicit exclusions below, never silently
        # skipped.
        $terrainDirectory = Join-Path $directory.FullName 'terrain'
        $hasTerrain = Test-Path -LiteralPath $terrainDirectory -PathType Container
        $controlAnchor = 'editor-control'
        $alwaysControls = @(
            'editor-pause-resume', 'editor-task-tree', 'editor-terrain-shortcut',
            'editor-font-size', 'editor-autosave', 'editor-logging',
            'editor-music', 'editor-collision-clip', 'editor-save',
            'editor-reset', 'editor-graceful-quit', 'editor-cursor-state'
        )
        foreach ($control in $alwaysControls) {
            Add-Scenario $scenarios $scenarioKeys $actionMap $control $level $controlAnchor 'EditorControl' ([ordered]@{ control=$control })
        }
        if ($lightmapFiles.Count -gt 0) {
            Add-Scenario $scenarios $scenarioKeys $actionMap 'editor-lightmap-mode' $level $controlAnchor 'EditorControl' ([ordered]@{ control='editor-lightmap-mode'; lightmaps=$lightmapFiles })
        }
        if ($hasTerrain) {
            Add-Scenario $scenarios $scenarioKeys $actionMap 'editor-terrain-fog' $level $controlAnchor 'EditorControl' ([ordered]@{ control='editor-terrain-fog'; terrain='present' })
        }
        if ($level -lt 14) {
            Add-Scenario $scenarios $scenarioKeys $actionMap 'editor-level-change' $level $controlAnchor 'EditorControl' ([ordered]@{ control='editor-level-change'; nextLevel=$level + 1 })
        }

        $actionPresence = @{}
        foreach ($scenario in @($scenarios | Where-Object { $_.level -eq $level })) { $actionPresence[$scenario.action] = $true }
        foreach ($actionName in @($actionMap.Keys | Sort-Object)) {
            if (-not $actionPresence.ContainsKey($actionName)) {
                $reason = switch ($actionName) {
                    'model-picker' { 'No Task_New instance has an authored model reference.' }
                    'texture-resolution' { 'No Task_New instance has an authored texture reference.' }
                    'sound-resolution' { 'No Task_New instance has an authored sound reference.' }
                    'graph-overlay' { 'No AIGraph task has a matching graph<taskId>.dat file.' }
                    'ai-animation' { 'No AnimTask or HumanAI Task_New instance was discovered.' }
                    'weather-classification' { 'No RainEffect Task_New instance was discovered.' }
                    'lightmap-inspection' { 'No lightmap files were discovered.' }
                    'editor-lightmap-mode' { 'No lightmap archive was discovered in this level.' }
                    'editor-terrain-fog' { 'No terrain directory was discovered in this level.' }
                    'editor-level-change' { 'No higher level exists to switch to (level 14 is the last).' }
                    'object-visual-orbit' { 'No renderable Task_New instance (authored model + 3-component position) was discovered.' }
                    default { 'No applicable corpus anchor was discovered.' }
                }
                $exclusions.Add([ordered]@{ action=$actionName; level=$level; reason=$reason })
            }
        }
        $levels.Add([ordered]@{
            level=$level
            sourcePath=(Get-RelativePath $objectsQvm $GameRoot)
            sourceHash=$sourceHash
            files=$fileInventory
            taskIds=$taskIds.ToArray()
            declarations=@($declarations.Keys | Sort-Object)
            inventory=$inventory.ToArray()
            discovery=[ordered]@{
                graphs=$graphFiles
                orphanGraphFiles=$orphanGraphFiles
                ai=$aiFiles
                animationTaskIds=@($animationTasks | ForEach-Object { $_.taskId })
                weatherTaskIds=@($weatherTasks | ForEach-Object { $_.taskId })
                lightmaps=$lightmapFiles
                unresolvedModels=@($inventory | Where-Object { $_.hasModel -and -not $_.helperModel -and -not $_.modelResolved } | ForEach-Object { $_.modelId } | Sort-Object -Unique)
            }
        })
    }

    $inventoryHash = Get-Sha256Text ((@($levels | ForEach-Object { $_.inventory }) | ConvertTo-Json -Depth 100 -Compress))
    $manifest = [ordered]@{
        schemaVersion=1
        generatedBy='New-EditorWorkflowManifest.ps1'
        generatedFrom=$GameRoot
        catalogueHash=(Get-Sha256 $cataloguePath)
        inventoryHash=$inventoryHash
        levels=$levels.ToArray()
        scenarios=$scenarios.ToArray()
        exclusions=$exclusions.ToArray()
    }
    $OutputPath = Full $OutputPath
    $parent = Split-Path -Parent $OutputPath
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    Write-Host "Generated editor workflow manifest with $($levels.Count) levels, $($scenarios.Count) scenarios, and $($exclusions.Count) exclusions at $OutputPath"
} finally {
    Remove-Item -LiteralPath $disposableRoot -Recurse -Force -ErrorAction SilentlyContinue
}
