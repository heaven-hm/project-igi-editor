[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ObjPath,
    [Parameter(Mandatory)][double[]]$Position,
    [double[]]$Rotation = @(0,0,0),
    [double]$Scale = 40.96,
    [double]$VerticalFov = 45,
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
if ($Position.Count -ne 3 -or $Rotation.Count -lt 1 -or $Rotation.Count -gt 3 -or $Scale -le 0 -or $VerticalFov -le 0 -or $VerticalFov -ge 180) { throw 'Invalid camera geometry parameters.' }
$rotationForCamera = @(0, 0, 0)
for ($i = 0; $i -lt $Rotation.Count; $i++) { $rotationForCamera[$i] = $Rotation[$i] }
foreach ($value in @($Position)+@($rotationForCamera)+@($Scale,$VerticalFov)) {
    if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw 'Camera geometry must be finite.' }
}
$points = @(foreach ($line in [IO.File]::ReadLines((Resolve-Path $ObjPath))) {
    if ($line -notmatch '^v\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)\s*$') { continue }
    $v = @(1..3 | ForEach-Object { [double]::Parse($Matches[$_], [Globalization.CultureInfo]::InvariantCulture) * $Scale })
    # Renderer placement: translate * Rz * Rx * Ry * scale (radians).
    $cy=[Math]::Cos($rotationForCamera[1]); $sy=[Math]::Sin($rotationForCamera[1])
    $x=$cy*$v[0]+$sy*$v[2]; $y=$v[1]; $z=-$sy*$v[0]+$cy*$v[2]
    $cx=[Math]::Cos($rotationForCamera[0]); $sx=[Math]::Sin($rotationForCamera[0])
    $y2=$cx*$y-$sx*$z; $z2=$sx*$y+$cx*$z
    $cz=[Math]::Cos($rotationForCamera[2]); $sz=[Math]::Sin($rotationForCamera[2])
    [pscustomobject]@{x=$Position[0]+$cz*$x-$sz*$y2;y=$Position[1]+$sz*$x+$cz*$y2;z=$Position[2]+$z2}
})
if ($points.Count -lt 3) { throw 'Mesh has fewer than three vertices.' }
$bounds = foreach ($axis in @('x','y','z')) { $points | Measure-Object -Property $axis -Minimum -Maximum }
$center = @(foreach ($bound in $bounds) { ($bound.Minimum+$bound.Maximum)/2 })
$radius = [Math]::Sqrt([Math]::Pow(($bounds[0].Maximum-$bounds[0].Minimum)/2,2)+[Math]::Pow(($bounds[1].Maximum-$bounds[1].Minimum)/2,2)+[Math]::Pow(($bounds[2].Maximum-$bounds[2].Minimum)/2,2))
if ($radius -le 0) { throw 'Mesh has zero extent.' }
$distance = 1.2*$radius/[Math]::Sin($VerticalFov*[Math]::PI/360)
$views = foreach ($azimuth in @(0,45,90,135,180,225,270,315)) {
    $yaw=$azimuth*[Math]::PI/180
    [pscustomobject]@{name="azimuth-$azimuth";position=@(($center[0]+[Math]::Sin($yaw)*$distance),($center[1]-[Math]::Cos($yaw)*$distance),$center[2]);yaw=$azimuth;pitch=0;roll=0}
}
foreach ($pitch in @(-89,89)) {
    $rad=$pitch*[Math]::PI/180
    $views += [pscustomobject]@{name=$(if($pitch -lt 0){'above'}else{'below'});position=@($center[0],($center[1]-[Math]::Cos($rad)*$distance),($center[2]-[Math]::Sin($rad)*$distance));yaw=0;pitch=$pitch;roll=0}
}
$plan = [pscustomobject]@{schemaVersion=1;meshHash=(Get-FileHash $ObjPath -Algorithm SHA256).Hash;vertexCount=$points.Count;authoredPosition=@($Position);authoredRotation=@($Rotation);cameraRotation=@($rotationForCamera);target=$center;radius=$radius;distance=$distance;views=@($views);verification='planned poses only; live framing and materials remain unverified'}
if ($OutputPath) { $plan | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8 }
$plan
