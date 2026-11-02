[CmdletBinding()]
param([string]$ObjPath)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ObjPath)) { $ObjPath = Join-Path $PSScriptRoot 'fixtures/camera-box.obj' }
$plan = & (Join-Path $PSScriptRoot 'New-SmartCameraPlan.ps1') -ObjPath $ObjPath -Position @(100,200,300)
if ($plan.views.Count -ne 10) { throw 'Missing view coverage' }
if (@($plan.views.name | Select-Object -Unique).Count -ne 10) { throw 'Duplicate view names' }
foreach ($view in $plan.views) {
    $yaw=$view.yaw*[Math]::PI/180; $pitch=$view.pitch*[Math]::PI/180
    $forward=@((-[Math]::Sin($yaw)*[Math]::Cos($pitch)),([Math]::Cos($yaw)*[Math]::Cos($pitch)),[Math]::Sin($pitch))
    $squared=0
    foreach ($axis in 0..2) {
        $delta=$plan.target[$axis]-$view.position[$axis]
        $squared += $delta*$delta
        if ([Math]::Abs($view.position[$axis]+$forward[$axis]*$plan.distance-$plan.target[$axis]) -gt 0.001) { throw "$($view.name) does not aim at the mesh center" }
    }
    if ([Math]::Abs([Math]::Sqrt($squared)-$plan.distance) -gt 0.001) { throw 'Inconsistent orbit radius' }
}
$rejected=$false
try { & (Join-Path $PSScriptRoot 'New-SmartCameraPlan.ps1') -ObjPath $ObjPath -Position @(0,0,0) -Scale 0 | Out-Null } catch { $rejected=$true }
if (-not $rejected) { throw 'Zero scale must be rejected' }
'PASS: ten camera poses point at the mesh center with a constant radius; zero scale rejected.'
