# Copies CoolSynth's MiniLab 3 scripts (D211) from the install folder into the player's own Documents, where FL Studio
# and Ableton Live look for them. The installer runs this as the player who started it (runasoriginaluser), not as the
# administrator it elevated to, so the scripts land in that player's folders even when someone else typed the admin
# password. GetFolderPath follows a Documents folder moved elsewhere, OneDrive's included.
$ErrorActionPreference = 'Stop'
$documents = [Environment]::GetFolderPath('MyDocuments')
$places = @(
    @{ From = 'fl-studio\CoolSynth MiniLab'; To = 'Image-Line\FL Studio\Settings\Hardware\CoolSynth MiniLab' },
    @{ From = 'ableton\CoolSynth_MiniLab'; To = 'Ableton\User Library\Remote Scripts\CoolSynth_MiniLab' }
)
foreach ($place in $places) {
    $to = Join-Path $documents $place.To
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Copy-Item -Path (Join-Path (Join-Path $PSScriptRoot $place.From) '*.py') -Destination $to -Force
}
