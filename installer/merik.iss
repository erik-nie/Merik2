; Inno Setup-script voor het Windows-installatieprogramma van Merik Studio.
; Wordt gebouwd door .github/workflows/release.yml:
;   ISCC /DAppVersion=0.7.1 /DSourceDir=<map met "Merik Studio.exe" en de dll's> /DOutputDir=dist installer\merik.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
; AppId blijft dat van "Merik" (0.x): de installer werkt een bestaande installatie dan bij in
; plaats van er een tweede naast te zetten (zelfde map, zelfde regel in Apps & features).
AppId={{6B0F7A3E-2C41-4D58-9E73-5A1C8B2D4F90}
AppName=Merik Studio
AppVersion={#AppVersion}
AppVerName=Merik Studio {#AppVersion}
AppPublisher=Erik Nie
AppPublisherURL=https://github.com/erik-nie/Merik2
AppSupportURL=https://github.com/erik-nie/Merik2/issues
AppUpdatesURL=https://github.com/erik-nie/Merik2/releases
DefaultDirName={autopf}\Merik Studio
DefaultGroupName=Merik Studio
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Merik Studio.exe
LicenseFile={#SourceDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=Merik-Studio-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Standaard voor de huidige gebruiker (geen beheerdersrechten nodig); met de keuze in het dialoogvenster voor alle gebruikers.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Overblijfselen van "Merik" (0.x) bij een update: het oude programma en de oude snelkoppelingen.
; Instellingen en setlists staan in %APPDATA%\Merik2 en blijven staan.
Type: files; Name: "{app}\Merik.exe"
Type: files; Name: "{autoprograms}\Merik.lnk"
Type: files; Name: "{autodesktop}\Merik.lnk"

[Icons]
Name: "{autoprograms}\Merik Studio"; Filename: "{app}\Merik Studio.exe"
Name: "{autodesktop}\Merik Studio"; Filename: "{app}\Merik Studio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Merik Studio.exe"; Description: "Start Merik Studio"; Flags: nowait postinstall skipifsilent
