; Inno Setup-script voor het Windows-installatieprogramma van Merik.
; Wordt gebouwd door .github/workflows/release.yml:
;   ISCC /DAppVersion=0.7.1 /DSourceDir=<map met Merik.exe en de dll's> /DOutputDir=dist installer\merik.iss

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
AppId={{6B0F7A3E-2C41-4D58-9E73-5A1C8B2D4F90}
AppName=Merik
AppVersion={#AppVersion}
AppVerName=Merik {#AppVersion}
AppPublisher=Erik Nie
AppPublisherURL=https://github.com/erik-nie/Merik2
AppSupportURL=https://github.com/erik-nie/Merik2/issues
AppUpdatesURL=https://github.com/erik-nie/Merik2/releases
DefaultDirName={autopf}\Merik
DefaultGroupName=Merik
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Merik.exe
LicenseFile={#SourceDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=Merik-{#AppVersion}-windows-x64-setup
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

[Icons]
Name: "{autoprograms}\Merik"; Filename: "{app}\Merik.exe"
Name: "{autodesktop}\Merik"; Filename: "{app}\Merik.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Merik.exe"; Description: "Start Merik"; Flags: nowait postinstall skipifsilent
