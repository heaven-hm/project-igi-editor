$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'editor-e2e.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
$function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-OrbitDelta' }, $true)
if (-not $function) { throw 'Missing orbit mapping' }
Invoke-Expression $function.Extent.Text
function Fail($message) { throw $message }
$back = Get-OrbitDelta 'back' 12
$right = Get-OrbitDelta 'right' 12
$left = Get-OrbitDelta 'left' 12
if ($back.yawPixels -eq $right.yawPixels) { throw 'Back and right must be different camera azimuths.' }
if ([Math]::Abs($back.yawPixels - 2 * $right.yawPixels) -gt 1) { throw 'Back must rotate twice as far as right.' }
if ($left.yawPixels -ne -$right.yawPixels) { throw 'Left and right must be opposite quarter turns.' }
foreach ($name in @('front-left','front-right','back-left','back-right')) {
    if ((Get-OrbitDelta $name 12).pitchPixels -ne 0) { throw "Horizontal diagonal $name must not change elevation." }
}
'PASS: orbit azimuth mapping'
