function Test-SmartModelLog {
    param([string]$Text, $Anchor)
    $failures = [Collections.Generic.List[string]]::new()
    if (@($Anchor.authoredPosition).Count -ne 3 -or @($Anchor.authoredRotation).Count -ne 3 -or [string]::IsNullOrWhiteSpace([string]$Anchor.modelId)) { throw 'Incomplete authored model anchor.' }
    $model = [regex]::Escape([string]$Anchor.modelId)
    $invariant = [Globalization.CultureInfo]::InvariantCulture
    $rows = [regex]::Matches($Text, ('\[LevelLoader\] Object Loaded: ModelID='+$model+', Type=([^,]+), Name=(.*?), Pos=\(([^)]+)\), Ori=\(([^)]+)\),'))
    $matching = @()
    $matchingKeys = @()
    $isAI = ($Anchor.category -eq 'AI' -or $Anchor.type -in @('HumanSoldier','HumanSoldierFemale','HumanSoldierRPG','HumanPlayer','HumanAI','AISquad','PatrolPath','PatrolPathCommand'))
    foreach ($row in $rows) {
        $pos = @($row.Groups[3].Value.Split(',') | ForEach-Object { [double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture) })
        $ori = @($row.Groups[4].Value.Split(',') | ForEach-Object { [double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture) })
        if ($pos.Count -ne 3 -or $ori.Count -ne 3) { continue }
        $same = $row.Groups[1].Value -eq $Anchor.type
        foreach ($axis in 0..2) {
            foreach ($value in @($pos[$axis],$ori[$axis],[double]$Anchor.authoredPosition[$axis],[double]$Anchor.authoredRotation[$axis])) {
                if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { $same=$false }
            }
            if ([Math]::Abs($pos[$axis]-[double]$Anchor.authoredPosition[$axis]) -gt 32) { $same=$false }
            if (-not $isAI -and [Math]::Abs($ori[$axis]-[double]$Anchor.authoredRotation[$axis]) -gt 0.00001) { $same=$false }
        }
        if ($same) {
            $matching += $row.Value
            $matchingKeys += ((@($pos+$ori | ForEach-Object { ([double]$_).ToString('R',$invariant) })) -join ',')
        }
    }
    $matchingVariants = @($matchingKeys | Select-Object -Unique).Count
    if ($matchingVariants -ne 1) { $failures.Add('Expected one consistent loaded-model transform; absent or conflicting evidence.') }
    $assignment = [regex]::Matches($Text, ('\[TEX Native\] Applied textures to modelId='+$model+' subMeshes=(\d+) datTextures=(\d+) assigned=(\d+)'))
    if ($assignment.Count -eq 0) { $failures.Add('Missing live texture-assignment evidence.') }
    foreach ($row in $assignment) {
        if ([int]$row.Groups[1].Value -le 0 -or [int]$row.Groups[2].Value -le 0 -or [int]$row.Groups[3].Value -ne [int]$row.Groups[1].Value) { $failures.Add('Incomplete live submesh texture assignments.') }
    }
    $textureLoads = @()
    foreach ($texture in @($Anchor.requiredTextures)) {
        if ([string]::IsNullOrWhiteSpace([string]$texture)) { continue }
        $texturePattern = [regex]::Escape([string]$texture)
        $loaded = [regex]::IsMatch($Text, ('\[TEX\] ResCache loaded '+$texturePattern+' ([1-9]\d*)x([1-9]\d*)(?:\s|$)')) -or
            [regex]::IsMatch($Text, ('\[TEX Native\] Loaded textureId='+$texturePattern+' ([1-9]\d*)x([1-9]\d*)\s+frames='))
        $textureLoads += [pscustomobject]@{texture=$texture;loaded=$loaded}
        if (-not $loaded) { $failures.Add("Missing successful load evidence for required texture $texture.") }
    }
    [pscustomobject]@{passed=($failures.Count -eq 0);failures=@($failures.ToArray());matchingTransforms=$matching.Count;matchingTransformVariants=$matchingVariants;assignmentRecords=$assignment.Count;requiredTextureLoads=$textureLoads;scope='loader transform, required texture loads and assignment counts; not per-draw GPU bindings or visual acceptance'}
}
