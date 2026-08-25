; VCamBench installer.
;
; Two things make this more than a file copy:
;
;   1. vcamsource.dll must be registered in HKLM. The Windows Camera Frame
;      Server loads it as LOCAL SERVICE, and that account cannot see a per-user
;      registration - which is also why this product cannot ship as MSIX.
;      See docs/msix-limitation.md.
;
;   2. The install location is not a preference. The Frame Server has to be able
;      to read the DLL, so it goes somewhere with permissive ACLs. A user
;      profile folder fails silently: the camera appears and produces nothing.
;
; Build:  ISCC.exe installer\vcambench.iss
; Signed: ISCC.exe /DSIGN installer\vcambench.iss   (needs a configured SignTool)

#define AppName        "VCamBench"
#define AppVersion     "0.1.1"
#define AppPublisher   "VCamBench"
#define AppExeName     "vcambench.exe"
#define BuildDir       "..\build\Release"

[Setup]
; Never change AppId - it is how upgrades find the previous install.
AppId={{9F2B5C41-4E3A-4D0B-9C1E-7A6D8B2F4E55}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=..\dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Registering a COM server in HKLM requires elevation. There is no per-user
; install mode for this product.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; The media source calls MFCreateVirtualCamera, which is Windows 11 only.
MinVersion=10.0.22000

; The app holds this mutex for its lifetime, so Setup can notice a running copy
; and offer to close it instead of failing on a locked file.
AppMutex=Local\VCamBenchSingleInstance
CloseApplications=yes
RestartApplications=no

#ifdef SIGN
; Configure the tool once in the Inno Setup IDE or on the command line, e.g.
;   ISCC /Smysigntool="signtool.exe sign /fd sha256 /tr <ts-url> /td sha256 $f"
SignTool=mysigntool
SignedUninstaller=yes
#endif

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; regserver calls DllRegisterServer on install and DllUnregisterServer on
; uninstall. 64bit is required or it would be registered under WOW6432Node,
; where the Frame Server will not look.
Source: "{#BuildDir}\vcamsource.dll"; DestDir: "{app}"; \
    Flags: ignoreversion regserver 64bit uninsrestartdelete

Source: "{#BuildDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\vcamctl.exe";   DestDir: "{app}"; Flags: ignoreversion

; Diagnostics. Small, and they are how a support question gets answered:
; camlist shows what Windows actually enumerates, camcapture proves whether
; frames reach a normal app.
Source: "{#BuildDir}\camlist.exe";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\camcapture.exe"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
; The media source runs inside the Frame Server and writes its log here. The
; ACL is granted in [Run] below - Inno has no built-in identifier for the
; LOCAL SERVICE account.
Name: "{commonappdata}\{#AppName}"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Run]
; S-1-5-19 is NT AUTHORITY\LOCAL SERVICE, the account the Frame Server runs as.
; Logging is best effort: if this fails the camera still works, it just gets
; harder to diagnose, so the failure is not surfaced.
Filename: "{sys}\icacls.exe"; \
    Parameters: """{commonappdata}\{#AppName}"" /grant *S-1-5-19:(OI)(CI)M /T /C"; \
    Flags: runhidden; StatusMsg: "로그 디렉터리 권한 설정 중..."

Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commonappdata}\{#AppName}"
