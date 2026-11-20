; CoolSynth's Windows installer (D209), built with Inno Setup 6.5 or newer:
;   ISCC /DAppVersion=0.3.0 /DBuildDir=..\build\CoolSynth_artefacts\Release installer\CoolSynth.iss
; It installs the VST3 and the CLAP where hosts look (Common Files\VST3 and Common Files\CLAP), the app with a Start menu entry, and,
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
; No licence page: the MIT licence asks for no acceptance, only that its notice ships with copies (LICENSE.txt below).
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
; Uninstalling removes the MiniLab scripts from Documents (FL Studio and Ableton read them there): an admin install
; touching a user's folder is what this warning is about, and it is meant.
UsedUserAreasWarning=no
SetupIconFile=CoolSynth.ico
UninstallDisplayIcon={app}\CoolSynth.exe
VersionInfoVersion={#AppVersion}
VersionInfoProductName=CoolSynth

[Types]
Name: "full"; Description: "Everything: the plugins, the app and all recorded instruments"
Name: "compact"; Description: "The plugins and the app, without the recorded instruments"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin, to play CoolSynth in your music software (DAW)"; Types: full compact custom
Name: "clap"; Description: "CLAP plugin, for DAWs that support it: Bitwig, Reaper, FL Studio, Studio One"; Types: full compact custom
Name: "app"; Description: "Standalone app"; Types: full compact custom
Name: "minilab"; Description: "Arturia MiniLab 3 support in FL Studio and Ableton Live"; Types: full compact custom

[Files]
Source: "{#BuildDir}\VST3\CoolSynth.vst3\*"; DestDir: "{commoncf64}\VST3\CoolSynth.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; The CLAP version (D216): a controller's knobs follow the panel's page. Common Files\CLAP is where CLAP hosts look.
Source: "{#BuildDir}\CLAP\CoolSynth.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#BuildDir}\Standalone\CoolSynth.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
; The MiniLab 3 scripts (D211). They belong in the player's own Documents, where FL Studio and Ableton Live look for
; them, but an elevated installer's {userdocs} is the administrator's: so they go in the install folder first and
; Install-MiniLabScripts.ps1 copies them on as the player who started the installer ([Run] below). FL links its script
; to the MiniLab by itself; in Live it is chosen once as a control surface. Harmless without a MiniLab.
Source: "..\resources\daw-scripts\fl-studio\CoolSynth MiniLab\*.py"; DestDir: "{app}\DAW scripts\fl-studio\CoolSynth MiniLab"; Components: minilab; Flags: ignoreversion
Source: "..\resources\daw-scripts\ableton\CoolSynth_MiniLab\*.py"; DestDir: "{app}\DAW scripts\ableton\CoolSynth_MiniLab"; Components: minilab; Flags: ignoreversion
Source: "Install-MiniLabScripts.ps1"; DestDir: "{app}\DAW scripts"; Components: minilab; Flags: ignoreversion

[InstallDelete]
; Installed by 0.3.0 test builds and no longer: the guide is a web page now.
Type: files; Name: "{app}\README.md"

[Icons]
Name: "{autoprograms}\CoolSynth"; Filename: "{app}\CoolSynth.exe"; Components: app

[UninstallDelete]
; The MiniLab scripts copied into Documents. Uninstalling runs elevated, so this reaches the Documents of the account
; that approved it: the player's own on a home PC, where they are one and the same.
Type: filesandordirs; Name: "{userdocs}\Image-Line\FL Studio\Settings\Hardware\CoolSynth MiniLab"
Type: filesandordirs; Name: "{userdocs}\Ableton\User Library\Remote Scripts\CoolSynth_MiniLab"

[Run]
; Not a postinstall choice: part of installing the MiniLab support, as the player who started the installer.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ""{app}\DAW scripts\Install-MiniLabScripts.ps1"""; StatusMsg: "Setting up the MiniLab 3 for FL Studio and Ableton Live..."; Components: minilab; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\CoolSynth.exe"; Description: "Start CoolSynth"; Components: app; Flags: nowait postinstall skipifsilent
; The guide on the web, where it shows as a page (a README in a text editor is markdown, not help).
Filename: "https://github.com/tomcoolpxl/CoolSynth#readme"; Description: "Open CoolSynth's guide on the web (setting up your keyboard and DAW)"; Flags: shellexec nowait postinstall skipifsilent unchecked

; The instrument families: components, downloads and what uninstalling removes (generated).
#include "Instruments.iss"
