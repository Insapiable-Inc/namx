; Inno Setup script for the Windows installer (ROADMAP Phase 8). The release workflow
; builds it from the same windeployqt folder as the portable zip:
;
;   iscc /DAppVersion=0.0.0 /DSourceDir=<abs>\dist\namx /DOutputDir=<abs>\dist namx.iss
;
; Per-user install by default (no admin); the first page offers a machine-wide one.
; Unsigned for now: SmartScreen warns about an unknown publisher.

#ifndef AppVersion
  #error Pass /DAppVersion=<version>
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<windeployqt folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change: Windows matches upgrades and the uninstaller by this ID.
AppId={{74FB5A98-AEF3-4B92-8B86-BC397778C341}
AppName=NAM-X
AppVersion={#AppVersion}
AppVerName=NAM-X {#AppVersion}
AppPublisher=Insapiable
AppPublisherURL=https://github.com/Insapiable-Inc/namx
AppSupportURL=https://github.com/Insapiable-Inc/namx/issues
DefaultDirName={autopf}\NAM-X
DefaultGroupName=NAM-X
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\resources\icons\namx.ico
UninstallDisplayIcon={app}\namx.exe
ChangesAssociations=yes
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=namx-{#AppVersion}-windows-x64-setup

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "vc_redist*.exe"; Flags: ignoreversion recursesubdirs
; The MSVC runtime installer, run only when the runtime is missing (see [Code]).
Source: "{#SourceDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\NAM-X"; Filename: "{app}\namx.exe"
Name: "{autodesktop}\NAM-X"; Filename: "{app}\namx.exe"; Tasks: desktopicon

[Registry]
; .nam / .nampreset open in NAM-X. HKA = the machine for an admin install, the user
; otherwise. .nam also lists NAM-X under "Open with" without taking the default from
; another app; .nampreset is ours alone.
Root: HKA; Subkey: "Software\Classes\.nam\OpenWithProgids"; ValueType: string; ValueName: "NAMX.Model"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\NAMX.Model"; ValueType: string; ValueName: ""; ValueData: "Neural Amp Modeler profile"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\NAMX.Model\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\namx.exe,0"
Root: HKA; Subkey: "Software\Classes\NAMX.Model\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\namx.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.nampreset"; ValueType: string; ValueName: ""; ValueData: "NAMX.Preset"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.nampreset\OpenWithProgids"; ValueType: string; ValueName: "NAMX.Preset"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\NAMX.Preset"; ValueType: string; ValueName: ""; ValueData: "NAM-X preset"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\NAMX.Preset\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\namx.exe,0"
Root: HKA; Subkey: "Software\Classes\NAMX.Preset\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\namx.exe"" ""%1"""

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist; Check: VCRuntimeMissing
Filename: "{app}\namx.exe"; Description: "{cm:LaunchProgram,NAM-X}"; Flags: nowait postinstall skipifsilent

[Code]
const
  RuntimeKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';

// The VC++ 2015-2022 x64 runtime registers its version here. "Installed" alone isn't
// enough: namx.exe needs at least the runtime it was built against, which is the
// version of the vc_redist.x64.exe shipped next to it (an older 14.x fails to start it).
function VCRuntimeMissing: Boolean;
var
  Installed, Minor, Bld, MS, LS, NeedMinor, NeedBld: Cardinal;
begin
  if not (RegQueryDWordValue(HKLM64, RuntimeKey, 'Installed', Installed) and (Installed = 1))
     or not RegQueryDWordValue(HKLM64, RuntimeKey, 'Minor', Minor)
     or not RegQueryDWordValue(HKLM64, RuntimeKey, 'Bld', Bld) then
  begin
    Result := True;
    exit;
  end;
  if not GetVersionNumbers(ExpandConstant('{tmp}\vc_redist.x64.exe'), MS, LS) then
  begin
    Result := False; // no bundled installer to compare with (or to run)
    exit;
  end;
  NeedMinor := MS and $FFFF;
  NeedBld := LS shr 16;
  Result := (Minor < NeedMinor) or ((Minor = NeedMinor) and (Bld < NeedBld));
end;
