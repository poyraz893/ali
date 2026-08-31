#define MyAppName "Windows 11 Explorer Alfabe Çubuğu"
#define MyAppVersion "2.0.0"
#define MyAppExeName "Explorer-Alfabe-Cubugu.exe"

[Setup]
AppId={{B417953E-482A-4B39-93CE-5B1C2F771A5A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\Explorer Alfabe Çubuğu
DefaultGroupName=Explorer Alfabe Çubuğu
OutputDir=dist
OutputBaseFilename=Explorer-Alfabe-Cubugu-Kurulum-v2.0.0
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern

[Files]
Source: "dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Explorer Alfabe Çubuğu"; Filename: "{app}\{#MyAppExeName}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "ExplorerAlfabeCubugu"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Alfabe çubuğunu başlat"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /F /IM {#MyAppExeName}"; Flags: runhidden; RunOnceId: "StopExplorerAlphabet"
