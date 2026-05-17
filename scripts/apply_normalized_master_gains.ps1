#Requires -Version 7
# Applies normalized masterGainDb values from preset_loudness.csv into
# src/presets/FactoryPresets.cpp.  Matches Nth masterGainDb occurrence to Nth
# CSV row, mirroring the order in which presets appear in the kPresets table.

[CmdletBinding()]
param(
    [string] $CsvPath        = (Join-Path $PSScriptRoot '..\build\preset_loudness.csv'),
    [string] $FactoryCppPath = (Join-Path $PSScriptRoot '..\src\presets\FactoryPresets.cpp')
)

$ErrorActionPreference = 'Stop'

$csv = Import-Csv -Path $CsvPath
Write-Host "Loaded $($csv.Count) normalization rows from $CsvPath"

$source = Get-Content -Path $FactoryCppPath -Raw

$pattern = 'ids::masterGainDb,\s*-?[0-9]+(?:\.[0-9]+)?f'
$matches = [regex]::Matches($source, $pattern)
Write-Host "Found $($matches.Count) masterGainDb occurrences in $FactoryCppPath"

if ($matches.Count -ne $csv.Count)
{
    throw "Mismatch: CSV has $($csv.Count) rows but source has $($matches.Count) masterGainDb lines."
}

# Replace right-to-left so earlier offsets stay valid.
$buffer = [System.Text.StringBuilder]::new($source)
for ($i = $matches.Count - 1; $i -ge 0; $i--)
{
    $row     = $csv[$i]
    $newDb   = [double] $row.new_master_db
    # Print with at most one decimal, drop trailing ".0" only when integer.
    $newText = if ([math]::Abs($newDb - [math]::Round($newDb)) -lt 1e-6) {
        "ids::masterGainDb, $([int][math]::Round($newDb)).0f"
    } else {
        "ids::masterGainDb, {0}f" -f ([math]::Round($newDb, 1))
    }
    [void] $buffer.Remove($matches[$i].Index, $matches[$i].Length)
    [void] $buffer.Insert($matches[$i].Index, $newText)
}

Set-Content -Path $FactoryCppPath -Value $buffer.ToString() -NoNewline
Write-Host "Rewrote masterGainDb values in $FactoryCppPath"
