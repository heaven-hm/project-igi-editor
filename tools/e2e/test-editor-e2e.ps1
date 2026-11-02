$ErrorActionPreference = 'Stop'

$runner = Join-Path $PSScriptRoot 'editor-e2e.ps1'
$valid = Join-Path $PSScriptRoot 'scenarios\editor-regression.json'
$invalid = Join-Path $PSScriptRoot 'scenarios\invalid-missing-step-id.json'
$invalidActionOracle = Join-Path $PSScriptRoot 'scenarios\invalid-action-oracle-dependency.json'
$workflows = Join-Path $PSScriptRoot 'scenarios\editor-workflows.json'
$objectVisual = Join-Path $PSScriptRoot 'scenarios\object-visual-workflows.json'
$assetWorkflows = Join-Path $PSScriptRoot 'scenarios\asset-workflows.json'

if (-not (Test-Path -LiteralPath $runner)) {
    throw "Runner is missing: $runner"
}

function Invoke-Validation([string] $manifest) {
    # Run the child out of process so its intentional malformed-manifest error
    # cannot be promoted by Windows PowerShell; the exit code remains
    # authoritative for both the invalid and valid contracts.
    $child = Start-Process -FilePath 'pwsh.exe' -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runner,
        '-ValidateOnly', '-ScenarioPath', $manifest
    ) -WindowStyle Hidden -Wait -PassThru
    return [int]$child.ExitCode
}

$invalidCode = Invoke-Validation $invalid
if ($invalidCode -eq 0) {
    throw 'Malformed manifest was accepted.'
}

$invalidActionOracleCode = Invoke-Validation $invalidActionOracle
if ($invalidActionOracleCode -eq 0) {
    throw 'Action-result oracle with a missing dependency was accepted.'
}

$validCode = Invoke-Validation $valid
if ($validCode -ne 0) {
    throw "Valid manifest was rejected with exit code $validCode."
}

$workflowsCode = Invoke-Validation $workflows
if ($workflowsCode -ne 0) {
    throw "Editor workflows manifest was rejected with exit code $workflowsCode."
}

$objectVisualCode = Invoke-Validation $objectVisual
if ($objectVisualCode -ne 0) {
    throw "Object visual workflows manifest was rejected with exit code $objectVisualCode."
}

$assetWorkflowsCode = Invoke-Validation $assetWorkflows
if ($assetWorkflowsCode -ne 0) {
    throw "Asset workflows manifest was rejected with exit code $assetWorkflowsCode."
}

Write-Host 'Editor E2E manifest contract: PASS'
