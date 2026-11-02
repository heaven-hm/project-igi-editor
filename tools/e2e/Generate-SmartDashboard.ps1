[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ArtifactsRoot
)

$ErrorActionPreference = 'Stop'
$pyScript = Join-Path $PSScriptRoot 'generate_dashboard.py'
$pythonCandidates = @(
    'D:\henv\Scripts\python.exe',
    'python.exe'
)

$pythonExe = $null
foreach ($c in $pythonCandidates) {
    if (Test-Path -LiteralPath $c) {
        $pythonExe = [IO.Path]::GetFullPath($c)
        break
    }
}
if (-not $pythonExe) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { $pythonExe = $cmd.Source }
}

if (-not $pythonExe) {
    Write-Warning "Python environment not found; skipping dashboard generation."
    return
}

& $pythonExe $pyScript --artifact-dir $ArtifactsRoot
$reportPath = Join-Path $ArtifactsRoot 'report.html'
if (Test-Path -LiteralPath $reportPath) {
    Write-Host ("[Dashboard Report] file:///{0}" -f ($reportPath.Replace('\', '/'))) -ForegroundColor Cyan
}
