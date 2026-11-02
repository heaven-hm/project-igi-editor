$ErrorActionPreference='Stop'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'editor-e2e.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw $errors[0] }
foreach ($name in @('Fail','Get-Property','Assert-Integer','Assert-Region','Validate-Step')) {
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
    Invoke-Expression $fn.Extent.Text
}
$script:SupportedSteps=@('screenshot','launch_editor')
Validate-Step ([pscustomobject]@{id='shot';type='screenshot';name='shot';client=$true}) 'test' 0 $pwd
Validate-Step ([pscustomobject]@{id='launch';type='launch_editor';level=1;drawParts=-2}) 'test' 0 $pwd
foreach($step in @(
    [pscustomobject]@{id='shot';type='screenshot';name='shot';client='false'},
    [pscustomobject]@{id='shot';type='screenshot';name='shot';client=$true;region=@(0,0,20,20)},
    [pscustomobject]@{id='launch';type='launch_editor';level=1;drawParts='-2 -level 14'}
)) {
    $rejected=$false
    try { Validate-Step $step 'test' 0 $pwd } catch { $rejected=$true }
    if(-not $rejected) { throw 'Invalid camera-runner option accepted.' }
}
'PASS: physical capture and diagnostic draw options validated; ambiguous or injected options rejected.'
