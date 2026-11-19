# Reads CoolSynth's window through Windows UI Automation, as a screen reader does: the knobs, faders, pads and
# buttons with their names, kinds and values; with -Press, presses the named element (its Invoke pattern).
# Run with Windows PowerShell 5.1 (powershell.exe), which has the UI Automation client.
param([string]$Press = '')
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$proc = Get-Process -Name CoolSynth -ErrorAction Stop | Select-Object -First 1
$A = [System.Windows.Automation.AutomationElement]
$byProcess = New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, $proc.Id)
$window = $A::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $byProcess)
if ($window -eq $null) { 'No CoolSynth window found.'; exit 1 }
"Window: " + $window.Current.Name
$all = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
foreach ($e in $all) {
    $name = $e.Current.Name
    if ($name -notmatch '^(Knob|Main knob|Fader|Pad|Prefs|Hold|Oct)') { continue }
    $value = ''
    $pattern = $null
    if ($e.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) { $value = $pattern.Current.Value }
    elseif ($e.TryGetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$pattern)) { $value = [string]$pattern.Current.Value }
    $kind = $e.Current.ControlType.ProgrammaticName -replace 'ControlType\.', ''
    $help = $e.Current.HelpText
    "{0,-34} {1,-8} {2}{3}" -f $name, $kind, $value, $(if ($help) { "  [$help]" } else { '' })
    if ($Press -ne '' -and $name -eq $Press) {
        $invoke = $null
        if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) { $invoke.Invoke(); "  -> pressed $name" }
        else { "  -> $name has no Invoke pattern" }
    }
}
