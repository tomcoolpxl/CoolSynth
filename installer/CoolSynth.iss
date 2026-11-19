; CoolSynth's Windows installer (D209), built with Inno Setup 6.5 or newer:
;   ISCC /DAppVersion=0.3.0 /DBuildDir=..\build\CoolSynth_artefacts\Release installer\CoolSynth.iss
; It installs the VST3 where every host looks (Common Files\VST3), the app with a Start menu entry, and,
; for each instrument family ticked, downloads that family's zip from the Hugging Face dataset, checks
; its SHA-256 and unpacks it into C:\ProgramData\CoolSynth\Library, where CoolSynth reads it
; (SampleLibrary::defaultRoot). The User folder in it, the player's own sounds, is never touched.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\CoolSynth_artefacts\Release"
#endif

[Setup]
; Fixed for every version, so a new installer updates the one already there.
AppId={{BAFEBEB6-B552-4AE6-A4D3-195312258ED1}
AppName=CoolSynth
AppVersion={#AppVersion}
AppVerName=CoolSynth {#AppVersion}
AppPublisher=tomcoolpxl
AppPublisherURL=https://github.com/tomcoolpxl/CoolSynth
AppSupportURL=https://github.com/tomcoolpxl/CoolSynth/issues
AppUpdatesURL=https://github.com/tomcoolpxl/CoolSynth/releases
DefaultDirName={autopf}\CoolSynth
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename=CoolSynth-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; .zip needs the full extraction method (Inno Setup 6.5).
ArchiveExtraction=full
UninstallDisplayName=CoolSynth
; The MiniLab scripts go in the player's Documents (FL Studio and Ableton read them there): an admin install
; writing a user's folder is what this warning is about, and it is meant.
UsedUserAreasWarning=no
SetupIconFile=CoolSynth.ico
UninstallDisplayIcon={app}\CoolSynth.exe
VersionInfoVersion={#AppVersion}
VersionInfoProductName=CoolSynth

[Types]
Name: "full"; Description: "Everything: the VST3, the app and all recorded instruments"
Name: "compact"; Description: "The VST3 and the app, without the recorded instruments"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin, to play CoolSynth in your music software (DAW)"; Types: full compact custom
Name: "app"; Description: "Standalone app"; Types: full compact custom
Name: "minilab"; Description: "Arturia MiniLab 3 support in FL Studio and Ableton Live"; Types: full compact custom

[Files]
Source: "{#BuildDir}\VST3\CoolSynth.vst3\*"; DestDir: "{commoncf64}\VST3\CoolSynth.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\CoolSynth.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Components: app; Flags: ignoreversion isreadme
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
; The MiniLab 3 scripts (D211), in the player's own Documents where FL Studio and Ableton Live look for them. FL links
; its script to the MiniLab by itself; in Live it is chosen once as a control surface. Harmless without a MiniLab.
Source: "..\resources\daw-scripts\fl-studio\CoolSynth MiniLab\*.py"; DestDir: "{userdocs}\Image-Line\FL Studio\Settings\Hardware\CoolSynth MiniLab"; Components: minilab; Flags: ignoreversion
Source: "..\resources\daw-scripts\ableton\CoolSynth_MiniLab\*.py"; DestDir: "{userdocs}\Ableton\User Library\Remote Scripts\CoolSynth_MiniLab"; Components: minilab; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\CoolSynth"; Filename: "{app}\CoolSynth.exe"; Components: app

[Run]
Filename: "{app}\CoolSynth.exe"; Description: "Start CoolSynth"; Components: app; Flags: nowait postinstall skipifsilent

; The instrument families: components, downloads and what uninstalling removes (generated).
#include "Instruments.iss"
