; FileDonkey Windows installer.
;
; Not meant to be compiled by hand: package-win.ps1 builds and stages the
; payload, then passes the paths and the version in on the ISCC command line.
; The defaults below exist so that opening this file in the Inno Setup IDE
; gives something coherent rather than a wall of "undeclared identifier".
;
; Requires Inno Setup 6.3 or newer (for ArchitecturesAllowed=x64compatible).

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef MyAppStage
  #define MyAppStage ""
#endif
#ifndef MyStageDir
  #define MyStageDir "..\build\package-win-stage"
#endif
#ifndef MyRepoRoot
  #define MyRepoRoot ".."
#endif

#if MyAppStage == ""
  #define AppFullVersion MyAppVersion
#else
  #define AppFullVersion MyAppVersion + "-" + MyAppStage
#endif

[Setup]
; Never change AppId. It is how Windows recognises an existing FileDonkey and
; upgrades it in place; a new one here would leave every past version installed
; alongside the new one.
AppId={{6F2C56A1-B19E-4F1E-937A-A176769CC05D}
AppName=FileDonkey
AppVersion={#MyAppVersion}
AppVerName=FileDonkey {#AppFullVersion}
VersionInfoVersion={#MyAppVersion}
AppPublisher=Ihor Horemykin
AppPublisherURL=https://filedonkey.app
AppSupportURL=https://filedonkey.app
AppUpdatesURL=https://filedonkey.app

DefaultDirName={autopf}\FileDonkey
DefaultGroupName=FileDonkey
UninstallDisplayIcon={app}\FileDonkey.exe
LicenseFile={#MyRepoRoot}\LICENSE

OutputBaseFilename=FileDonkey-{#AppFullVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Admin, because {autopf} resolves to Program Files. FileDonkey itself runs as
; a normal user afterwards - WinFsp does not require an elevated process to
; mount, so nothing here asks the user to run the app as administrator.
PrivilegesRequired=admin

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole staged payload: FileDonkey.exe, the Qt runtime and plugin
; directories that windeployqt collected, the MinGW runtime DLLs, and
; winfsp-x64.dll.
Source: "{#MyStageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; Renamed to .txt only so that double-clicking them on a test machine opens an
; editor rather than the "how do you want to open this file" dialog.
Source: "{#MyRepoRoot}\LICENSE";                DestDir: "{app}"; DestName: "LICENSE.txt";                Flags: ignoreversion
Source: "{#MyRepoRoot}\LICENSE-NONCOMMERCIAL";  DestDir: "{app}"; DestName: "LICENSE-NONCOMMERCIAL.txt";  Flags: ignoreversion

[Icons]
Name: "{group}\FileDonkey";                       Filename: "{app}\FileDonkey.exe"
Name: "{group}\{cm:UninstallProgram,FileDonkey}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FileDonkey";                 Filename: "{app}\FileDonkey.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\FileDonkey.exe"; Description: "{cm:LaunchProgram,FileDonkey}"; Flags: nowait postinstall skipifsilent

; Nothing writes a Run key here. The app manages its own "start when I sign in"
; setting from the settings page - see app/autostart.cpp - and an installer that
; also wrote one would fight it.

[Code]
const
  WinFspDownloadUrl = 'https://winfsp.dev/rel/';

{ WinFsp is a kernel driver installed by its own MSI; the winfsp-x64.dll we ship
  beside the .exe is only the user-mode half and is useless on its own. Both
  registry views are checked because WinFsp registers itself under the 32-bit
  node, and this setup runs 64-bit where HKLM is the 64-bit view. }
function WinFspInstalled(): Boolean;
var
  InstallDir: String;
begin
  Result :=
    RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\WinFsp', 'InstallDir', InstallDir) or
    RegQueryStringValue(HKLM, 'SOFTWARE\WinFsp', 'InstallDir', InstallDir);
end;

function InitializeSetup(): Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;

  if WinFspInstalled() then
    Exit;

  if MsgBox(
      'FileDonkey needs WinFsp, which is not installed on this computer.' + #13#10 + #13#10 +
      'WinFsp provides the virtual disk that FileDonkey mounts. Without it, ' +
      'FileDonkey will install and start, but will not be able to mount anything.' + #13#10 + #13#10 +
      'Open the WinFsp download page now and install FileDonkey afterwards?' + #13#10 + #13#10 +
      'Choose No to continue installing FileDonkey now - you can install WinFsp ' +
      'later and FileDonkey will find it without being reinstalled.',
      mbConfirmation, MB_YESNO) = IDYES then
  begin
    ShellExec('open', WinFspDownloadUrl, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
    Result := False;
  end;
end;
