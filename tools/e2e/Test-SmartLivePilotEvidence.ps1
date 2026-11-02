[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunDirectory,
    [Parameter(Mandatory)][string]$LogSnapshot,
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content (Join-Path $RunDirectory 'scenario-manifest.json') -Raw | ConvertFrom-Json
$run = Get-Content (Join-Path $RunDirectory 'run.json') -Raw | ConvertFrom-Json
if (@($manifest.scenarios).Count -ne 1 -or @($run.results).Count -ne 1) { throw 'Pilot evidence requires exactly one scenario.' }
$scenario = $manifest.scenarios[0]; $result = $run.results[0]
$failures = [Collections.Generic.List[string]]::new()
if ($result.status -ne 'PASS') { $failures.Add("Capture failed: $($result.failure)") }
$bytes = [IO.File]::ReadAllBytes((Resolve-Path $LogSnapshot))
if ($result.logOffset -gt $bytes.Length) { throw 'Log snapshot is shorter than the run offset.' }
$log = [Text.Encoding]::UTF8.GetString($bytes, [int]$result.logOffset, $bytes.Length - [int]$result.logOffset)
$matches = [regex]::Matches($log, '\[Camera\] F11 target=(?<kind>.*?) position=\((?<x>[^,]+),(?<y>[^,]+),(?<z>[^)]+)\)')
if ($matches.Count -eq 0) { $failures.Add('Missing camera-target evidence.') }
foreach ($match in $matches) {
    if ($match.Groups['kind'].Value -ne 'Object') { $failures.Add('Camera focused a graph instead of the pilot object.'); break }
    $axes = @('x','y','z')
    for ($i=0; $i -lt 3; $i++) {
        $observed = [double]::Parse($match.Groups[$axes[$i]].Value, [Globalization.CultureInfo]::InvariantCulture)
        if ([Math]::Abs($observed - [double]$scenario.anchor.authoredPosition[$i]) -gt 32) {
            $failures.Add("Camera target differs from authored object position on $($axes[$i]).")
        }
    }
}
$shots = foreach ($angle in @('front','front-right','right','back-right','back','back-left','left','front-left')) {
    $path = Join-Path (Join-Path $RunDirectory $result.name) "$angle.png"
    if (-not (Test-Path -LiteralPath $path)) { $failures.Add("Missing $angle screenshot."); continue }
    [pscustomobject]@{angle=$angle;path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
}
# Neither camera intent nor file presence establishes projected model pixels,
# actual view angles, live material assignment, or orientation correctness.
$pending = @('projected-object framing','measured camera poses','live orientation versus authored rotation','live texture and attachment assignment','top and bottom views')
$report = [ordered]@{ accepted=$false; status=$(if($failures.Count){'FAIL'}else{'UNVERIFIED'});level=$scenario.level;taskId=$scenario.anchor.taskId;modelId=$scenario.anchor.modelId;failures=@($failures.ToArray());pending=$pending;screenshots=@($shots);logSnapshotHash=(Get-FileHash -LiteralPath $LogSnapshot -Algorithm SHA256).Hash }
if (-not $OutputPath) { $OutputPath = Join-Path $RunDirectory 'pilot-acceptance.json' }
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$report | ConvertTo-Json -Depth 4
exit 1
