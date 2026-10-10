; 星集控被控端 · 安装程序（Inno Setup 6）
; 迁移自 installer.nsi（NSIS 3）→ Inno Setup，2026-10-10。
;
; 三个设计要点（与 NSIS 版一致）：
;   1. **装机不收集云端地址/设备令牌**（2026-10-10 改）：agent.env 只给截图目录，
;      云端地址与设备令牌由「用星璃账号绑定」在首次启动时写入（OAuth 换票 → saveAgentEnv）。
;      安装向导不出现、也不接收这两项，避免令牌随包泄露、也免得装机的人抄错。
;   2. **自启沿用 deploy\install-autostart.ps1**（登录触发器计划任务，不是 Windows Service）：
;      真 Service 跑在 Session0，抓不到交互桌面 —— 锁屏/输入注入/截屏全会打在空白会话上。
;      安装器只负责「解包 + 写配置 + 调它」，建任务的逻辑只有一份，避免两处分叉。
;   3. **安装目录固定、不让改** —— 卸载时要删安装目录，如果放任用户把目录选成 C:\ 或已有目录，
;      那就是一次误删事故。固定成 {autopf}\Stelarith 之后，递归删除才是安全的。
;
; 构建（工作目录＝本文件所在目录）：
;     ISCC installer.iss /DMyAppVersion=0.6.24-rc.4
;   或 scripts/build-agent-installer.sh（会自动解析 build\version.nsh 的 VER 并传 /D）。
;
; 注意：本文件为 UTF-8 含 BOM（Inno Setup 6 是 Unicode，无 BOM 会按 ANSI 误读中文注释）。

; 版本号：由构建脚本通过环境变量 STE_INSTALLER_VERSION 传入（解析自 build\version.nsh，
; 唯一真源＝CMakeLists.txt 的 set(AGENT_VERSION ...)）。手动编译若未设该变量，回落到下面的常量。
; 不用 /DMyAppVersion=... 传参：Git Bash 的 MSYS 会把前导 /D 当成路径改写，导致 ISCC 报
; "more than one script filename"。环境变量没有前导斜杠，规避此坑。
#define MyAppVersion GetEnv("STE_INSTALLER_VERSION")
#if MyAppVersion == ""
  #define MyAppVersion "dev"
#endif

#define MyAppName "星集控被控端"
#define MyAppExeName "stelarith-agent-qt.exe"
#define MyPublisher "Stelarith"
#define MyURL "https://www.245959623.xyz"
; AppId：唯一标识本应用，卸载/升级据此识别。不要与其它应用共用。
; ⚠️ GUID 外的花括号必须写成 {{（双花括号），否则 Inno 会把 {54EABF4A...} 当成常量引用而报错。
#define MyAppId "{{54EABF4A-EC9F-4906-8CAA-5B46087310D2}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyPublisher}
AppPublisherURL={#MyURL}
AppSupportURL={#MyURL}
AppUpdatesURL={#MyURL}
; 所有用户安装（教室机切学生账户也看得到）；公共桌面/开始菜单快捷方式。
DefaultDirName={autopf}\Stelarith
; 不允许改目录（卸载 RMDir /r 才安全）
DisableDirPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; 仅 64 位（被控端只编 x64）。x64os = 64 位 Windows（x64 已弃用，Inno 提示改用 x64os）
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
OutputDir=dist
; 输出名带版本号（真值由 STE_INSTALLER_VERSION 传入；未传则回落 0.0.0）
OutputBaseFilename=stelarith-agent-setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern windows11
DefaultDialogFontName=Microsoft YaHei
; 装前若 exe 在跑，提示关闭；这里还在 [Code] 里强制停一次更稳。
CloseApplications=yes
RestartApplications=no
; 卸载信息（控制面板可见）
UninstallDisplayName={#MyAppName}（星集控）
UninstallDisplayIcon={app}\{#MyAppExeName}
; 代码签名（可选）：在 CI 里设 ISCC /SMySign=... 后取消注释。
; SignTool=MySign
; SignedUninstaller=yes

[Languages]
; 中文语言文件取自 Class-Widgets-2（参考仓库 scripts/Installer_Languages/），本机 Inno 6.7.3
; 的 Languages\ 里未带中文/英文，故中文走本地副本；英文用安装自带的 Default.isl（即英文基底）。
Name: "chinesesimplified"; MessagesFile: "scripts\Installer_Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; ── 运行时（build/：刚编出来的 exe + windeployqt 补全的 Qt）────────────────
; ⚠️ 不通杀 build\*：build/ 里混着 CMake 中间产物，整目录拷会把安装包撑爆。
;    与 NSIS 版一样点名取件。
Source: "build\stelarith-agent-qt.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Qt6*.dll"; DestDir: "{app}"; Flags: ignoreversion
; ── OpenSSL 3 运行库（2026-10-10 补）────────────────────────────────────────
; 被控端静态链 libdatachannel（WebRTC），其 DTLS 走 PG17 自带的 OpenSSL，
; exe 导入表直接写 libssl-3-x64.dll / libcrypto-3-x64.dll；windeployqt 不认它们，
; 必须显式点名（吃不到上面的 Qt6*.dll 通配）。随包分发（OpenSSL 3.x，Apache-2.0）。
Source: "build\libssl-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion
; ── QtWebEngine 进程（D8 拆 WebEngine 后整段删除）──────────────────────────
Source: "build\QtWebEngineProcess.exe"; DestDir: "{app}"; Flags: ignoreversion
; ── Qt 插件目录（递归）────────────────────────────────────────────────────
Source: "build\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\position\*"; DestDir: "{app}\position"; Flags: ignoreversion recursesubdirs createallsubdirs
; ── deploy/ 独有（构建不产出），点名取，不通杀 deploy\* ──────────────────────
; ⚠️ deploy/ 是个雷区：含旧快照 Qt6*.dll、154MB 的 Qt6WebEngineCore.dll、
;    **还有含真实设备令牌的 deploy\agent.env**。绝不通杀，只取下面这几样安全文件。
Source: "deploy\D3Dcompiler_47.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\opengl32sw.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\install-autostart.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\agent.env.example"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\start-agent.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "deploy\msi-post-install.cmd"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autoprograms}\{#MyAppName}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Run]
; 建登录触发器计划任务（逻辑在 ps1，-SkipCopy：文件已由安装器放好）
Filename: "powershell.exe"; Parameters: "/NoProfile /ExecutionPolicy Bypass /File ""{app}\install-autostart.ps1"" -SkipCopy -Dest ""{app}"""; WorkingDir: "{app}"; Flags: runhidden
; 装后启动（可选，默认不勾，用户想立刻绑定再勾）
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent unchecked

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
  begin
    // 先停掉正在跑的旧实例，否则 exe 被占用、覆盖失败。
    // 用 PowerShell 的 Stop-Process（不用 taskkill：语义模糊、返回难判断）。
    Exec('powershell.exe',
         '-NoProfile -Command "Get-Process -Name stelarith-agent-qt -ErrorAction SilentlyContinue | Stop-Process -Force"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(800);
  end;
  if CurStep = ssPostInstall then
  begin
    // 写最小化 agent.env：不含云端地址/令牌（由「用星璃账号绑定」首次启动时写入）。
    // 只给截图目录；设备码/显示名留空则回落电脑名，可在 OOBE 弹窗里改。
    // 注释行纯 ASCII（GBK 终端读配置会乱码，不值得为注释引编码问题）。
    SaveStringToFile(ExpandConstant('{app}\agent.env'),
      'rem Generated by installer. Bind this device via ''Stelarith account'' in the tray menu.' + #13#10 +
      'rem To rename this device, set STE_QT_NAME and restart the agent.' + #13#10 +
      'set STE_QT_SHOT_DIR=' + ExpandConstant('{app}') + '\shots' + #13#10,
      False);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    // 先删任务（不然它可能把进程再拉起来），再杀进程
    Exec('powershell.exe',
         '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\install-autostart.ps1') + '" -Uninstall',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec('powershell.exe',
         '-NoProfile -Command "Get-Process -Name stelarith-agent-qt -ErrorAction SilentlyContinue | Stop-Process -Force"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    // 注意：**不删数据目录** %LOCALAPPDATA%\xingjikong（历史日志与截图，留证比清干净重要）
  end;
end;
