; sys-bar installer (Inno Setup 6). Built by tools/package.ps1:
;   iscc /DAppVersion=1.0.0 /DDistDir=<repo>\dist packaging\installer.iss
; Per-user install (no administrator rights), x64 and ARM64 in a single setup.

#ifndef AppVersion
  #error AppVersion is required (/DAppVersion=x.y.z)
#endif
#ifndef DistDir
  #define DistDir "..\dist"
#endif

#define AppName "sys-bar"
#define AppExe "sys-bar.exe"
#define AppUrl "https://github.com/vitorpelegrini/sys-bar"
#define HasX64 FileExists(DistDir + "\x64\" + AppExe)
#define HasArm64 FileExists(DistDir + "\arm64\" + AppExe)

#if !HasX64 && !HasArm64
  #error No binaries in DistDir: build them first (tools/package.ps1)
#endif

[Setup]
AppId={{8C5D4F7A-2E1B-4D63-9A7E-5B2C1F0D3E91}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Vitor Pelegrini de Souza
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
AppCopyright=Copyright (c) 2026 Vitor Pelegrini de Souza
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#DistDir}
OutputBaseFilename=sys-bar-{#AppVersion}-setup
SetupIconFile=..\assets\sys-bar.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
LicenseFile=..\LICENSE
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; Windows 10 1809 or later.
MinVersion=10.0.17763
#if HasX64 && HasArm64
ArchitecturesAllowed=x64compatible or arm64
ArchitecturesInstallIn64BitMode=x64compatible or arm64
#elif HasArm64
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
; sys-bar is closed gracefully by the [Code] below (it must give the taskbar its space back),
; never killed by the Restart Manager.
CloseApplications=no
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
#if FileExists(CompilerPath + "Languages\Korean.isl")
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
#endif

[CustomMessages]
en.AutoStart=Start sys-bar with Windows
pt.AutoStart=Iniciar o sys-bar com o Windows
es.AutoStart=Iniciar sys-bar con Windows
fr.AutoStart=Démarrer sys-bar avec Windows
de.AutoStart=sys-bar mit Windows starten
it.AutoStart=Avvia sys-bar con Windows
pl.AutoStart=Uruchamiaj sys-bar razem z systemem Windows
tr.AutoStart=sys-bar'ı Windows ile başlat
ru.AutoStart=Запускать sys-bar вместе с Windows
uk.AutoStart=Запускати sys-bar разом із Windows
ja.AutoStart=Windows の起動時に sys-bar を開始する
#if FileExists(CompilerPath + "Languages\Korean.isl")
ko.AutoStart=Windows 시작 시 sys-bar 실행
#endif

[Tasks]
Name: "autostart"; Description: "{cm:AutoStart}"

[Files]
#if HasX64
Source: "{#DistDir}\x64\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion; Check: not IsArm64
#endif
#if HasArm64
Source: "{#DistDir}\arm64\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion; Check: IsArm64
#endif
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"

[Registry]
; Same value the app writes from its "Start with Windows" menu item.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "sys-bar"; \
    ValueData: """{app}\{#AppExe}"""; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  WM_CLOSE = $0010;
  ControllerClass = 'SysBar.Controller';

var
  WasRunning: Boolean;

{ Asks a running sys-bar (any copy, installed or portable) to exit cleanly and waits for it. }
function CloseRunningInstance(): Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  Wnd := FindWindowByClassName(ControllerClass);
  if Wnd <> 0 then
  begin
    PostMessage(Wnd, WM_CLOSE, 0, 0);
    for I := 1 to 100 do
    begin
      Sleep(100);
      if FindWindowByClassName(ControllerClass) = 0 then
        Break;
    end;
  end;
  Result := FindWindowByClassName(ControllerClass) = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  WasRunning := FindWindowByClassName(ControllerClass) <> 0;
  if CloseRunningInstance() then
    Result := ''
  else
    Result := 'sys-bar is still running. Please exit it from its right-click menu and try again.';
end;

{ An update must not leave the user without sys-bar: restart it if it was running, even in
  silent mode (the [Run] entry only covers interactive installs). }
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if (CurStep = ssPostInstall) and WasRunning and WizardSilent() then
    ExecAsOriginalUser(ExpandConstant('{app}\{#AppExe}'), '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
end;

function InitializeUninstall(): Boolean;
begin
  CloseRunningInstance();
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { The autostart value may also have been created from the app menu. }
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'sys-bar');
end;
