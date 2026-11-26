[Setup]
AppId=AquaFrame.Browser
AppName=Aqua Frame
AppVersion=1.0.0
DefaultDirName={localappdata}\Programs\Aqua Frame
DefaultGroupName=Aqua Frame
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=AquaFrame-Setup
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "..\dist\runtime\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Aqua Frame"; Filename: "{app}\AquaFrame.exe"

[Run]
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /grant ""*S-1-15-2-2:(OI)(CI)(RX)"""; Flags: runhidden waituntilterminated
