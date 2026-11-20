[Setup]
AppId=Nivora.Desktop
AppName=Nivora
AppVersion=1.1.0
AppPublisher=Nivora contributors
DefaultDirName={localappdata}\Programs\Nivora
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
WizardStyle=modern
SetupIconFile=assets\state0.ico
UninstallDisplayIcon={app}\Nivora.exe
OutputDir=dist
OutputBaseFilename=Nivora-Setup-1.1.0-x64
Compression=lzma2
SolidCompression=yes
AppMutex=Local\Nivora-Launcher-v1
[Files]
Source: "dist\Nivora.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"
Source: "NOTICE"; DestDir: "{app}"
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
[Icons]
Name: "{userprograms}\Nivora"; Filename: "{app}\Nivora.exe"
Name: "{userdesktop}\Nivora"; Filename: "{app}\Nivora.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\Nivora.exe"; Description: "Open Nivora"; Flags: nowait postinstall skipifsilent
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Nivora"; Flags: dontcreatekey uninsdeletevalue
