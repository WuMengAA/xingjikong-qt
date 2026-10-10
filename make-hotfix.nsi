; 星集控被控端 · 极小热修包（NSIS 3，2026-10-09）
;
; 与 installer.nsi（全量安装包）的区别 —— 热修只做"把文件换成新的"这一件事：
;   · 只带**变更件**（当前只有主程序 stelarith-agent-qt.exe），成品 ~1MB；
;     而全量安装包是 114MB 级。走 OTA 热修时每台机器少下 100MB 以上。
;   · 🔒 **绝不写 agent.env** —— 那里是设备令牌 / uid / 云端地址，因机器而异，
;     热修碰它等于把装机时配好的身份冲掉（全量安装包靠"只补缺不覆盖"守着同一件事）。
;   · 不建快捷方式、不写卸载键、不调 install-autostart.ps1、不写 version.nsh 之外的东西
;     —— 那些是"首次安装"的职责，热修重复做只会造出两套真相。
;
; 为什么是 exe 而不是 zip：被控端的 OTA 链路是「下载 → 校验 sha256 → 静默执行 /S」
;   （src/main.cpp 的 startSelfUpdate / otaOnFinished），它只会跑 exe。
;   所以**被控端的热修包必须是 exe**；管理端的热修包才是 zip（它的 OTA 是解压覆盖）。
;
; 构建：makensis make-hotfix.nsi   （工作目录＝本文件所在目录）
;   一般不必手工跑 —— scripts/make-hotfix.mjs 会先组好 hotfix-staging/ 再调它。
;
; ⚠️ 本文件必须保存为 **UTF-8 带 BOM**：`Unicode true` 靠 BOM 才生效，
;    丢了 BOM 会报 "Bad text encoding"。

Unicode true
; 版本号吃 CMake 生成的 build\version.nsh（!define VER），这里不手抄第二份。
!include "build\version.nsh"

Name "星集控被控端 热修"
OutFile "dist\stelarith-agent-hotfix-${VER}.exe"
; 真实安装目录以安装器写下的注册表为准；没有（理论上不会发生）才回落默认值。
InstallDirRegKey HKLM "Software\StelarithAgentQt" "InstallDir"
InstallDir "$PROGRAMFILES64\Stelarith"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
; 静默：OTA 用 /S 调它，绝不能弹任何 UI
SilentInstall silent
ShowInstDetails nevershow

Section "hotfix"
	SetOutPath "$INSTDIR"

	; 先停旧实例：exe 运行中被占用，不先停覆盖一定失败。
	; 与全量安装器同一写法（PowerShell 的 Stop-Process，不用 taskkill）。
	nsExec::ExecToLog 'powershell -NoProfile -Command "Get-Process -Name stelarith-agent-qt -ErrorAction SilentlyContinue | Stop-Process -Force"'
	Sleep 800

	; 变更件。暂存目录由 scripts/make-hotfix.mjs 组装。
	File "hotfix-staging\stelarith-agent-qt.exe"

	; ── OpenSSL 3 运行库（2026-10-10 补）──────────────────────────────────────────
	; 只换 exe、机器上却没有 libssl-3-x64.dll / libcrypto-3-x64.dll ⇒ 热修完照样
	; 起不来（「找不到 libssl-3-x64.dll」）。成因见 installer.nsi 里同一段注释。
	; 这两个是**运行库**，与"只带变更件"并不冲突：不带上，这次热修本身就是坏的。
	File "hotfix-staging\libssl-3-x64.dll"
	File "hotfix-staging\libcrypto-3-x64.dll"
SectionEnd
