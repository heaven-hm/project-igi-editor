$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'SmartModelEvidence.ps1')
$anchor=[pscustomobject]@{modelId='405_01_1';type='Building';authoredPosition=@(100,200,300);authoredRotation=@(0,0,1)}
$text="[LevelLoader] Object Loaded: ModelID=405_01_1, Type=Building, Name=WatchTower, Pos=(100,200,300), Ori=(0,0,1),`n[TEX Native] Applied textures to modelId=405_01_1 subMeshes=19 datTextures=8 assigned=19"
if (-not (Test-SmartModelLog $text $anchor).passed) { throw 'Valid loader/assignment evidence was rejected.' }
foreach($bad in @('',($text -replace 'assigned=19','assigned=0'),($text -replace 'Ori=\(0,0,1\)','Ori=(0,0,2)'),($text -replace 'Pos=\(100,200,300\)','Pos=(1000,200,300)'),($text+"`n"+($text -replace 'Pos=\(100,200,300\)','Pos=(101,200,300)')))) {
    if ((Test-SmartModelLog $bad $anchor).passed) { throw 'Missing, corrupt, or ambiguous evidence was accepted.' }
}
'PASS: correct state accepted; missing assignments, wrong transforms, and ambiguous logs rejected.'
$anchor | Add-Member -NotePropertyName requiredTextures -NotePropertyValue @('required_1')
if ((Test-SmartModelLog $text $anchor).passed) { throw 'Missing required texture accepted.' }
if (-not (Test-SmartModelLog ($text+"`n[TEX] ResCache loaded required_1 128x128") $anchor).passed) { throw 'Successful required texture load rejected.' }
if ((Test-SmartModelLog ($text+"`n[TEX] ResCache loaded required_1 0x128") $anchor).passed) { throw 'Zero-size texture accepted.' }
'PASS: native texture-load records with positive dimensions accepted.'
$native = $text+"`n[TEX Native] Loaded textureId=required_1 128x128 frames=1 path=required_1.tex"
if (-not (Test-SmartModelLog $native $anchor).passed) { throw 'Native successful texture load rejected.' }
'PASS: required texture identities and positive dimensions checked.'
