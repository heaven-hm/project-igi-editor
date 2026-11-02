[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactsRoot,
    [ValidateRange(1, 10)][int]$MinimumViews = 3,
    [ValidateRange(0.001, 0.95)][double]$MinimumCoverage = 0.001,
    [ValidateSet('Required','ReportOnly')][string]$VisualIntegrityPolicy = 'Required',
    [switch]$RequireVideo
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($ArtifactsRoot)
function Get-PortableSha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [IO.File]::ReadAllBytes($Path)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()
    } finally { $sha.Dispose() }
}
$batches = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter batch.json |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
if ($batches.Count -eq 0) { throw "No batch.json was found below $root." }

$failures = [Collections.Generic.List[string]]::new()
function Get-BundlePath([string]$BundleRoot, [string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath -match '(^|[\\/])\.\.([\\/]|$)' -or $RelativePath -match '^[A-Za-z]:') {
        return $null
    }
    $candidate = [IO.Path]::GetFullPath((Join-Path $BundleRoot $RelativePath))
    $rootWithSlash = ([IO.Path]::GetFullPath($BundleRoot)).TrimEnd('\') + '\'
    if (-not $candidate.StartsWith($rootWithSlash, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $candidate
}
function Test-BundleFile([string]$BundleRoot, [string]$RelativePath, [string]$Identity) {
    $path = Get-BundlePath $BundleRoot $RelativePath
    if ($null -eq $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $failures.Add("$Identity references missing or unsafe bundle file '$RelativePath'.")
        return $false
    }
    return $true
}
foreach ($batch in $batches) {
    if ($batch.status -ne 'PASS') { $failures.Add("Level $($batch.level) batch status is $($batch.status), not PASS."); continue }
    foreach ($object in @($batch.objects)) {
        $identity = "L$($batch.level) task $($object.taskId) model $($object.modelId)"
        $bundleRoot = if ($object.prefix) { Join-Path $root ('screenshots\\' + [string]$object.prefix) } else { $null }
        $bundleManifest = $null
        if ($null -eq $bundleRoot -or -not (Test-Path -LiteralPath $bundleRoot -PathType Container)) {
            $failures.Add("$identity has no portable object-evidence bundle directory.")
        } else {
            $manifestPath = Join-Path $bundleRoot 'manifest.json'
            if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
                $failures.Add("$identity is missing object-evidence/manifest.json.")
            } else {
                try { $bundleManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json }
                catch { $failures.Add("$identity has invalid object-evidence/manifest.json: $($_.Exception.Message)") }
                if ($null -ne $bundleManifest) {
                    if ([int]$bundleManifest.schemaVersion -ne 1) { $failures.Add("$identity has unsupported evidence manifest schema.") }
                    if ([string]$bundleManifest.source.editorSha256 -notmatch '^[a-f0-9]{64}$' -or
                        [string]$bundleManifest.source.inventorySha256 -notmatch '^[a-f0-9]{64}$' -or
                        [string]$bundleManifest.source.levelSourceSha256 -notmatch '^[a-f0-9]{64}$') {
                        $failures.Add("$identity evidence manifest lacks reproducible source hashes.")
                    }
                    if ([int]$bundleManifest.object.level -ne [int]$batch.level -or
                        [string]$bundleManifest.object.taskId -ne [string]$object.taskId -or
                        [string]$bundleManifest.object.modelId -ne [string]$object.modelId) {
                        $failures.Add("$identity evidence manifest identity does not match batch.json.")
                    }
                    foreach ($file in @($bundleManifest.files)) {
                        $relative = [string]$file.relativePath
                        if (Test-BundleFile $bundleRoot $relative $identity) {
                            $actual = Get-PortableSha256 (Get-BundlePath $bundleRoot $relative)
                            if ([string]$file.sha256 -and $actual -ne [string]$file.sha256) {
                                $failures.Add("$identity evidence hash mismatch for '$relative'.")
                            }
                        }
                    }
                    if ($bundleManifest.visualIntegrityPath) {
                        if (Test-BundleFile $bundleRoot ([string]$bundleManifest.visualIntegrityPath) $identity) {
                            try {
                                $portableResult = Get-Content -LiteralPath (Get-BundlePath $bundleRoot ([string]$bundleManifest.visualIntegrityPath)) -Raw | ConvertFrom-Json
                                if ($portableResult.evidence) {
                                    foreach ($field in @('objectMasks','materialMasks','depthBuffers','normalBuffers','overlays')) {
                                        if ($null -ne $portableResult.evidence.$field) {
                                            foreach ($reference in @($portableResult.evidence.$field)) {
                                                Test-BundleFile $bundleRoot ([string]$reference) $identity | Out-Null
                                            }
                                        }
                                    }
                                }
                                $integrity = $portableResult.visualIntegrity
                                if ($null -eq $integrity) {
                                    $failures.Add("$identity visual-integrity result is absent from its evidence contract.")
                                } else {
                                    if ($null -eq $portableResult.projectedParts) {
                                        $failures.Add("$identity visual-integrity evidence lacks serialized projected-part measurements.")
                                    }
                                    if ([int]$integrity.schemaVersion -ne 1 -or $null -eq $integrity.summary) {
                                        $failures.Add("$identity visual-integrity result has an unsupported result contract.")
                                    }
                                    if ($null -eq $portableResult.transform -or $null -eq $portableResult.capture) {
                                        $failures.Add("$identity visual-integrity contract lacks authored/runtime transform or capture metadata.")
                                    }
                                    if ([int]$integrity.partsExpected -ne @($portableResult.expectedParts).Count) {
                                        $failures.Add("$identity visual-integrity expected-part summary does not match its geometry inventory.")
                                    }
                                    if ([int]$integrity.partsObserved -lt 0 -or [int]$integrity.partsObserved -gt [int]$integrity.partsExpected) {
                                        $failures.Add("$identity visual-integrity observed-part summary is outside its inventory bounds.")
                                    }
                                    if (([int]$integrity.viewsPassed + [int]$integrity.viewsFailed) -ne [int]$integrity.viewsChecked) {
                                        $failures.Add("$identity visual-integrity view summary is internally inconsistent.")
                                    }
                                    if ([int]$integrity.summary.partsExpected -ne [int]$integrity.partsExpected -or
                                        [int]$integrity.summary.partsObserved -ne [int]$integrity.partsObserved -or
                                        [int]$integrity.summary.viewsChecked -ne [int]$integrity.viewsChecked) {
                                        $failures.Add("$identity visual-integrity nested summary disagrees with its compatibility fields.")
                                    }
                                    foreach ($finding in @($integrity.findings)) {
                                        if ([string]$finding.severity -notin @('error','warning')) {
                                            $failures.Add("$identity visual-integrity finding has no actionable severity.")
                                        }
                                        foreach ($reference in @($finding.evidence)) {
                                            Test-BundleFile $bundleRoot ([string]$reference) $identity | Out-Null
                                        }
                                    }
                                    $independentAnalyzer = Join-Path $PSScriptRoot 'Analyze-VisualIntegrityBundle.ps1'
                                    if (Test-Path -LiteralPath $independentAnalyzer -PathType Leaf) {
                                        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $independentAnalyzer `
                                            -BundleRoot $bundleRoot -VisualIntegrityPolicy $VisualIntegrityPolicy | Out-Null
                                        if ($LASTEXITCODE -ne 0 -and $VisualIntegrityPolicy -eq 'Required') {
                                            $failures.Add("$identity independent visual-integrity bundle analysis failed.")
                                        }
                                    }
                                }
                            } catch { $failures.Add("$identity has invalid portable visual-integrity evidence: $($_.Exception.Message)") }
                        }
                    }
                    if ($bundleManifest.evidencePath) { Test-BundleFile $bundleRoot ([string]$bundleManifest.evidencePath) $identity | Out-Null }
                }
            }
        }
        if ([int]$object.screenshotCount -lt $MinimumViews) {
            $failures.Add("$identity has $($object.screenshotCount) screenshots; expected at least $MinimumViews.")
        }
        $records = @($object.captureEvidence)
        if ($records.Count -lt $MinimumViews) {
            $failures.Add("$identity has $($records.Count) render-evidence records; expected at least $MinimumViews.")
            continue
        }
        foreach ($record in $records | Select-Object -First $MinimumViews) {
            if (-not $record.rendered) { $failures.Add("$identity $($record.view) was not rendered."); continue }
            if ($record.targetVisible -ne $true) { $failures.Add("$identity $($record.view) has no target-visible proof."); continue }
            if ($null -eq $record.targetCoverage -or [double]$record.targetCoverage -lt $MinimumCoverage) {
                $failures.Add("$identity $($record.view) coverage $($record.targetCoverage) is below $MinimumCoverage.")
            }
            if ($bundleRoot) {
                foreach ($field in @('png','bmp','visualObjectMask','visualMaterialMask','visualDepth','visualOverlay')) {
                    $value = [string]$record.$field
                    if ($value) { Test-BundleFile $bundleRoot ([IO.Path]::GetFileName($value)) $identity | Out-Null }
                }
            }
        }
        $visualStatus = if ($null -eq $object.visualIntegrity) { 'INCONCLUSIVE' } else {
            [string]$object.visualIntegrity.visualIntegrity.status
        }
        if ($VisualIntegrityPolicy -eq 'Required' -and $visualStatus -ne 'PASS') {
            $failures.Add("$identity visual-integrity status is $visualStatus; loader evidence cannot satisfy this gate.")
        }
        if ($RequireVideo -and ($null -eq $object.video -or $object.video.status -ne 'PASS')) {
            $failures.Add("$identity is missing a passing orbit video.")
        }
    }
}

if ($failures.Count) { throw ($failures -join [Environment]::NewLine) }
Write-Output "PASS: $($batches.Count) batch artifacts prove visible target framing for every checked view."
