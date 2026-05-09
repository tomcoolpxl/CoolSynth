param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [object]$RunTests = $false
)

. "$PSScriptRoot/Common.ps1"

function ConvertTo-CoolSynthBoolean {
    param(
        [Parameter(Mandatory)][object]$Value
    )

    if ($Value -is [bool]) {
        return $Value
    }

    if ($Value -is [byte] -or $Value -is [int] -or $Value -is [long]) {
        return [bool]$Value
    }

    if ($Value -is [string]) {
        switch -Regex ($Value.Trim()) {
            '^(?i:true|1)$' { return $true }
            '^(?i:false|0|)$' { return $false }
        }
    }

    throw "Unsupported RunTests value: $Value"
}

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$context = New-CoolSynthBuildContext -SourceRoot $sourceRoot -Configuration $Configuration
$shouldRunTests = ConvertTo-CoolSynthBoolean -Value $RunTests

Write-Host "Runner image: $($context.RunnerImage)"
Write-Host "Configuration: $($context.Configuration)"
Write-Host "Configure preset: $($context.ConfigurePreset)"
Write-Host "Build preset: $($context.BuildPreset)"

New-Item -ItemType Directory -Path $context.LogsDirectory -Force | Out-Null

Invoke-CoolSynthBootstrap -SourceRoot $context.SourceRoot
Invoke-CoolSynthConfigure -BuildContext $context
Invoke-CoolSynthBuild -BuildContext $context

if ($shouldRunTests) {
    Invoke-CoolSynthTests -BuildContext $context
}
