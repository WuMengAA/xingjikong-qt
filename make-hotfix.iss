; 星集控被控端 · 极小热修包（Inno Setup 6，2026-10-10 由 make-hotfix.nsi 迁移）
;
; 与 installer.iss（全量安装包）的区别 —— 热修只做"把文件换成新的"这一件事：
;   · 只带变更件（stelarith-agent-qt.exe）+ 运行库（OpenSSL 两枚），成品约 3MB；
;     全量安装包 90MB+ 级。走 OTA 热修时每台机器少下 80MB 以上。
;   · 绝不写 agent.env —— 那里是设备令牌/uid/云端地址，因机器而异，热修碰它等于把
;     装机时配好的身份冲掉。
;   · 不建快捷方式、不写卸载键、不调 install-autostart.ps1 —— 那些是"首次安装"的职责。
;
; AppId 必须与 installer.iss 完全相同（{54EABF4A-...}）：Inno 据此找到已装目录 {app}，
; 热修只是往那个目录覆盖文件，不会另起一套安装。
;
; 构建：scripts/make-hotfix.mjs 会设 STE_INSTALLER_VERSION 并调 ISCC 编它。
; 注意：UTF-8 带 BOM（Inno Unicode 靠 BOM 正确读中文）。

#define MyAppVersion GetEnv("STE_INSTALLER_VERSION")
#if MyAppVersion == ""
  #define MyAppVersion "dev"
#endif

#define MyAppName "星集控被控端 热修"
#define MyAppExeName "stelarith-agent-qt.exe"
; 与主安装器同一 AppId（双花括号转义，否则 {54EABF4A...} 被当成常量引用）
#define MyAppId "{{54EABF4A-EC9F-4906-8CAA-5B46087310D2}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
; 安装目录固定（与全量安装器一致）；AppId 命中已装实例时 Inno 自动用原 {app}
DefaultDirName={autopf}\Stelarith
DisableDirPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; 仅 64 位（被控端只编 x64）
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
OutputDir=dist
OutputBaseFilename=stelarith-agent-hotfix-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
; 热修必须静默：OTA 用 /VERYSILENT 调它，绝不弹任何 UI
WizardStyle=modern windows11
DisableWelcomePage=yes
DisableFinishedPage=yes
; 装前若 exe 在跑，提示关闭；[Code] 里也强制停一次更稳
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "scripts\Installer_Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; 仅变更件 + 运行库（与 NSIS 版口径一致：运行时 Qt6*.dll 等不进热修包）
Source: "build\stelarith-agent-qt.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\libssl-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    // 先停掉正在跑的旧实例，否则 exe 被占用、覆盖失败
    Exec('powershell.exe',
         '-NoProfile -Command "Get-Process -Name stelarith-agent-qt -ErrorAction SilentlyContinue | Stop-Process -Force"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(800);
  end;
  // 注意：热修不写 agent.env、不建快捷方式、不跑 install-autostart.ps1
end;
