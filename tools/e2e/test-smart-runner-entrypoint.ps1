$ErrorActionPreference = 'Stop'
$runnerPath = Join-Path $PSScriptRoot '../../Run-SmartTest.ps1'
$editorExePath = (Resolve-Path (Join-Path $PSScriptRoot '../../bin/Release/igi1ed.exe')).Path
$root = Join-Path ([IO.Path]::GetTempPath()) ('igi-smart-entrypoint-' + [Guid]::NewGuid().ToString('N'))
$commandRoot = Join-Path ([IO.Path]::GetTempPath()) ('igi-smart-command-' + [Guid]::NewGuid().ToString('N'))
try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runnerPath -ArtifactsRoot $root -Level 1 -MaxObjects 1 -ViewCount 3 -PrepareOnly -EditorExePath $editorExePath
    if ($LASTEXITCODE -ne 0) { throw "Smart runner returned $LASTEXITCODE." }
    $summary = @(Get-Content -LiteralPath (Join-Path $root 'summary.json') -Raw | ConvertFrom-Json)
    if ($summary.Count -ne 1 -or $summary[0].status -ne 'PREPARED') { throw 'Smart runner did not produce one prepared level result.' }
    if ([string]$summary[0].editorExecutable -ne $editorExePath) { throw 'Smart runner did not pass the explicit editor executable to its native session.' }
    Push-Location (Join-Path $PSScriptRoot '../..')
    try { & cmd.exe /c tests_run.cmd --level 1 --maximum 1 --views 3 --prepare-only --editor-exe $editorExePath --artifacts $commandRoot }
    finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Smart command returned $LASTEXITCODE." }
    $commandSummary = @(Get-Content -LiteralPath (Join-Path $commandRoot 'summary.json') -Raw | ConvertFrom-Json)
    if ($commandSummary.Count -ne 1 -or [string]$commandSummary[0].editorExecutable -ne $editorExePath) { throw 'Smart command did not forward its explicit editor executable.' }
    'PASS: smart runner and command wrapper forward an explicit editor executable.'
} finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $commandRoot) { Remove-Item -LiteralPath $commandRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
