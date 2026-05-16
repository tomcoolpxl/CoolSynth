# Splice the generated snippet into src/presets/FactoryPresets.cpp.
# Idempotent: re-running replaces any prior auto-generated block.

$ErrorActionPreference = 'Stop'

$cppPath     = Join-Path $PSScriptRoot '..\src\presets\FactoryPresets.cpp'
$snippetPath = Join-Path $PSScriptRoot '..\build_aux\factory_presets_snippet.txt'

$cppRaw     = Get-Content -Path $cppPath -Raw
$snippetRaw = Get-Content -Path $snippetPath -Raw

# Detect existing line ending (default CRLF on Windows).
$nl = if ($cppRaw -match "`r`n") { "`r`n" } else { "`n" }

# Normalize both to LF for string matching, restore later.
$cpp     = $cppRaw     -replace "`r`n", "`n"
$snippet = $snippetRaw -replace "`r`n", "`n"

# Split snippet into arrays block and registrations block at the marker.
$marker = "        // --- append to kPresets[] ---"
$idx = $snippet.IndexOf($marker)
if ($idx -lt 0) { throw "Marker not found in snippet" }

$arraysBlock = $snippet.Substring(0, $idx).TrimEnd("`n")
$regsBlock   = $snippet.Substring($idx + $marker.Length).TrimStart("`n").TrimEnd("`n")

# Strip any prior auto-generated arrays section.
$startTag = "        // === BEGIN AUTO YAML PRESETS ==="
$endTag   = "        // === END AUTO YAML PRESETS ==="
$si = $cpp.IndexOf($startTag)
$ei = $cpp.IndexOf($endTag)
if ($si -ge 0 -and $ei -gt $si) {
    $eolAfter = $cpp.IndexOf("`n", $ei) + 1
    $cpp = $cpp.Substring(0, $si) + $cpp.Substring($eolAfter)
}

# Strip any prior auto-generated list section.
$lstart = "            // === BEGIN AUTO YAML PRESET LIST ==="
$lend   = "            // === END AUTO YAML PRESET LIST ==="
$lsi = $cpp.IndexOf($lstart)
$lei = $cpp.IndexOf($lend)
if ($lsi -ge 0 -and $lei -gt $lsi) {
    $eolAfter = $cpp.IndexOf("`n", $lei) + 1
    $cpp = $cpp.Substring(0, $lsi) + $cpp.Substring($eolAfter)
}

# Insert arrays block right after the kArpBliss[] closing brace.
$arraysAnchor = "        };`n`n        template <int N>"
if (-not $cpp.Contains($arraysAnchor)) { throw "Could not locate arrays anchor" }

$replacement = "        };`n`n" + $startTag + "`n" + $arraysBlock + "`n" + $endTag + "`n`n        template <int N>"
$cpp = $cpp.Replace($arraysAnchor, $replacement)

# Append the new makeFactoryPreset(...) lines inside kPresets[].
$listAnchor = '            makeFactoryPreset("Arp Bliss",       "Arp",   kArpBliss),'
if (-not $cpp.Contains($listAnchor)) { throw "Could not locate kPresets list anchor" }

$listInsertion = $listAnchor + "`n" + $lstart + "`n" + $regsBlock + "`n" + $lend
$cpp = $cpp.Replace($listAnchor, $listInsertion)

# Restore the file's original line ending.
$cpp = $cpp -replace "`n", $nl

Set-Content -Path $cppPath -Value $cpp -NoNewline
Write-Host "Spliced presets into FactoryPresets.cpp (line ending = $($nl.Length) bytes)"
