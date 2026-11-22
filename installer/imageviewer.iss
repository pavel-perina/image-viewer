; Windows installer. Run through build-installer.ps1, which passes AppVersion and StageDir.
#ifndef AppVersion
  #error Run this through installer/build-installer.ps1
#endif

[Setup]
; Identifies the product to Windows across versions. Never change it.
AppId={{7E3A9C52-4B1D-4F6A-9E27-1C8D5A0B3F64}
AppName=Image Viewer
AppVersion={#AppVersion}
DefaultDirName={autopf}\Image Viewer
DisableProgramGroupPage=yes
; Per user: no admin prompt, and {autopf} is the user's own Programs folder.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=ImageViewer-{#AppVersion}-setup
UninstallDisplayIcon={app}\imageviewer.exe
SetupIconFile=..\src\app\windows\imageviewer.ico
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Image Viewer"; Filename: "{app}\imageviewer.exe"
Name: "{autodesktop}\Image Viewer"; Filename: "{app}\imageviewer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\imageviewer.exe"; Description: "{cm:LaunchProgram,Image Viewer}"; Flags: nowait postinstall skipifsilent
