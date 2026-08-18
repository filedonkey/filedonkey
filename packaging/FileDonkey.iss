; FileDonkey Windows installer.
;
; Not meant to be compiled by hand: package-win.ps1 builds and stages the
; payload, then passes the paths and the version in on the ISCC command line.
; The defaults below exist so that opening this file in the Inno Setup IDE
; gives something coherent rather than a wall of "undeclared identifier".
;
; Requires Inno Setup 6.3 or newer, for the x64os architecture identifiers.

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

; MyDokanMsi decides whether this installer carries the Dokany runtime or merely
; points at it; package-win.ps1 defines it unless -NoDokanBundle was passed.
; Everything below keys off whether it exists, so there is deliberately no
; default for it here.
#ifdef MyDokanMsi
  #define DokanMsiName ExtractFileName(MyDokanMsi)
  #ifndef MyDokanVersion
    #define MyDokanVersion "unknown"
  #endif
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

; Admin, because {autopf} resolves to Program Files - and, when the runtime is
; bundled, because installing Dokany's driver needs it too. FileDonkey itself
; runs as a normal user afterwards: Dokany does not require an elevated process
; to mount, so nothing here asks the user to run the app as administrator.
PrivilegesRequired=admin

; x64os rather than x64compatible. The latter would also let setup run on ARM64
; Windows through x64 emulation, and a kernel driver cannot be emulated: the
; bundled Dokan_x64.msi carries an x64 dokan2.sys that an ARM64 kernel will not
; load. Dokany publishes a separate Dokan_ARM64.msi, and until that is bundled
; too an ARM64 user is better served by a clear refusal here than by an
; application whose filesystem driver can never start.
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
#ifdef MyDokanMsi
; Listed first, and the position is load-bearing: SolidCompression makes the
; archive unpack in order, so a dontcopy entry at the end would force everything
; ahead of it to be decompressed before ExtractTemporaryFile could reach it.
;
; dontcopy because this is not part of the installed application. It is a
; prerequisite that PrepareToInstall extracts and runs only on machines that
; turn out to need it, into {tmp}, which is cleaned up either way.
Source: "{#MyDokanMsi}"; Flags: dontcopy
#endif

; The whole staged payload: FileDonkey.exe, the Qt runtime and plugin
; directories that windeployqt collected, and the MinGW runtime DLLs. No
; Dokany file appears here: its own installer owns dokan2.dll, in System32.
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
// Dokany is a kernel filesystem driver plus a user-mode DLL, both installed by
// its own MSI, and nothing in the staged payload substitutes for either.
//
// dokan2.dll is what gets checked, rather than the dokan2.sys driver or an
// uninstall entry, because it is a load-time import of FileDonkey.exe - so this
// asks precisely the question the Windows loader will ask a moment after setup
// finishes. A registry probe was the obvious thing to reach for, WinFsp having
// been found that way, but Dokany registers no key under SOFTWARE to read.
//
// ExpandConstant turns {sys} into the real System32 rather than the SysWOW64
// redirect, because ArchitecturesInstallIn64BitMode above puts this setup in
// 64-bit mode.
//
// These are line comments rather than the usual { } for a reason worth knowing
// before editing them: a brace comment ends at the FIRST closing brace, so
// naming an Inno constant inside one silently cuts the comment short and feeds
// the remainder of the sentence to the compiler as code.
function DokanInstalled(): Boolean;
begin
  Result := FileExists(ExpandConstant('{sys}\dokan2.dll'));
end;

#ifdef MyDokanMsi

// PrepareToInstall rather than a [Run] entry with a Check: [Run] discards the
// exit code, so a driver installation that genuinely failed and one that merely
// wants a reboot would both be reported to the user as a clean install. Here
// 3010 becomes Inno's own restart prompt, and anything else stops setup with
// the code still on screen.
//
// It also runs before a single file is copied, which is the right order: there
// is little point laying down an application whose prerequisite just failed.
//
// Dokany is deliberately not removed on uninstall. It is a shared system
// component, other software may be mounting through it, and pulling a
// filesystem driver out from under a stranger's application is not this
// installer's business.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';

  if DokanInstalled() then
    Exit;

  ExtractTemporaryFile('{#DokanMsiName}');

  if not Exec('msiexec.exe',
              '/i "' + ExpandConstant('{tmp}\{#DokanMsiName}') + '" /qn /norestart',
              '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := 'The Dokany installer could not be started.' + #13#10 +
              SysErrorMessage(ResultCode);
    Exit;
  end;

  if ResultCode = 3010 then
    NeedsRestart := True
  else if ResultCode <> 0 then
    Result := 'Dokany could not be installed - its installer returned ' +
              IntToStr(ResultCode) + '.' + #13#10 + #13#10 +
              'FileDonkey cannot run without it, so setup has stopped here.';
end;

// Installing a filesystem driver on someone's machine should not happen without
// saying so. The Ready page is the place for it: a modal dialog earlier in the
// wizard would interrupt to announce something the user cannot act on, whereas
// here it sits alongside everything else they are about to approve.
function UpdateReadyMemo(const Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + MemoGroupInfo;

  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;

  if not DokanInstalled() then
    Result := Result + NewLine + NewLine +
              'Prerequisite, installed first:' + NewLine +
              Space + 'Dokany {#MyDokanVersion} (filesystem driver)';
end;

#else

// Packaged with -NoDokanBundle, so there is no runtime to install from here and
// the most this can usefully do is point the user somewhere and step aside.
function InitializeSetup(): Boolean;
var
  ErrorCode: Integer;
begin
  Result := True;

  if DokanInstalled() then
    Exit;

  if MsgBox(
      'FileDonkey needs Dokany, which is not installed on this computer.' + #13#10 + #13#10 +
      'Dokany provides the virtual disk that FileDonkey mounts. FileDonkey ' +
      'cannot start without it: it will not open a window and report a ' +
      'problem, it will fail to launch at all.' + #13#10 + #13#10 +
      'Open the Dokany download page now and install FileDonkey afterwards?' + #13#10 + #13#10 +
      'Choose No to continue installing FileDonkey now - you can install ' +
      'Dokany later and FileDonkey will run without being reinstalled.',
      mbConfirmation, MB_YESNO) = IDYES then
  begin
    ShellExec('open', 'https://github.com/dokan-dev/dokany/releases/latest',
              '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
    Result := False;
  end;
end;

#endif
