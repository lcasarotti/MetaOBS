; Windows installer for MetaOBS, compiled with Inno Setup 6 by
; build-aux/Publish-MetaOBS.ps1, which passes these defines:
;
;   AppVersion     the MetaOBS version, e.g. 1.0.0
;   ProjectRoot    the source tree, for the license and the icon
;   StageDir       the installed tree that cmake --install produced
;   OutputDir      where the installer is written
;   VCRedist       vc_redist.x64.exe of the toolset MetaOBS was built with
;   VCRedistMinor  the minor version of that runtime (14.<minor>)
;   Repository     the GitHub repository, owner/name
;
; The user chooses between an installation for all users, in Program Files,
; and one for the current user only, which needs no administrator rights.
; MetaOBS has its own application ID and folder, so it can be installed next
; to OBS Studio. The built-in updater runs the installer of a newer release
; silently, with /ALLUSERS or /CURRENTUSER as before.

#ifndef AppVersion
  #error AppVersion is not defined
#endif

#define AppExe "obs64.exe"

[Setup]
AppId={{BD8E8530-A7CF-4447-8FF1-4316049024FB}
AppName=MetaOBS
AppVersion={#AppVersion}
AppVerName=MetaOBS {#AppVersion}
AppPublisher=MetaOBS
AppPublisherURL=https://github.com/{#Repository}
AppSupportURL=https://github.com/{#Repository}/issues
AppUpdatesURL=https://github.com/{#Repository}/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=MetaOBS
DefaultDirName={autopf}\MetaOBS
DefaultGroupName=MetaOBS
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#ProjectRoot}\COPYING
SetupIconFile={#ProjectRoot}\frontend\cmake\windows\obs-studio.ico
UninstallDisplayIcon={app}\bin\64bit\{#AppExe}
UninstallDisplayName=MetaOBS
OutputDir={#OutputDir}
OutputBaseFilename=MetaOBS-{#AppVersion}-windows-x64-installer
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "swedish"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[CustomMessages]
InstallingVCRedist=Installing the Microsoft Visual C++ runtime...
italian.InstallingVCRedist=Installazione del runtime Microsoft Visual C++...

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#VCRedist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
Name: "{autoprograms}\MetaOBS"; Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"
Name: "{autodesktop}\MetaOBS"; Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Run]
; The runtime is installed for the whole system, so it asks for administrator
; rights even when MetaOBS is installed for the current user only.
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "{cm:InstallingVCRedist}"; Flags: shellexec waituntilterminated; Check: VCRedistNeeded
Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Description: "{cm:LaunchProgram,MetaOBS}"; Flags: nowait postinstall skipifsilent

[Code]
{ MetaOBS needs a Visual C++ runtime at least as new as the toolset it was
  built with. }
function VCRedistNeeded: Boolean;
var
  Key: String;
  Installed: Cardinal;
  Minor: Cardinal;
begin
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  Result := not (RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) and (Installed = 1) and
    RegQueryDWordValue(HKLM64, Key, 'Minor', Minor) and (Minor >= {#VCRedistMinor}));
end;
