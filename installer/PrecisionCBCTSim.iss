; PrecisionCBCTSim 的 Inno Setup 安装包脚本。
;
; 用法：
;   1. 先跑 scripts\deploy.ps1 -Configuration Release，确保 dist\Release 是完整的便携目录；
;   2. 安装 Inno Setup 6（https://jrsoftware.org/isdl.php）；
;   3. 用 ISCC 编译本脚本：
;        & "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\PrecisionCBCTSim.iss
;      或在 Inno Setup IDE 里打开后按 F9。
;   4. 产物在 dist\PrecisionCBCTSim-<version>-setup.exe。
;
; 版本号与 CMakeLists.txt 的 project(VERSION) 保持一致。

#define AppName "PrecisionCBCTSim"
#define AppVersion "0.1.0"
#define AppPublisher "PrecisionCBCTSim"
#define AppExe "PrecisionCBCTSim.exe"
#define DistDir "..\dist\Release"

[Setup]
AppId={{8E1E1C4B-9E1C-4D2B-A9C8-2C7A3B6F5D10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; 安装程序与卸载项使用同一枚应用图标
SetupIconFile=..\app\appicon.ico
UninstallDisplayIcon={app}\{#AppExe}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 程序需要 OpenGL 3.2+ 上下文（VTK OpenGL2 后端）
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional tasks:"

[Files]
; dist\Release 已经是自包含目录（Qt + VTK + MSVC 运行库都在里面）
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
