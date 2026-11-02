$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw [System.InvalidOperationException]::new($Message) }
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { Fail $Message }
}
function Values($Value) {
    if ($null -eq $Value) { return @() }
    return @($Value)
}
function Get-IndependentTaskIds([string]$Source) {
    $taskIds = New-Object System.Collections.Generic.List[string]
    $anonymous = 0
    foreach ($match in [regex]::Matches($Source, '\bTask_New\s*\(\s*(-?[0-9]+|"(?:\\.|[^"])*")')) {
        $rawTaskId = $match.Groups[1].Value
        if ($rawTaskId.StartsWith('"') -and $rawTaskId.EndsWith('"')) {
            $rawTaskId = $rawTaskId.Substring(1, $rawTaskId.Length - 2).Replace('\"', '"').Replace('\\', '\')
        }
        if ($rawTaskId -eq '-1') {
            $anonymous++
            $taskIds.Add("-1#$anonymous")
        } else {
            $taskIds.Add($rawTaskId)
        }
    }
    return $taskIds.ToArray()
}

function New-RunnerFixtureFile([string]$Path, $Manifest) {
    $Manifest | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Invoke-RunnerValidate([string]$ManifestPath) {
    # Run the child out of process so an intentional validation rejection cannot
    # be promoted by this script's $ErrorActionPreference; the exit code and
    # rendered message stay authoritative.
    $lines = @(& pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ValidateOnly -ScenarioPath $ManifestPath 2>&1)
    return [pscustomobject]@{ ExitCode = [int]$LASTEXITCODE; Output = ($lines | Out-String) }
}
function Assert-OutputContains([string]$Text, [string]$Keyword, [string]$CaseLabel) {
    Require ($Text.IndexOf($Keyword, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) "$CaseLabel output did not mention '$Keyword': $Text"
}

$gameRoot = 'D:\IGI1'
$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
$cataloguePath = Join-Path $PSScriptRoot 'editor-workflow-catalogue.json'
$generatorPath = Join-Path $PSScriptRoot 'New-EditorWorkflowManifest.ps1'
$converterPath = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'assets\editor\tools\igi1conv\igi1conv.exe'
$manifestPath = Join-Path $env:TEMP ('igi-editor-workflow-manifest-' + [guid]::NewGuid().ToString('N') + '.json')
$decompileRoot = Join-Path $env:TEMP ('igi-editor-workflow-contract-' + [guid]::NewGuid().ToString('N'))
$runnerFixtureRoot = Join-Path $env:TEMP ('igi-editor-runner-contract-' + [guid]::NewGuid().ToString('N'))
$locationPushed = $false

try {
    if (-not (Test-Path -LiteralPath $cataloguePath -PathType Leaf)) {
        Fail "Editor workflow catalogue is missing: $cataloguePath"
    }
    if (-not (Test-Path -LiteralPath $generatorPath -PathType Leaf)) {
        Fail "Editor workflow manifest generator is missing: $generatorPath"
    }
    if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
        Fail "Editor E2E runner is missing: $runner"
    }
    if (-not (Test-Path -LiteralPath $converterPath -PathType Leaf)) {
        Fail "Installed converter is missing: $converterPath"
    }
    if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
        Fail "Game root is missing: $gameRoot"
    }
    New-Item -ItemType Directory -Path $runnerFixtureRoot -Force | Out-Null

    Push-Location -LiteralPath $gameRoot
    $locationPushed = $true
    $childWorkingDirectory = [string](& pwsh -NoProfile -Command '(Get-Location).ProviderPath')
    Require ($childWorkingDirectory.TrimEnd('\') -ceq $gameRoot.TrimEnd('\')) "Generator child working directory must be $gameRoot, got $childWorkingDirectory."
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $generatorPath -GameRoot $gameRoot -OutputPath $manifestPath
    Require ($LASTEXITCODE -eq 0) "Editor workflow manifest generator failed with exit code $LASTEXITCODE."
    Require (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Generator did not write $manifestPath."

    $catalogue = Get-Content -LiteralPath $cataloguePath -Raw | ConvertFrom-Json
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $catalogueEntries = Values $catalogue.actions
    Require ($catalogueEntries.Count -gt 0) 'Catalogue must declare at least one action.'
    foreach ($entry in $catalogueEntries) {
        foreach ($field in @('name', 'action', 'applicability', 'requiredSteps', 'uiOracle', 'stateOracle')) {
            Require ($null -ne $entry.$field -and -not [string]::IsNullOrWhiteSpace([string]$entry.$field)) "Catalogue action '$($entry.name)' is missing $field."
        }
    }

    $levels = Values $manifest.levels
    Require ($levels.Count -eq 14) "Expected 14 levels, got $($levels.Count)."
    $numbers = @($levels | ForEach-Object { [int]$_.level } | Sort-Object)
    Require (($numbers -join ',') -eq '1,2,3,4,5,6,7,8,9,10,11,12,13,14') "Manifest levels were '$($numbers -join ',')'."
    Require ([string]$manifest.inventoryHash -cmatch '^[a-f0-9]{64}$') 'Manifest must contain a lowercase 64-character SHA-256 inventory hash.'

    $scenarios = Values $manifest.scenarios
    $exclusions = Values $manifest.exclusions
    foreach ($action in $catalogueEntries) {
        $applicable = @($scenarios | Where-Object { $_.action -eq $action.name })
        $excluded = @($exclusions | Where-Object { $_.action -eq $action.name -and -not [string]::IsNullOrWhiteSpace([string]$_.reason) })
        Require (($applicable.Count + $excluded.Count) -gt 0) "Catalogue action '$($action.name)' has no applicable scenario or explicit exclusion."
    }

    $anchors = @{}
    foreach ($scenario in $scenarios) {
        foreach ($field in @('level', 'taskId', 'workflow')) {
            Require ($null -ne $scenario.$field -and -not [string]::IsNullOrWhiteSpace([string]$scenario.$field)) "Scenario is missing $field."
        }
        $key = "$($scenario.level)|$($scenario.taskId)|$($scenario.workflow)"
        Require (-not $anchors.ContainsKey($key)) "Duplicate scenario anchor: $key"
        $anchors[$key] = $true
    }

    foreach ($level in $levels) {
        foreach ($action in $catalogueEntries) {
            $applicable = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq $action.name })
            $excluded = @($exclusions | Where-Object { $_.level -eq $level.level -and $_.action -eq $action.name })
            Require (($applicable.Count + $excluded.Count) -gt 0) "Level $($level.level) action '$($action.name)' has no scenario or explicit exclusion."
            Require ($excluded.Count -le 1) "Level $($level.level) action '$($action.name)' has duplicate exclusions."
            foreach ($exclusion in $excluded) {
                Require (-not [string]::IsNullOrWhiteSpace([string]$exclusion.reason)) "Level $($level.level) action '$($action.name)' has an exclusion without a reason."
            }
        }
        $sourcePath = [string]$level.sourcePath
        Require ($sourcePath -notmatch '\\\\') "Level $($level.level) source path contains doubled separators: $sourcePath"
        $objectsQvm = Join-Path $gameRoot $sourcePath
        Require (Test-Path -LiteralPath $objectsQvm -PathType Leaf) "Level $($level.level) source path is missing: $objectsQvm"
        $expectedSourceHash = (Get-FileHash -LiteralPath $objectsQvm -Algorithm SHA256).Hash.ToLowerInvariant()
        Require ([string]$level.sourceHash -cmatch '^[a-f0-9]{64}$') "Level $($level.level) sourceHash is not a lowercase 64-character SHA-256 value."
        Require ([string]$level.sourceHash -ceq $expectedSourceHash) "Level $($level.level) sourceHash does not match $objectsQvm."

        if (-not (Test-Path -LiteralPath $decompileRoot -PathType Container)) { New-Item -ItemType Directory -Path $decompileRoot -Force | Out-Null }
        $decompiled = Join-Path $decompileRoot ("level$($level.level)-objects.qsc")
        & $converterPath qvm decompile $objectsQvm -o $decompiled
        Require ($LASTEXITCODE -eq 0) "Independent objects.qvm decompile failed for level $($level.level) with exit code $LASTEXITCODE."
        Require (Test-Path -LiteralPath $decompiled -PathType Leaf) "Independent decompile did not write $decompiled."
        $independentTaskIds = @(Get-IndependentTaskIds (Get-Content -LiteralPath $decompiled -Raw))
        Require ($independentTaskIds.Count -gt 0) "Independent decompile found no Task_New instances for level $($level.level)."

        $taskIds = @(Values $level.taskIds | ForEach-Object { [string]$_ })
        $inventory = Values $level.inventory
        $inventoryTaskIds = @($inventory | ForEach-Object { [string]$_.taskId })
        Require (($inventoryTaskIds | Sort-Object -Unique).Count -eq $inventoryTaskIds.Count) "Level $($level.level) inventory contains duplicate task IDs."
        Require (($taskIds | Sort-Object -Unique).Count -eq $taskIds.Count) "Level $($level.level) declared task IDs contain duplicates."
        Require (($independentTaskIds | Sort-Object -Unique).Count -eq $independentTaskIds.Count) "Independent decompile found duplicate task IDs for level $($level.level)."
        Require ((Compare-Object -ReferenceObject ($independentTaskIds | Sort-Object) -DifferenceObject ($taskIds | Sort-Object)).Count -eq 0) "Level $($level.level) declared task IDs do not match independently decompiled Task_New instances."
        Require ((Compare-Object -ReferenceObject ($independentTaskIds | Sort-Object) -DifferenceObject ($inventoryTaskIds | Sort-Object)).Count -eq 0) "Level $($level.level) inventory task IDs are not exhaustive against independently decompiled Task_New instances."
        foreach ($entry in $inventory) {
            foreach ($field in @('level', 'taskId', 'type', 'modelId', 'authoredPosition', 'authoredRotation', 'textures', 'lods', 'sounds', 'sourceHash', 'renderable', 'hasModel', 'hasPosition', 'hasRotation', 'helperModel', 'modelResolved')) {
                Require ($null -ne $entry.$field) "Level $($level.level) task $($entry.taskId) is missing $field."
            }
            Require ([string]$entry.sourceHash -cmatch '^[a-f0-9]{64}$') "Level $($level.level) task $($entry.taskId) sourceHash is not a lowercase 64-character SHA-256 value."
            Require ([string]$entry.sourceHash -ceq $expectedSourceHash) "Level $($level.level) task $($entry.taskId) sourceHash does not match $objectsQvm."
        }
        # Every AIGraph task whose numeric id matches a discovered graph<id>.dat
        # file must have exactly one graph-overlay scenario carrying node/edge
        # counts.  A discovered graph file with no matching AIGraph task is an
        # orphan corpus finding recorded in discovery.orphanGraphFiles, not a
        # silent gap.
        $graphIdToFile = @{}
        foreach ($graphFile in Values $level.discovery.graphs) {
            $graphId = [IO.Path]::GetFileNameWithoutExtension($graphFile) -replace '^graph', ''
            $graphIdToFile[[string]$graphId] = [string]$graphFile
        }
        $matchedGraphFiles = @{}
        foreach ($graphScenario in @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq 'graph-overlay' })) {
            $graphAnchor = $graphScenario.anchor
            Require ([int]$graphAnchor.nodeCount -ge 1) "Level $($level.level) graph '$($graphAnchor.graphFile)' anchor must carry nodeCount >= 1."
            Require ($null -ne $graphAnchor.edgeCount) "Level $($level.level) graph '$($graphAnchor.graphFile)' anchor must carry edgeCount."
            Require ($graphAnchor.graphTaskId -eq [string]$graphScenario.taskId) "Level $($level.level) graph-overlay anchor graphTaskId must equal the AIGraph taskId."
            Require (@($graphAnchor.nodes).Count -eq [int]$graphAnchor.nodeCount) "Level $($level.level) graph '$($graphAnchor.graphFile)' must carry one metadata record per node."
            Require (@($graphAnchor.edges).Count -eq [int]$graphAnchor.edgeCount) "Level $($level.level) graph '$($graphAnchor.graphFile)' must carry one metadata record per edge."
            Require (@($graphAnchor.criteriaClasses).Count -gt 0) "Level $($level.level) graph '$($graphAnchor.graphFile)' must carry criterion classes."
            Require ($null -ne $graphAnchor.criteriaCounts -and $null -ne $graphAnchor.criterionSamples) "Level $($level.level) graph '$($graphAnchor.graphFile)' must carry criterion counts and samples."
            $matchedGraphFiles[$graphAnchor.graphFile] = $true
        }
        foreach ($entry in @(Values $level.inventory | Where-Object { $_.type -eq 'AIGraph' })) {
            if ($graphIdToFile.ContainsKey([string]$entry.taskId)) {
                $graphFile = $graphIdToFile[[string]$entry.taskId]
                $matching = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq 'graph-overlay' -and $_.taskId -eq $entry.taskId -and $_.anchor.graphFile -eq $graphFile })
                Require ($matching.Count -eq 1) "Level $($level.level) AIGraph task $($entry.taskId) with file '$graphFile' must have exactly one graph-overlay scenario; found $($matching.Count)."
            }
        }
        $orphanFiles = @(Values $level.discovery.orphanGraphFiles | ForEach-Object { [string]$_ })
        $expectedOrphan = @($graphIdToFile.Keys | Where-Object { -not $matchedGraphFiles.ContainsKey([string]$graphIdToFile[[string]$_]) } | ForEach-Object { [string]$graphIdToFile[[string]$_] } | Sort-Object)
        $orphanDelta = Compare-Object -ReferenceObject $expectedOrphan -DifferenceObject $orphanFiles
        Require ($null -eq $orphanDelta -or @($orphanDelta).Count -eq 0) "Level $($level.level) discovery.orphanGraphFiles does not match the graph files without an AIGraph task."
    }

    # ---- Editor control/persistence coverage contract (Task 3) ----
    # Controls that exist on the editor for every level must have a generated
    # scenario on every one of the 14 levels, never a silent exclusion.  The
    # conditional controls (lightmap mode, terrain fog, level change) are
    # allowed an explicit corpus exclusion only where the corpus lacks the
    # feature.
    $alwaysApplicableControls = @(
        'editor-pause-resume', 'editor-task-tree', 'editor-terrain-shortcut',
        'editor-font-size', 'editor-autosave', 'editor-logging',
        'editor-music', 'editor-collision-clip', 'editor-save',
        'editor-reset', 'editor-graceful-quit', 'editor-cursor-state'
    )
    foreach ($control in $alwaysApplicableControls) {
        foreach ($level in $levels) {
            $controlScenarios = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq $control })
            Require ($controlScenarios.Count -eq 1) "Level $($level.level) control '$control' must have exactly one generated scenario; found $($controlScenarios.Count)."
        }
    }
    $conditionalControls = @('editor-lightmap-mode', 'editor-terrain-fog', 'editor-level-change')
    foreach ($control in $conditionalControls) {
        $total = @($scenarios | Where-Object { $_.action -eq $control }).Count
        $excluded = @($exclusions | Where-Object { $_.action -eq $control }).Count
        Require (($total + $excluded) -gt 0) "Conditional control '$control' has no scenario or exclusion anywhere."
    }
    $controlAnchors = @($scenarios | Where-Object { $_.taskId -eq 'editor-control' })
    foreach ($scenario in $controlAnchors) {
        foreach ($field in @('action', 'workflow', 'level', 'taskId', 'type', 'anchor')) {
            Require ($null -ne $scenario.$field) "Control scenario on level $($scenario.level) is missing $field."
        }
        Require ($scenario.workflow -eq $scenario.action) "Control scenario '$($scenario.action)' workflow must equal its action."
        Require ($scenario.type -eq 'EditorControl') "Control scenario '$($scenario.action)' type must be EditorControl."
    }

    # ---- Object visual orbit coverage contract (Task 4) ----
    # Every renderable instance must be internally consistent (renderable iff
    # it has an authored model and a 3-component position; orientation is
    # recorded but not a gate because soldiers/cameras carry yaw-only Angle
    # fields) and must carry exactly one object-visual-orbit scenario whose
    # anchor records model/position/rotation/LODs and the ten deterministic
    # views.  The orbit-required inventory fields stay stable so a task can
    # never silently lose its visual anchor.
    $orbitAnchorsByKey = @{}
    foreach ($scenario in @($scenarios | Where-Object { $_.action -eq 'object-visual-orbit' })) {
        $orbitAnchorsByKey["$($scenario.level)|$($scenario.taskId)"] = $scenario
    }
    foreach ($level in $levels) {
        foreach ($entry in @(Values $level.inventory)) {
            $hasModel = -not [string]::IsNullOrWhiteSpace([string]$entry.modelId)
            $posCount = @($entry.authoredPosition).Count
            $rotCount = @($entry.authoredRotation).Count
            $helperModel = [bool]$entry.helperModel
            $expectedRenderable = ($hasModel -and $posCount -eq 3 -and -not $helperModel)
            $declaredRenderable = [bool]$entry.renderable
            Require ($declaredRenderable -eq $expectedRenderable) "Level $($level.level) task $($entry.taskId) renderable flag $declaredRenderable disagrees with fields (model=$hasModel pos=$posCount rot=$rotCount)."
            Require ($null -ne $entry.hasModel -and $null -ne $entry.hasPosition -and $null -ne $entry.hasRotation) "Level $($level.level) task $($entry.taskId) is missing renderable classification fields."
            if ($declaredRenderable) {
                $orbitScenario = $orbitAnchorsByKey["$($level.level)|$($entry.taskId)"]
                Require ($null -ne $orbitScenario) "Level $($level.level) renderable task $($entry.taskId) must have exactly one object-visual-orbit scenario; found 0."
                $anchor = $orbitScenario.anchor
                Require ([string]$anchor.modelId -eq [string]$entry.modelId) "Level $($level.level) task $($entry.taskId) orbit anchor modelId does not match inventory."
                Require (@($anchor.views).Count -eq 10) "Level $($level.level) task $($entry.taskId) orbit anchor must carry ten views."
                foreach ($view in @('front','back','left','right','top','bottom','front-left','front-right','back-left','back-right')) {
                    Require (@($anchor.views) -contains $view) "Level $($level.level) task $($entry.taskId) orbit anchor is missing view '$view'."
                }
                $positionDelta = Compare-Object -ReferenceObject @($entry.authoredPosition) -DifferenceObject @($anchor.authoredPosition)
                Require ($null -eq $positionDelta -or @($positionDelta).Count -eq 0) "Level $($level.level) task $($entry.taskId) orbit anchor position differs from inventory."
                $lodDelta = Compare-Object -ReferenceObject @($entry.lods) -DifferenceObject @($anchor.lods)
                Require ($null -eq $lodDelta -or @($lodDelta).Count -eq 0) "Level $($level.level) task $($entry.taskId) orbit anchor LODs differ from inventory."
            }
        }
    }
    $renderableCount = @($levels | ForEach-Object { @(Values $_.inventory | Where-Object { $_.renderable }) }).Count
    Require ($orbitAnchorsByKey.Count -eq $renderableCount) "Orbit anchor count $($orbitAnchorsByKey.Count) must equal renderable inventory count $renderableCount."

    # ---- Asset corpus resolution contract (Task 5) ----
    # Every model reference must be classified: a real mesh resolves to at
    # least one discoverable LOD in the importable archive set, a helper model
    # (waypoint/colbox/joint_fixer/numeric spline index) is an intentional
    # non-mesh placeholder, and anything else is a genuine corpus finding that
    # must be recorded in discovery.unresolvedModels so it can never silently
    # vanish.  Textures and sounds are recorded per entry for the resolver
    # workflows.
    foreach ($level in $levels) {
        $unresolved = @(Values $level.discovery.unresolvedModels | ForEach-Object { [string]$_ })
        $expectedUnresolved = @($level.inventory | Where-Object { $_.hasModel -and -not $_.helperModel -and -not $_.modelResolved } | ForEach-Object { [string]$_.modelId } | Sort-Object -Unique)
        $unresolvedDelta = Compare-Object -ReferenceObject $expectedUnresolved -DifferenceObject $unresolved
        Require ($null -eq $unresolvedDelta -or @($unresolvedDelta).Count -eq 0) "Level $($level.level) discovery.unresolvedModels does not match the unresolved inventory records."
        foreach ($entry in @(Values $level.inventory)) {
            $hasModel = -not [string]::IsNullOrWhiteSpace([string]$entry.modelId)
            if ($hasModel) {
                $model = [string]$entry.modelId
                $helper = [bool]$entry.helperModel
                $resolved = [bool]$entry.modelResolved
                $isSentinel = $model -imatch '^(waypoint|colbox[0-9]*|joint_fixer[0-9]*|[0-9]+)$'
                Require ($helper -eq $isSentinel) "Level $($level.level) task $($entry.taskId) helperModel flag $helper disagrees with model '$model'."
                if ($helper) {
                    Require ($resolved) "Level $($level.level) task $($entry.taskId) helper model '$model' must be modelResolved."
                } elseif (-not $resolved) {
                    Require (@($unresolved) -contains $model) "Level $($level.level) unresolved model '$model' (task $($entry.taskId)) is missing from discovery.unresolvedModels."
                }
            } else {
                Require (-not [bool]$entry.helperModel) "Level $($level.level) task $($entry.taskId) has no model but helperModel is true."
            }
        }
    }

    # ---- Graph/AI/animation coverage contract (Task 6) ----
    # Every graph-overlay anchor must name its real graph/task, node population,
    # and criterion data so a scenario can select a discovered node. Every
    # animation anchor must record its source, patrol classification, and the
    # graph target used by the live workflow. Graph/AI features absent from a
    # level must be explicit exclusions, never silent gaps.
    $task6ScenarioPath = Join-Path $PSScriptRoot 'scenarios\graph-ai-animation-workflows.json'
    Require (Test-Path -LiteralPath $task6ScenarioPath -PathType Leaf) "Task 6 scenario file is missing: $task6ScenarioPath"
    $task6Scenarios = Values (Get-Content -LiteralPath $task6ScenarioPath -Raw | ConvertFrom-Json).scenarios
    Require ($task6Scenarios.Count -gt 0) 'Task 6 scenario file must contain scenarios.'
    $task6Keys = @{}
    foreach ($scenario in $task6Scenarios) {
        foreach ($field in @('name','action','workflow','level','taskId','type','anchor','requiredSteps','uiOracle','stateOracle','steps')) {
            Require ($null -ne $scenario.$field) "Task 6 scenario is missing $field."
        }
        Require ($scenario.action -in @('graph-overlay','ai-animation')) "Task 6 scenario '$($scenario.name)' has unsupported action '$($scenario.action)'."
        $key = "$($scenario.level)|$($scenario.taskId)|$($scenario.action)"
        Require (-not $task6Keys.ContainsKey($key)) "Duplicate Task 6 scenario anchor: $key"
        $task6Keys[$key] = $true
        Require (@($scenario.steps).Count -gt 0) "Task 6 scenario '$($scenario.name)' must have runnable steps."
        $hasUi = @($scenario.steps | Where-Object { $_.type -in @('screenshot','assert_screenshot_region','assert_screenshot_color_ratio','assert_screenshot_difference') }).Count -gt 0
        $hasState = @($scenario.steps | Where-Object { $_.type -in @('assert_process','capture_window_state','wait_for_log','assert_log','assert_log_count','assert_file','assert_path','assert_file_hash','snapshot_paths','restore_paths','assert_graph_edit') }).Count -gt 0
        Require $hasUi "Task 6 scenario '$($scenario.name)' has no UI oracle step."
        Require $hasState "Task 6 scenario '$($scenario.name)' has no state oracle step."
        $stepTypes = @($scenario.steps | ForEach-Object { [string]$_.type })
        if ($scenario.action -eq 'graph-overlay') {
            Require ([bool]$scenario.requiresMutation) "Graph scenario '$($scenario.name)' must declare requiresMutation=true."
            Require (-not [string]::IsNullOrWhiteSpace([string]$scenario.anchor.graphFile)) "Graph scenario '$($scenario.name)' must carry graphFile."
            Require ([int]$scenario.anchor.nodeCount -ge 1) "Graph scenario '$($scenario.name)' must carry nodeCount."
            Require (@($scenario.anchor.nodes).Count -eq [int]$scenario.anchor.nodeCount) "Graph scenario '$($scenario.name)' must carry all node metadata."
            Require (@($scenario.anchor.edges).Count -eq [int]$scenario.anchor.edgeCount) "Graph scenario '$($scenario.name)' must carry all edge metadata."
            Require (@($scenario.anchor.criteriaClasses).Count -gt 0) "Graph scenario '$($scenario.name)' must carry criterion classes."
            Require ($stepTypes -contains 'select_graph_node') "Graph scenario '$($scenario.name)' must select a graph node."
            Require ($stepTypes -contains 'nudge_graph_node') "Graph scenario '$($scenario.name)' must perform a declared graph node edit."
            Require ($stepTypes -contains 'assert_graph_edit') "Graph scenario '$($scenario.name)' must compare graph data outside the declared edit."
            Require ($stepTypes -contains 'snapshot_paths') "Graph scenario '$($scenario.name)' must snapshot its graph file."
            Require ($stepTypes -contains 'restore_paths') "Graph scenario '$($scenario.name)' must restore its graph file."
        } else {
            Require (-not [bool]$scenario.requiresMutation) "Animation scenario '$($scenario.name)' must not mutate game data."
            Require ($null -ne $scenario.anchor.animationIds) "Animation scenario '$($scenario.name)' must carry animationIds."
            Require (-not [string]::IsNullOrWhiteSpace([string]$scenario.anchor.animationSource)) "Animation scenario '$($scenario.name)' must carry animationSource."
            Require ($null -ne $scenario.anchor.patrol) "Animation scenario '$($scenario.name)' must carry patrol classification."
            Require ($stepTypes -contains 'start_animation') "Animation scenario '$($scenario.name)' must start playback."
            Require ($stepTypes -contains 'pause_animation') "Animation scenario '$($scenario.name)' must pause playback."
            Require ($stepTypes -contains 'assert_screenshot_difference') "Animation scenario '$($scenario.name)' must compare playing frames."
            Require (@($scenario.steps | Where-Object { $_.type -eq 'assert_screenshot_difference' -and $_.maxChangedRatio -eq 0 }).Count -gt 0) "Animation scenario '$($scenario.name)' must assert stable paused frames."
        }
    }
    foreach ($level in $levels) {
        $levelScenarios = @($scenarios | Where-Object { $_.level -eq $level.level })
        $levelTask6Scenarios = @($task6Scenarios | Where-Object { $_.level -eq $level.level })
        $levelGraphFiles = @(Values $level.discovery.graphs | ForEach-Object { [string]$_ })
        $levelGraphTaskScenarios = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq 'graph-overlay' })
        $levelGraphTaskKeys = @{}
        foreach ($scenario in $levelGraphTaskScenarios) {
            $anchor = $scenario.anchor
            Require (-not [string]::IsNullOrWhiteSpace([string]$anchor.graphFile)) "Level $($level.level) graph scenario '$($scenario.taskId)' is missing graphFile."
            Require ($levelGraphFiles -contains [string]$anchor.graphFile) "Level $($level.level) graph scenario '$($scenario.taskId)' names an undiscovered graph file '$($anchor.graphFile)'."
            Require ([int]$anchor.nodeCount -ge 1) "Level $($level.level) graph '$($anchor.graphFile)' must carry nodeCount >= 1."
            Require ([int]$anchor.edgeCount -ge 0) "Level $($level.level) graph '$($anchor.graphFile)' must carry edgeCount >= 0."
            Require ($null -ne $anchor.graphTaskId -and [string]$anchor.graphTaskId -eq [string]$scenario.taskId) "Level $($level.level) graph '$($anchor.graphFile)' graphTaskId must equal its AIGraph taskId."
            Require (@($anchor.nodes).Count -eq [int]$anchor.nodeCount) "Level $($level.level) graph '$($anchor.graphFile)' must carry one real metadata record per graph node."
            Require (@($anchor.edges).Count -eq [int]$anchor.edgeCount) "Level $($level.level) graph '$($anchor.graphFile)' must carry one real metadata record per graph edge."
            Require (@($anchor.criteriaClasses).Count -gt 0) "Level $($level.level) graph '$($anchor.graphFile)' must carry node criterion classes."
            Require ($null -ne $anchor.criteriaCounts) "Level $($level.level) graph '$($anchor.graphFile)' must carry node criterion counts."
            foreach ($criterionClass in @($anchor.criteriaClasses)) {
                $criterionSample = @($anchor.criterionSamples | Where-Object { $_.class -eq $criterionClass })
                Require ($criterionSample.Count -eq 1) "Level $($level.level) graph '$($anchor.graphFile)' criterion class '$criterionClass' must carry exactly one discovered node sample."
                Require (@($anchor.nodes | Where-Object { [string]$_.id -eq [string]$criterionSample[0].nodeId -and $_.class -eq $criterionClass }).Count -eq 1) "Level $($level.level) graph '$($anchor.graphFile)' criterion sample '$criterionClass' must name a real node metadata record."
            }
            Require ($null -ne $anchor.sampleNodeId) "Level $($level.level) graph '$($anchor.graphFile)' must carry a sampleNodeId."
            Require ($null -ne $anchor.sampleNodeCriteria) "Level $($level.level) graph '$($anchor.graphFile)' must carry sampleNodeCriteria."
            Require ($null -ne $anchor.sampleNodeClass) "Level $($level.level) graph '$($anchor.graphFile)' must carry sampleNodeClass."
            $levelGraphTaskKeys[[string]$anchor.graphFile] = $true
        }
        foreach ($graphFile in $levelGraphFiles) {
            $graphTask = @($level.inventory | Where-Object { $_.type -eq 'AIGraph' -and [string]$graphFile -match ('\\graphs\\graph' + [regex]::Escape([string]$_.taskId) + '\\.dat$') })
            if ($graphTask.Count -eq 1) {
                Require ($levelGraphTaskKeys.ContainsKey($graphFile)) "Level $($level.level) graph '$graphFile' has no graph-overlay scenario."
            }
        }
        $aiAnimationScenarios = @($scenarios | Where-Object { $_.level -eq $level.level -and $_.action -eq 'ai-animation' })
        foreach ($scenario in $aiAnimationScenarios) {
            Require ($null -ne $scenario.anchor.animationClass) "Level $($level.level) ai-animation anchor for $($scenario.taskId) must carry animationClass."
            Require ($scenario.anchor.animationClass -in @('AnimTask', 'HumanAI')) "Level $($level.level) ai-animation anchor for $($scenario.taskId) has unknown animationClass '$($scenario.anchor.animationClass)'."
            Require ($null -ne $scenario.anchor.animationIds) "Level $($level.level) ai-animation anchor for $($scenario.taskId) must carry animationIds."
            Require ($null -ne $scenario.anchor.animationSource) "Level $($level.level) ai-animation anchor for $($scenario.taskId) must carry animationSource."
            Require ($null -ne $scenario.anchor.graphTaskId) "Level $($level.level) ai-animation anchor for $($scenario.taskId) must carry graphTaskId (or -1)."
            Require ($null -ne $scenario.anchor.patrol) "Level $($level.level) ai-animation anchor for $($scenario.taskId) must carry patrol classification."
            $patrol = $scenario.anchor.patrol
            Require ($patrol.status -in @('authored','none','not-applicable')) "Level $($level.level) ai-animation anchor for $($scenario.taskId) has invalid patrol status '$($patrol.status)'."
            if ($scenario.anchor.animationClass -eq 'HumanAI') {
                Require (-not [string]::IsNullOrWhiteSpace([string]$patrol.scriptPath)) "Level $($level.level) HumanAI $($scenario.taskId) must carry its AI script path."
                if ($patrol.status -eq 'authored') {
                    Require (@($patrol.pathIds).Count -gt 0) "Level $($level.level) HumanAI $($scenario.taskId) authored patrol must carry path IDs."
                } elseif ($patrol.status -eq 'none') {
                    Require (-not [string]::IsNullOrWhiteSpace([string]$patrol.exclusionReason)) "Level $($level.level) HumanAI $($scenario.taskId) without patrol must carry an exclusion reason."
                    $noPatrol = @($exclusions | Where-Object { $_.action -eq 'ai-patrol' -and $_.level -eq $level.level -and [string]$_.taskId -eq [string]$scenario.taskId })
                    Require ($noPatrol.Count -eq 1) "Level $($level.level) HumanAI $($scenario.taskId) without patrol must have one explicit ai-patrol exclusion."
                }
            }
        }
    }

    $task6GraphKeys = @{}
    $task6AnimationClasses = @{}
    $task6PatrolStatuses = @{}
    foreach ($scenario in $task6Scenarios) {
        $key = "$($scenario.level)|$($scenario.taskId)|$($scenario.action)"
        if ($scenario.action -eq 'graph-overlay') {
            $matching = @($scenarios | Where-Object { $_.level -eq $scenario.level -and $_.action -eq 'graph-overlay' -and [string]$_.taskId -eq [string]$scenario.taskId -and [string]$_.anchor.graphFile -eq [string]$scenario.anchor.graphFile })
            Require ($matching.Count -eq 1) "Task 6 graph scenario '$($scenario.name)' does not match exactly one generated graph anchor."
            $task6GraphKeys[[string]$scenario.anchor.graphFile] = $true
        } else {
            $matching = @($scenarios | Where-Object { $_.level -eq $scenario.level -and $_.action -eq 'ai-animation' -and [string]$_.taskId -eq [string]$scenario.taskId })
            Require ($matching.Count -eq 1) "Task 6 animation scenario '$($scenario.name)' does not match exactly one generated animation anchor."
            $task6AnimationClasses[[string]$scenario.anchor.animationClass] = $true
            if ([string]$scenario.anchor.animationClass -eq 'HumanAI') {
                $task6PatrolStatuses[[string]$scenario.anchor.patrol.status] = $true
            }
        }
    }
    foreach ($level in $levels) {
        foreach ($graphFile in @(Values $level.discovery.graphs)) {
            $graphTask = @($level.inventory | Where-Object { $_.type -eq 'AIGraph' -and [string]$graphFile -match ('\\graphs\\graph' + [regex]::Escape([string]$_.taskId) + '\\.dat$') })
            if ($graphTask.Count -eq 1) {
                Require ($task6GraphKeys.ContainsKey([string]$graphFile)) "Task 6 checked-in graph batch has no scenario for '$graphFile'."
            }
        }
    }
    $generatedAnimationScenarios = @($scenarios | Where-Object { $_.action -eq 'ai-animation' })
    foreach ($class in @($generatedAnimationScenarios | ForEach-Object { [string]$_.anchor.animationClass } | Sort-Object -Unique)) {
        Require ($task6AnimationClasses.ContainsKey($class)) "Task 6 checked-in animation batch has no representative for class '$class'."
    }
    foreach ($status in @($generatedAnimationScenarios | Where-Object { $_.anchor.animationClass -eq 'HumanAI' } | ForEach-Object { [string]$_.anchor.patrol.status } | Sort-Object -Unique)) {
        Require ($task6PatrolStatuses.ContainsKey($status)) "Task 6 checked-in animation batch has no HumanAI patrol representative for status '$status'."
    }

    # ---- Runner evidence/reversible primitives contract (Task 2) ----
    # Every case below is validated by editor-e2e.ps1 -ValidateOnly BEFORE any
    # editor launch.  Each scenario is otherwise well-formed (carries both a UI
    # oracle and a state oracle) and carries exactly one defect, so the runner
    # must reject it for that specific reason; the two pair cases prove the
    # generic screenshot/state-pair rule, and the valid cases must pass.

    $invalidOrbitAngle = Join-Path $runnerFixtureRoot 'invalid-orbit-angle.json'
    New-RunnerFixtureFile $invalidOrbitAngle @{ schemaVersion=1; scenarios=@(
        @{ name='runner-invalid-orbit-angle'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='orbit'; type='orbit_camera'; angle='sideways'; distance=200; pixels=12; screenshotAfter='orbit-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $invalidOrbitDistance = Join-Path $runnerFixtureRoot 'invalid-orbit-distance.json'
    New-RunnerFixtureFile $invalidOrbitDistance @{ schemaVersion=1; scenarios=@(
        @{ name='runner-invalid-orbit-distance'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='orbit'; type='orbit_camera'; angle='front'; distance=-5; pixels=12; screenshotAfter='orbit-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $outsidePath = Join-Path $runnerFixtureRoot 'outside-path.json'
    New-RunnerFixtureFile $outsidePath @{ schemaVersion=1; scenarios=@(
        @{ name='runner-path-outside-game-root'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='snapshot'; type='snapshot_paths'; paths=@('C:\Windows\win.ini') }
            @{ id='restore'; type='restore_paths' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $escapePath = Join-Path $runnerFixtureRoot 'escape-path.json'
    New-RunnerFixtureFile $escapePath @{ schemaVersion=1; scenarios=@(
        @{ name='runner-path-escape-attempt'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='snapshot'; type='snapshot_paths'; paths=@('..\..\Windows\win.ini') }
            @{ id='restore'; type='restore_paths' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $missingRestoreHashes = Join-Path $runnerFixtureRoot 'missing-restore-hashes.json'
    New-RunnerFixtureFile $missingRestoreHashes @{ schemaVersion=1; scenarios=@(
        @{ name='runner-restore-without-snapshot'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='restore'; type='restore_paths'; paths=@('editor/qed/qedconfig.qsc') }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $invalidAssertFileHash = Join-Path $runnerFixtureRoot 'invalid-assert-file-hash.json'
    New-RunnerFixtureFile $invalidAssertFileHash @{ schemaVersion=1; scenarios=@(
        @{ name='runner-invalid-file-hash'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='hash-check'; type='assert_file_hash'; path='editor/qed/qedconfig.qsc'; sha256='not-a-hex-digest' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $failedStateOnly = Join-Path $runnerFixtureRoot 'failed-state-only.json'
    New-RunnerFixtureFile $failedStateOnly @{ schemaVersion=1; scenarios=@(
        @{ name='runner-state-only-pair'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='healthy'; type='capture_window_state' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $failedVisualOnly = Join-Path $runnerFixtureRoot 'failed-visual-only.json'
    New-RunnerFixtureFile $failedVisualOnly @{ schemaVersion=1; scenarios=@(
        @{ name='runner-visual-only-pair'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-pair-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $validPair = Join-Path $runnerFixtureRoot 'valid-pair.json'
    New-RunnerFixtureFile $validPair @{ schemaVersion=1; scenarios=@(
        @{ name='runner-screenshot-state-valid-pair'; level=1; requiresMutation=$false; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-pair-shot' }
            @{ id='healthy'; type='capture_window_state' }
            @{ id='state-shot'; type='screenshot'; name='primitive-pair-state-shot' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $validMutation = Join-Path $runnerFixtureRoot 'valid-mutation.json'
    New-RunnerFixtureFile $validMutation @{ schemaVersion=1; scenarios=@(
        @{ name='runner-valid-reversible-mutation'; level=1; requiresMutation=$true; steps=@(
            @{ id='launch'; type='launch_editor'; level=1 }
            @{ id='window'; type='wait_for_window'; timeoutSeconds=45 }
            @{ id='shot'; type='screenshot'; name='primitive-shot' }
            @{ id='snapshot'; type='snapshot_paths'; paths=@('editor/qed/qedconfig.qsc') }
            @{ id='restore'; type='restore_paths' }
            @{ id='close'; type='close_editor'; force=$true }
        ) }
    ) }

    $runnerCases = @(
        @{ Label='unsupported orbit angle'; Path=$invalidOrbitAngle; Code=1; Keyword='angle must be one of' },
        @{ Label='unsupported orbit distance'; Path=$invalidOrbitDistance; Code=1; Keyword='distance must be greater than 0' },
        @{ Label='path outside D:\IGI1'; Path=$outsidePath; Code=1; Keyword='relative to the game root' },
        @{ Label='path escaping D:\IGI1'; Path=$escapePath; Code=1; Keyword='relative to the game root' },
        @{ Label='missing restore hashes'; Path=$missingRestoreHashes; Code=1; Keyword='no preceding snapshot_paths' },
        @{ Label='invalid file-hash digest'; Path=$invalidAssertFileHash; Code=1; Keyword='SHA-256 digest' },
        @{ Label='failed state-only pair'; Path=$failedStateOnly; Code=1; Keyword='screenshot/UI oracle' },
        @{ Label='failed visual-only pair'; Path=$failedVisualOnly; Code=1; Keyword='state/data oracle' },
        @{ Label='valid screenshot/state pair'; Path=$validPair; Code=0; Keyword='Validated' },
        @{ Label='valid reversible mutation manifest'; Path=$validMutation; Code=0; Keyword='Validated' }
    )
    foreach ($case in $runnerCases) {
        $outcome = Invoke-RunnerValidate $case.Path
        Require ($outcome.ExitCode -eq $case.Code) "Runner contract case '$($case.Label)' exited $($outcome.ExitCode); expected $($case.Code). Output: $($outcome.Output)"
        Assert-OutputContains $outcome.Output $case.Keyword $case.Label
    }

    Write-Host 'Editor workflow manifest contract: PASS'
} finally {
    if ($locationPushed) { Pop-Location }
    Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $decompileRoot -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $runnerFixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}
