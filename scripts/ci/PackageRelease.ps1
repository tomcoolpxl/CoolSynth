param(
    [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [string]$TagName = ''
)

. "$PSScriptRoot/Common.ps1"

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$context = New-CoolSynthBuildContext -SourceRoot $sourceRoot -Configuration $Configuration -TagName $TagName
$artifacts = Get-CoolSynthArtifactPaths -BuildContext $context
$packages = New-CoolSynthReleasePackages -BuildContext $context -ArtifactPaths $artifacts

$checksumPath = Join-Path $context.PackageDirectory "$($context.ProjectName)-windows-x64-sha256-$($context.TagName).txt"
$manifestPath = Join-Path $context.PackageDirectory 'release-manifest.json'

Write-CoolSynthChecksumFile -PackagedAssets $packages -DestinationPath $checksumPath
Write-CoolSynthReleaseManifest -BuildContext $context -PackagedAssets $packages -DestinationPath $manifestPath

Write-Host "Packaged assets:"
foreach ($package in $packages) {
    Write-Host " - $($package.FileName)"
}
Write-Host " - $(Split-Path -Path $checksumPath -Leaf)"
Write-Host " - $(Split-Path -Path $manifestPath -Leaf)"
