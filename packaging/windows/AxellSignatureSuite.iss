; Axell Signature Suite for Windows: the Dojo app and the Axell plugins (64-bit VST3) in one installer.
; Axell 76 is staged from its own release like the eight others.
; Built by .github/workflows/windows-suite.yml (Inno Setup 6), which stages the files first:
;   staging\app\         Axell Signature Dojo.exe
;   staging\plugins\     <Name>.vst3 bundles, from each plugin's published Windows release (checksums verified)
;   staging\licenses\    each product's LICENSE / notices
; Plugins go to Common Files\VST3, the folder every Windows DAW scans; the Dojo updates them later.
#ifndef SuiteVersion
  #define SuiteVersion "0.0.0"
#endif
#ifndef Staging
  #define Staging "..\..\staging"
#endif

[Setup]
AppId={{1C373E39-ACC9-447F-A0D6-34816DFD4F06}
AppName=Axell Signature Suite
AppVersion={#SuiteVersion}
AppPublisher=Axell Yobag (Zmardan)
AppPublisherURL=https://github.com/zmardanian
AppSupportURL=https://github.com/zmardanian/axell-signature-dojo
AppContact=contact@axellsdojo.com
DefaultDirName={autopf}\Axell Signature Dojo
DefaultGroupName=Axell Signature
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=AxellSignatureSuite-{#SuiteVersion}-windows-x64-setup
SetupIconFile=dojo.ico
UninstallDisplayIcon={app}\Axell Signature Dojo.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile={#Staging}\licenses\LICENSE-Dojo.txt

[Types]
Name: "full"; Description: "The Dojo and every plugin"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "dojo"; Description: "Axell Signature Dojo (installs, updates and tunes the suite)"; Types: full custom
Name: "plugins"; Description: "Plugins (VST3)"; Types: full custom
; components.iss and files.iss are written by the workflow: one component per staged plugin
#include "components.iss"

[Files]
Source: "{#Staging}\app\*"; DestDir: "{app}"; Components: dojo; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Staging}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion
#include "files.iss"

[Icons]
Name: "{group}\Axell Signature Dojo"; Filename: "{app}\Axell Signature Dojo.exe"; Components: dojo
Name: "{autodesktop}\Axell Signature Dojo"; Filename: "{app}\Axell Signature Dojo.exe"; Components: dojo; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the Dojo"; Components: dojo

[Run]
Filename: "{app}\Axell Signature Dojo.exe"; Description: "Open Axell Signature Dojo"; Components: dojo; Flags: nowait postinstall skipifsilent
