; -- Milton.iss --


[Setup]
AppName=Milton
AppVersion=1.2.5
DefaultDirName={pf}\Milton
DefaultGroupName=Milton
;UninstallDisplayIcon={app}\Milton.exe
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=MiltonSetup_1.2.5
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Files]
Source: "Milton.exe"; DestDir: "{app}"
Source: "Carlito.ttf"; DestDir: "{app}"
Source: "Carlito.LICENSE"; DestDir: "{app}"
Source: "LICENSE.txt"; DestDir: "{app}"

[Icons]
Name: "{group}\Milton"; Filename: "{app}\Milton.exe"
