; Inno Setup script for the Windows installer.
;   iscc /DAppVersion=5.0.0 /DDeployDir=C:\path\to\deploy\bin packaging\windows\sondepredict.iss
; DeployDir is the folder prepared by `cmake --install` + windeployqt (see .github/workflows/release.yml).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef DeployDir
  #error DeployDir must be defined (/DDeployDir=...)
#endif

[Setup]
AppId={{8C4E1B0E-5D7A-4F0B-9E7A-2B6C3D9F1A42}
AppName=Sonde Predict
AppVersion={#AppVersion}
AppVerName=Sonde Predict {#AppVersion}
DefaultDirName={autopf}\Sonde Predict
DefaultGroupName=Sonde Predict
DisableProgramGroupPage=yes
OutputBaseFilename=SondePredict-{#AppVersion}-windows-x64-setup
OutputDir=..\..\dist
SetupIconFile=..\icons\sondepredict.ico
UninstallDisplayIcon={app}\sondepredict.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Install for the current user without admin rights unless the user chooses otherwise.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Sonde Predict"; Filename: "{app}\sondepredict.exe"
Name: "{autodesktop}\Sonde Predict"; Filename: "{app}\sondepredict.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\sondepredict.exe"; Description: "{cm:LaunchProgram,Sonde Predict}"; Flags: nowait postinstall skipifsilent
