; Windows installer. Run through build-installer.ps1, which passes AppVersion and StageDir.
#ifndef AppVersion
  #error Run this through installer/build-installer.ps1
#endif

; The extensions the program reads, as supportedExtensions() in src/core/image_source.cpp lists
; them. Formats.TheWindowsInstallerOffersWhatItReads keeps the two the same.
#dim Ext[20]
#define Ext[0] "jpg"
#define Ext[1] "jpeg"
#define Ext[2] "jpe"
#define Ext[3] "jfif"
#define Ext[4] "tif"
#define Ext[5] "tiff"
#define Ext[6] "bmp"
#define Ext[7] "dib"
#define Ext[8] "pbm"
#define Ext[9] "pgm"
#define Ext[10] "ppm"
#define Ext[11] "pnm"
#define Ext[12] "pam"
#define Ext[13] "pfm"
#define Ext[14] "tga"
#define Ext[15] "gif"
#define Ext[16] "png"
#define Ext[17] "webp"
#define Ext[18] "avif"
#define Ext[19] "jxl"

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
; Tells Explorer to read the types again, after install and uninstall, for Open with.
ChangesAssociations=yes
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked
Name: "openwith"; Description: "Offer Image Viewer in Open with and in Default apps (keeps the programs pictures open with now)"

; The program offers itself for its types and never takes them: Windows lets only the user choose
; the default, in Open with or in Settings > Apps > Default apps. Everything is per user, and
; uninstalling removes only what is written here. Software\ImageViewer also holds the settings
; (QSettings), so only its Capabilities key goes, and the key itself only when empty.
#define I
#sub ExtensionEntries
Root: HKCU; Subkey: "Software\Classes\.{#Ext[I]}"; ValueType: none; Flags: uninsdeletekeyifempty; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.{#Ext[I]}\OpenWithProgids"; ValueType: string; ValueName: "ImageViewer.Image"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\Applications\imageviewer.exe\SupportedTypes"; ValueType: string; ValueName: ".{#Ext[I]}"; ValueData: ""; Tasks: openwith
Root: HKCU; Subkey: "Software\ImageViewer\Capabilities\FileAssociations"; ValueType: string; ValueName: ".{#Ext[I]}"; ValueData: "ImageViewer.Image"; Tasks: openwith
#endsub

[Registry]
; A type for pictures, which opens them here.
Root: HKCU; Subkey: "Software\Classes\ImageViewer.Image"; ValueType: string; ValueData: "Image"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\ImageViewer.Image\DefaultIcon"; ValueType: string; ValueData: """{app}\imageviewer.exe"",0"; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\ImageViewer.Image\shell\open\command"; ValueType: string; ValueData: """{app}\imageviewer.exe"" ""%1"""; Tasks: openwith
; The program by its executable's name: its name in Open with, and the types it takes.
Root: HKCU; Subkey: "Software\Classes\Applications\imageviewer.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Image Viewer"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\Applications\imageviewer.exe\shell\open\command"; ValueType: string; ValueData: """{app}\imageviewer.exe"" ""%1"""; Tasks: openwith
; Default apps lists the programs registered here, each with the types it can take.
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Image Viewer"; ValueData: "Software\ImageViewer\Capabilities"; Flags: uninsdeletevalue; Tasks: openwith
Root: HKCU; Subkey: "Software\ImageViewer"; ValueType: none; Flags: uninsdeletekeyifempty; Tasks: openwith
Root: HKCU; Subkey: "Software\ImageViewer\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Image Viewer"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\ImageViewer\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "View, browse and edit pictures"; Tasks: openwith
Root: HKCU; Subkey: "Software\ImageViewer\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\imageviewer.exe"",0"; Tasks: openwith
#for {I = 0; I < DimOf(Ext); I++} ExtensionEntries

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Image Viewer"; Filename: "{app}\imageviewer.exe"
Name: "{autodesktop}\Image Viewer"; Filename: "{app}\imageviewer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\imageviewer.exe"; Description: "{cm:LaunchProgram,Image Viewer}"; Flags: nowait postinstall skipifsilent
