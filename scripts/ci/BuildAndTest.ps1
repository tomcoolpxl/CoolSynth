param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [switch]$RunTests
)

. "$PSScriptRoot/Common.ps1"

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$context = New-CoolSynthBuildContext -SourceRoot $sourceRoot -Configuration $Configuration

Write-Host "Runner image: $($context.RunnerImage)"
Write-Host "Configuration: $($context.Configuration)"
Write-Host "Configure preset: $($context.ConfigurePreset)"
Write-Host "Build preset: $($context.BuildPreset)"

New-Item -ItemType Directory -Path $context.LogsDirectory -Force | Out-Null

Invoke-CoolSynthBootstrap -SourceRoot $context.SourceRoot
Invoke-CoolSynthConfigure -BuildContext $context
Invoke-CoolSynthBuild -BuildContext $context

if ($RunTests.IsPresent) {
    Invoke-CoolSynthTests -BuildContext $context
}
