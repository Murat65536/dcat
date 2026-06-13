[Setup]
AppName=dcat
AppVersion=0.1.0
AppPublisher=dcat contributors
DefaultDirName={localappdata}\Programs\dcat
DefaultGroupName=dcat
OutputDir=installer
OutputBaseFilename=dcat-windows-setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
; Tells Windows Explorer to refresh so the PATH change takes effect immediately
ChangesEnvironment=yes
DisableProgramGroupPage=yes

[Files]
; Copy everything from the dist folder we created
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
; Add the app installation directory to the current user's PATH.
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
// Function to check if our directory is already in the PATH
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  // Look for the path inside the current Path variable
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

// Function to safely remove our directory from the PATH on uninstall
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  OrigPath: string;
  PosApp: Integer;
  AppStr: string;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
    begin
      AppStr := ';' + ExpandConstant('{app}');
      PosApp := Pos(Uppercase(AppStr), Uppercase(OrigPath));
      if PosApp > 0 then
      begin
        Delete(OrigPath, PosApp, Length(AppStr));
        RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath);
      end;
    end;
  end;
end;
