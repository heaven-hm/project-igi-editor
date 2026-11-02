[CmdletBinding()]
param([string]$OutputPath = 'artifacts/e2e/graph-smoke-14.json')
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$draft = Get-Content (Join-Path $PSScriptRoot 'scenarios/graph-ai-animation-workflows.json') -Raw | ConvertFrom-Json
$selected = foreach ($level in 1..14) {
    $areas = Get-Content (Join-Path $repo "assets/editor/tools/QGraphs/graph_level$level.json") -Raw | ConvertFrom-Json
    $eligible = @($areas | Where-Object { $_.Area -and $_.Area -notmatch 'cut\s*scene' } | ForEach-Object { [string]$_.Graph })
    $candidate = $draft.scenarios | Where-Object { $_.level -eq $level -and $_.action -eq 'graph-overlay' -and [string]$_.taskId -in $eligible -and $_.anchor.nodeCount -gt 0 } | Sort-Object { $_.anchor.nodeCount } -Descending | Select-Object -First 1
    if (-not $candidate) { throw "No known non-cutscene graph for level $level" }
    $steps = @($candidate.steps | Where-Object { $_.id -in @('launch','window','loaded','healthy','open-find','type-task','confirm-find','find-settle','show-graph','graph-settle','focus-graph','focus-settle','graph-before') })
    $steps += @{ id='graph-loaded'; type='assert_log'; pattern=('\[GRAPH\] Overlay loaded ' + $candidate.anchor.nodeCount + ' nodes, .*graph' + [regex]::Escape([string]$candidate.taskId) + '\.dat'); timeoutSeconds=5 }
    $steps += @{ id='final-health'; type='assert_process' }
    $steps += @{ id='close'; type='close_editor'; force=$true }
    [pscustomobject][ordered]@{ name="level$level-graph-smoke-$($candidate.taskId)"; level=$level; taskId=$candidate.taskId; area=($areas | Where-Object { [string]$_.Graph -eq [string]$candidate.taskId }).Area; nodeCount=$candidate.anchor.nodeCount; requiresMutation=$false; steps=$steps }
}
$parent = Split-Path $OutputPath -Parent
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
@{ scenarios=@($selected) } | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$selected | Select-Object level,taskId,area,nodeCount | Format-Table
