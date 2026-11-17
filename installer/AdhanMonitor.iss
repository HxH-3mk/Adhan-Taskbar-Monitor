; Inno Setup script for Adhan Taskbar Monitor (https://jrsoftware.org/isinfo.php).
; Per-user install by default: no administrator rights needed, installs to %LOCALAPPDATA%\Programs.
; Setup also offers "Install for all users" (Program Files, needs administrator rights).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Root ".."

[Setup]
AppId={{A7D3E1C0-5B24-4D9A-8F31-9E2C4A1B7D01}
AppName=Adhan Taskbar Monitor
AppVersion={#AppVersion}
AppVerName=Adhan Taskbar Monitor {#AppVersion}
AppPublisher=HxH-3mk
AppPublisherURL=https://github.com/HxH-3mk/Adhan-Taskbar-Monitor
DefaultDirName={autopf}\Adhan Taskbar Monitor
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\dist
OutputBaseFilename=AdhanMonitor-Setup-{#AppVersion}
SetupIconFile={#Root}\res\app.ico
UninstallDisplayIcon={app}\AdhanMonitor.exe
UninstallDisplayName=Adhan Taskbar Monitor
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Tasks]
Name: "autostart"; Description: "Start Adhan Taskbar Monitor when I sign in to Windows"

[Files]
Source: "{#Root}\AdhanMonitor.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Adhan Taskbar Monitor"; Filename: "{app}\AdhanMonitor.exe"

[Run]
Filename: "{app}\AdhanMonitor.exe"; Description: "Start Adhan Taskbar Monitor now"; Flags: nowait postinstall skipifsilent
Filename: "https://buymeacoffee.com/hxh3mk"; Description: "Enjoying Adhan Taskbar Monitor? Support on Buy Me a Coffee"; Flags: shellexec postinstall unchecked skipifsilent nowait runasoriginaluser

[UninstallRun]
Filename: "{app}\AdhanMonitor.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "QuitApp"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

procedure QuitRunningApp();
var
  Code: Integer;
begin
  if FileExists(ExpandConstant('{app}\AdhanMonitor.exe')) then
    Exec(ExpandConstant('{app}\AdhanMonitor.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec('taskkill.exe', '/F /IM AdhanMonitor.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(300);
end;

function InstalledRunValue(): String;
begin
  Result := '"' + ExpandConstant('{app}\AdhanMonitor.exe') + '"';
end;

procedure RemoveOwnAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, 'AdhanTaskbarMonitor', Value) and (CompareText(Value, InstalledRunValue()) = 0) then
    RegDeleteValue(HKCU, RunKey, 'AdhanTaskbarMonitor');
end;

function CanWriteTo(Dir: String): Boolean;
var
  Parent, Probe: String;
begin
  Dir := RemoveBackslashUnlessRoot(Dir);
  if DirExists(Dir) then
  begin
    Probe := AddBackslash(Dir) + '~adhanmonitor-write-test.tmp';
    Result := SaveStringToFile(Probe, '', False);
    DeleteFile(Probe);
    exit;
  end;
  Parent := Dir;
  while not DirExists(Parent) do
  begin
    if ExtractFileDir(Parent) = Parent then
    begin
      Result := False;
      exit;
    end;
    Parent := ExtractFileDir(Parent);
  end;
  Probe := AddBackslash(Parent) + '~adhanmonitor-write-test';
  Result := CreateDir(Probe);
  if Result then
    RemoveDir(Probe);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not WizardSilent() and not CanWriteTo(WizardDirValue()) then
  begin
    if IsAdminInstallMode() then
      MsgBox('Setup cannot create or write to this folder:' + #13#10 + WizardDirValue() + #13#10#13#10 +
        'Please choose a different folder.', mbError, MB_OK)
    else
      MsgBox('Your account cannot create or write to this folder without administrator rights:' + #13#10 +
        WizardDirValue() + #13#10#13#10 +
        'Choose a folder you own (the default one always works), or close Setup, run it again and ' +
        'pick "Install for all users".', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    QuitRunningApp();
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('autostart') then
      RegWriteStringValue(HKCU, RunKey, 'AdhanTaskbarMonitor', InstalledRunValue())
    else
      RemoveOwnAutostart();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    QuitRunningApp();
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveOwnAutostart();
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\AdhanTaskbarMonitor');
  end;
end;
