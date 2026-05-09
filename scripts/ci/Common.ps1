Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PathExists {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
}

function New-CoolSynthBuildContext {
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
        [string]$TagName = ''
    )

    $resolvedSourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
    $configurationKey = $Configuration.ToLowerInvariant()
    $buildDirectory = Join-Path $resolvedSourceRoot "build/ci-$configurationKey"
    $effectiveTagName = if ([string]::IsNullOrWhiteSpace($TagName)) {
        "manual-$configurationKey"
    } else {
        $TagName
    }

    [pscustomobject]@{
        SourceRoot        = $resolvedSourceRoot
        Configuration     = $Configuration
        ConfigurePreset   = "ci-$configurationKey"
        BuildPreset       = "ci-build-$configurationKey"
        BuildDirectory    = $buildDirectory
        LogsDirectory     = Join-Path $buildDirectory 'logs'
        ProjectName       = 'CoolSynth'
        TagName           = $effectiveTagName
        PackageDirectory  = Join-Path $buildDirectory 'packages'
        TestResults       = Join-Path $buildDirectory 'Testing/ctest-results.xml'
        RunnerImage       = if ($env:COOLSYNTH_RUNNER_IMAGE) { $env:COOLSYNTH_RUNNER_IMAGE } elseif ($env:ImageOS) { $env:ImageOS } else { 'local' }
        CommitSha         = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { '' }
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][scriptblock]$ScriptBlock
    )

    $logDirectory = Split-Path -Path $LogPath -Parent
    if (-not [string]::IsNullOrWhiteSpace($logDirectory)) {
        New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    }

    & $ScriptBlock 2>&1 | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed. See log: $LogPath"
    }
}

function Invoke-CoolSynthBootstrap {
    param(
        [Parameter(Mandatory)][string]$SourceRoot
    )

    $juceRoot = Join-Path $SourceRoot 'external/JUCE'
    $juceCMake = Join-Path $juceRoot 'CMakeLists.txt'

    if ($env:GITHUB_ACTIONS -ne 'true') {
        Assert-PathExists -Path $juceCMake -Description 'JUCE submodule checkout'
        Write-Host "Using existing JUCE submodule checkout at $juceRoot"
        return
    }

    Push-Location $SourceRoot
    try {
        & git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) {
            throw 'git submodule update --init --recursive failed.'
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-CoolSynthConfigure {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )

    $logPath = Join-Path $BuildContext.LogsDirectory 'configure.log'
    Invoke-LoggedCommand -LogPath $logPath -ScriptBlock {
        Push-Location $BuildContext.SourceRoot
        try {
            & cmake --preset $BuildContext.ConfigurePreset
        }
        finally {
            Pop-Location
        }
    }
}

function Invoke-CoolSynthBuild {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )

    $logPath = Join-Path $BuildContext.LogsDirectory 'build.log'
    Invoke-LoggedCommand -LogPath $logPath -ScriptBlock {
        Push-Location $BuildContext.SourceRoot
        try {
            & cmake --build --preset $BuildContext.BuildPreset --config $BuildContext.Configuration
        }
        finally {
            Pop-Location
        }
    }
}

function Invoke-CoolSynthTests {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )

    $testingDirectory = Split-Path -Path $BuildContext.TestResults -Parent
    New-Item -ItemType Directory -Path $testingDirectory -Force | Out-Null

    $logPath = Join-Path $BuildContext.LogsDirectory 'ctest.log'
    Invoke-LoggedCommand -LogPath $logPath -ScriptBlock {
        Push-Location $BuildContext.SourceRoot
        try {
            & ctest --test-dir $BuildContext.BuildDirectory `
                -C $BuildContext.Configuration `
                --output-on-failure `
                --output-junit $BuildContext.TestResults
        }
        finally {
            Pop-Location
        }
    }

    Assert-PathExists -Path $BuildContext.TestResults -Description 'CTest JUnit results'
}

function Get-CoolSynthArtifactPaths {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext
    )

    $artifactRoot = Join-Path $BuildContext.BuildDirectory "$($BuildContext.ProjectName)_artefacts/$($BuildContext.Configuration)"
    $standaloneDirectory = Join-Path $artifactRoot 'Standalone'
    $standaloneExe = Join-Path $standaloneDirectory "$($BuildContext.ProjectName).exe"
    $standalonePdb = Join-Path $standaloneDirectory "$($BuildContext.ProjectName).pdb"
    $vst3Bundle = Join-Path (Join-Path $artifactRoot 'VST3') "$($BuildContext.ProjectName).vst3"

    Assert-PathExists -Path $standaloneExe -Description 'Standalone executable'
    Assert-PathExists -Path $vst3Bundle -Description 'VST3 bundle'
    Assert-PathExists -Path $BuildContext.TestResults -Description 'CTest JUnit results'

    [pscustomobject]@{
        StandaloneDirectory = $standaloneDirectory
        StandaloneExe       = $standaloneExe
        StandalonePdb       = if (Test-Path -LiteralPath $standalonePdb) { $standalonePdb } else { $null }
        Vst3Bundle          = $vst3Bundle
        TestResults         = $BuildContext.TestResults
    }
}

function New-CoolSynthReleasePackages {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext,
        [Parameter(Mandatory)][pscustomobject]$ArtifactPaths
    )

    if (Test-Path -LiteralPath $BuildContext.PackageDirectory) {
        Remove-Item -LiteralPath $BuildContext.PackageDirectory -Recurse -Force
    }

    New-Item -ItemType Directory -Path $BuildContext.PackageDirectory -Force | Out-Null

    $stagingRoot = Join-Path $BuildContext.PackageDirectory '.staging'
    $standaloneStage = Join-Path $stagingRoot 'standalone'
    $vst3Stage = Join-Path $stagingRoot 'vst3'
    New-Item -ItemType Directory -Path $standaloneStage -Force | Out-Null
    New-Item -ItemType Directory -Path $vst3Stage -Force | Out-Null

    Get-ChildItem -LiteralPath $ArtifactPaths.StandaloneDirectory -Force |
        Where-Object { $_.Name -ne "$($BuildContext.ProjectName).pdb" } |
        Copy-Item -Destination $standaloneStage -Recurse -Force

    Copy-Item -LiteralPath $ArtifactPaths.Vst3Bundle -Destination $vst3Stage -Recurse -Force

    $standaloneZip = Join-Path $BuildContext.PackageDirectory "$($BuildContext.ProjectName)-windows-x64-standalone-$($BuildContext.TagName).zip"
    $vst3Zip = Join-Path $BuildContext.PackageDirectory "$($BuildContext.ProjectName)-windows-x64-vst3-$($BuildContext.TagName).zip"

    $standalonePayload = @(Get-ChildItem -LiteralPath $standaloneStage -Force | Select-Object -ExpandProperty FullName)
    if ($standalonePayload.Count -eq 0) {
        throw "Standalone package staging directory is empty: $standaloneStage"
    }

    Compress-Archive -Path $standalonePayload -DestinationPath $standaloneZip -CompressionLevel Optimal -Force
    Compress-Archive -Path (Join-Path $vst3Stage "$($BuildContext.ProjectName).vst3") -DestinationPath $vst3Zip -CompressionLevel Optimal -Force

    Assert-PathExists -Path $standaloneZip -Description 'Standalone zip'
    Assert-PathExists -Path $vst3Zip -Description 'VST3 zip'

    Remove-Item -LiteralPath $stagingRoot -Recurse -Force

    @(
        [pscustomobject]@{
            LogicalName = 'standalone'
            FileName    = Split-Path -Path $standaloneZip -Leaf
            FilePath    = $standaloneZip
            Sha256      = (Get-FileHash -LiteralPath $standaloneZip -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        [pscustomobject]@{
            LogicalName = 'vst3'
            FileName    = Split-Path -Path $vst3Zip -Leaf
            FilePath    = $vst3Zip
            Sha256      = (Get-FileHash -LiteralPath $vst3Zip -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    )
}

function Write-CoolSynthChecksumFile {
    param(
        [Parameter(Mandatory)][array]$PackagedAssets,
        [Parameter(Mandatory)][string]$DestinationPath
    )

    $lines = foreach ($asset in $PackagedAssets) {
        '{0} *{1}' -f $asset.Sha256, $asset.FileName
    }

    Set-Content -LiteralPath $DestinationPath -Value $lines -Encoding ascii
    Assert-PathExists -Path $DestinationPath -Description 'Checksum file'
}

function Write-CoolSynthReleaseManifest {
    param(
        [Parameter(Mandatory)][pscustomobject]$BuildContext,
        [Parameter(Mandatory)][array]$PackagedAssets,
        [Parameter(Mandatory)][string]$DestinationPath
    )

    $standaloneAsset = $PackagedAssets | Where-Object { $_.LogicalName -eq 'standalone' } | Select-Object -First 1
    $vst3Asset = $PackagedAssets | Where-Object { $_.LogicalName -eq 'vst3' } | Select-Object -First 1
    $checksumFileName = "$($BuildContext.ProjectName)-windows-x64-sha256-$($BuildContext.TagName).txt"

    $manifest = [pscustomobject]@{
        TagName         = $BuildContext.TagName
        CommitSha       = $BuildContext.CommitSha
        RunnerImage     = $BuildContext.RunnerImage
        Configuration   = $BuildContext.Configuration
        StandaloneAsset = $standaloneAsset.FileName
        Vst3Asset       = $vst3Asset.FileName
        ChecksumFile    = $checksumFileName
        GeneratedAtUtc  = (Get-Date).ToUniversalTime().ToString('o')
    }

    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $DestinationPath -Encoding utf8
    Assert-PathExists -Path $DestinationPath -Description 'Release manifest'
}

function Test-CoolSynthPrereleaseTag {
    param(
        [Parameter(Mandatory)][string]$TagName
    )

    return $TagName -match '^v\d+\.\d+\.\d+-'
}
