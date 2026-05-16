# One-shot generator: parses coolsynth_30_presets_with_scifi_additions.yaml
# and emits C++ constexpr arrays + a list of array names for FactoryPresets.cpp.
# Run from repo root: pwsh -NoProfile -File scripts/generate_factory_presets.ps1

$ErrorActionPreference = 'Stop'

$yamlPath = Join-Path $PSScriptRoot '..\coolsynth_30_presets_with_scifi_additions.yaml'
$outPath  = Join-Path $PSScriptRoot '..\build_aux\factory_presets_snippet.txt'
$auxDir   = Split-Path $outPath -Parent
if (-not (Test-Path $auxDir)) { New-Item -ItemType Directory -Path $auxDir | Out-Null }

# Parameter id order (must match the existing 15 presets and the YAML key set).
$paramOrder = @(
    'oscAWave','oscAOctave','oscAFineCents','oscALevel','oscAPulseWidth','oscASyncEnabled',
    'oscBWave','oscBOctave','oscBFineCents','oscBLevel','oscBPulseWidth','oscBLowFrequencyMode',
    'noiseLevel',
    'filterCutoffHz','filterResonance','filterEnvAmount','filterKeyTracking',
    'filterAttackMs','filterDecayMs','filterSustain','filterReleaseMs',
    'ampAttackMs','ampDecayMs','ampSustain','ampReleaseMs',
    'lfoRateHz','lfoWave','lfoToOscPitch','lfoToPulseWidth','lfoToFilterCutoff','modWheelToLfoDepth',
    'polyModOscBToOscPitch','polyModEnvToOscPitch','polyModOscBToPulseWidth','polyModEnvToPulseWidth',
    'polyModOscBToFilterCutoff','polyModEnvToFilterCutoff',
    'glideTimeMs','playMode','keyPriority','pitchBendRangeSemitones',
    'vintageAmount','panSpread','velocityToAmp','velocityToFilter',
    'arpEnabled','arpInternalTempoBpm','arpRateDivision','arpPattern','arpOctaveRange','arpGate','arpLatch',
    'driveEnabled','driveAmount','driveMix',
    'chorusEnabled','chorusRateHz','chorusDepth','chorusMix',
    'delayEnabled','delayTimeMs','delayFeedback','delayMix',
    'reverbEnabled','reverbSize','reverbDamping','reverbMix',
    'masterGainDb'
)

# Param-id -> identifier slug used in C++ symbol generation.
function Make-Slug([string]$name) {
    $clean = ($name -replace '[^A-Za-z0-9]', ' ').Trim()
    $parts = $clean -split '\s+' | Where-Object { $_ }
    ($parts | ForEach-Object { $_.Substring(0,1).ToUpper() + $_.Substring(1) }) -join ''
}

function Format-Float([string]$raw) {
    $t = $raw.Trim()
    if ($t -eq 'true')  { return '1.0f' }
    if ($t -eq 'false') { return '0.0f' }
    $v = [double]::Parse($t, [System.Globalization.CultureInfo]::InvariantCulture)
    # Always emit with at least one decimal and trailing 'f'.
    $s = $v.ToString('0.0##############', [System.Globalization.CultureInfo]::InvariantCulture)
    return "$s" + 'f'
}

$lines = Get-Content -Path $yamlPath
$presets = @()
$current = $null
$inParams = $false

foreach ($line in $lines) {
    if ($line -match '^name:\s*(.+?)\s*$') {
        if ($current) { $presets += ,$current }
        $current = @{ name = $Matches[1]; category = ''; description = ''; params = @{} }
        $inParams = $false
        continue
    }
    if ($line -match '^category:\s*(.+?)\s*$') { $current.category = $Matches[1]; continue }
    if ($line -match '^description:\s*(.+?)\s*$') { $current.description = $Matches[1]; continue }
    if ($line -match '^params:\s*$') { $inParams = $true; continue }
    if ($inParams -and $line -match '^\s+([A-Za-z]+):\s*(.+?)\s*$') {
        $current.params[$Matches[1]] = $Matches[2]
    }
}
if ($current) { $presets += ,$current }

$sb = [System.Text.StringBuilder]::new()
$arrayNames = @()

foreach ($p in $presets) {
    $slug = 'k' + (Make-Slug $p.name)
    $arrayNames += ,@($slug, $p.name, $p.category)
    [void]$sb.AppendLine("        constexpr PresetParameterValue $slug[] = {")
    foreach ($id in $paramOrder) {
        if (-not $p.params.ContainsKey($id)) {
            throw "Preset '$($p.name)' missing parameter '$id'"
        }
        $val = Format-Float $p.params[$id]
        [void]$sb.AppendLine("            { ids::$id, $val },")
    }
    [void]$sb.AppendLine('        };')
    [void]$sb.AppendLine('')
}

[void]$sb.AppendLine('        // --- append to kPresets[] ---')
foreach ($entry in $arrayNames) {
    $slug, $name, $cat = $entry
    $nameLit = '"' + $name + '",'
    $catLit  = '"' + $cat  + '",'
    $namePad = $nameLit.PadRight(22)
    $catPad  = $catLit.PadRight(10)
    [void]$sb.AppendLine("            makeFactoryPreset($namePad $catPad $slug),")
}

Set-Content -Path $outPath -Value $sb.ToString()
Write-Host "Wrote $outPath with $($presets.Count) presets."
