; 星集控被控端 · 安装程序（NSIS 3）
;
; 三个设计要点：
;   1. **装机参数因机器而异**（云端地址 / 设备令牌 / 设备 uid）→ 放在安装向导里输入，
;      装完写成 agent.env。不让装机的人去手工编辑文件（他会漏、会写错）。
;   2. **自启沿用 install-autostart.ps1**（登录触发器计划任务，不是 Windows Service）：
;      真 Service 跑在 Session0，抓不到交互桌面 —— 锁屏/输入注入/截屏全会打在空白会话上。
;      NSIS 只负责「解包 + 写配置 + 调它」，**建任务的逻辑只有一份**，避免两处分叉。
;   3. **安装目录固定、不让改** —— 卸载时要 `RMDir /r`，如果放任用户把目录选成 C:\ 或已有目录，
;      那就是一次误删事故。固定成 C:\Program Files\Stelarith 之后，这个递归删除才是安全的。
;
; 构建：makensis installer.nsi      （工作目录＝本文件所在目录）
;
; ⚠️ 别直接**双击本文件**来编译：.nsi 关联的是 NSIS 的 GUI 编译器 makensisw.exe，
;    它把临时文件写 %TEMP% 并把真实原因吞进一个模态框，弹的是
;    「Error writing temporary file. Make sure your temp folder is valid.」——
;    你既看不到是哪个目录、也看不到真正的原因。
;    请改用 CLI 入口（任选其一，工作目录＝本文件所在目录）：
;        build-agent-installer.sh     ← Git Bash / 脚本化（会自己把 %TEMP% 指到可用目录）
;        build-agent-installer.bat    ← 普通 cmd 窗口（脚本内已强制 CLI 编译器 + 可写性检查）

Unicode true
!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"     ; ${GetParameters} / ${GetOptions}：无人值守装机传参用（2026-10-06 加）

!define APPNAME "星集控被控端"
!define APPID   "StelarithAgentQt"
; ⚠️ VER 由 CMakeLists.txt 生成到 build\version.nsh —— 工程里版本号只有一个真源
;    （set(AGENT_VERSION ...)，2026-10-07 起同 DeepSeek Harness 标准：X.Y.Z / X.Y.Z-rc.N）。
;    这里以前手抄一份 "0.6.5"，跟 kAppVersion 各写一份，漂移过一次（exe 0.4.0-v1 /
;    安装器 0.5.0，云端按旧号判断 OTA）。别把手写值抄回来。
!include "build\version.nsh"

Name "${APPNAME}"
OutFile "dist\stelarith-agent-setup.exe"
InstallDir "$PROGRAMFILES64\Stelarith"
InstallDirRegKey HKLM "Software\${APPID}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; ── 装机输入（默认值给好，装 TEST1 时基本只要改 uid）──
Var DlgUrl
Var DlgToken
Var DlgUid
Var Url
Var Token
Var Uid

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
Page custom ConfigPageCreate ConfigPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
	StrCpy $Url   "ws://127.0.0.1:8788/ws/agent"
	StrCpy $Token ""
	StrCpy $Uid   "TEST1"

	; ── 无人值守装机：从命令行读配置（2026-10-06 加）────────────────────────
	; 为什么必须有：云端的 assets/provision/install-agent.ps1（浏览器点一下 → 教室机双击 bat
	; 那条链）就是 `Start-Process $pkg -ArgumentList "/S"` 静默装的。静默模式下向导页不显示，
	; 这三项如果只能手填，无人值守这条路就断了 —— 而它恰恰是"零成本维护"的入口。
	; 用法：stelarith-agent-setup.exe /S /UID=class_xlzx_2028_08 /URL=wss://.../ws/agent /TOKEN=xxx
	; 不给参数就退回上面的默认值（行为与改动前一致）。
	${GetParameters} $R0
	${GetOptions} $R0 "/UID="   $R1
	${If} $R1 != ""
		StrCpy $Uid $R1
	${EndIf}
	${GetOptions} $R0 "/URL="   $R1
	${If} $R1 != ""
		StrCpy $Url $R1
	${EndIf}
	${GetOptions} $R0 "/TOKEN=" $R1
	${If} $R1 != ""
		StrCpy $Token $R1
	${EndIf}
FunctionEnd

Function ConfigPageCreate
	; ⚠️ 静默安装(/S)必须**跳过本页**：无桌面时 nsDialogs::Create 返回 error，
	;    而下面的 ${If} $0 == error { Abort } 会把**整个安装**中止掉。
	;    实测：不改这里，`setup.exe /S` 退出码 2、什么都没装 —— 云端那条无人值守装机链
	;    (install-agent.ps1 第 95 行 /S) 就是死在这一步，而且现场只看到"装了但没反应"。
	${If} ${Silent}
		Abort
	${EndIf}

	!insertmacro MUI_HEADER_TEXT "装机参数" "这三项因机器而异；装完会写成 agent.env"
	nsDialogs::Create 1018
	Pop $0
	${If} $0 == error
		Abort
	${EndIf}

	${NSD_CreateLabel} 0 0 100% 22u "云端地址 / 设备令牌 / 设备 uid 会写进 agent.env。$\r$\n不确定就先按默认装，之后改 $\"$INSTDIR\agent.env$\" 再重启被控端。"
	Pop $0

	${NSD_CreateLabel} 0 30u 100% 11u "云端地址（ws://主机:8788/ws/agent）"
	Pop $0
	${NSD_CreateText} 0 42u 100% 13u "$Url"
	Pop $DlgUrl

	${NSD_CreateLabel} 0 62u 100% 11u "设备令牌（要与云端 CLOUD_WS_TOKEN 一致；留空则云端会拒绝）"
	Pop $0
	${NSD_CreateText} 0 74u 100% 13u "$Token"
	Pop $DlgToken

	${NSD_CreateLabel} 0 94u 100% 11u "设备 uid（这台机器的代号，例如 TEST1）"
	Pop $0
	${NSD_CreateText} 0 106u 100% 13u "$Uid"
	Pop $DlgUid

	nsDialogs::Show
FunctionEnd

Function ConfigPageLeave
	${NSD_GetText} $DlgUrl   $Url
	${NSD_GetText} $DlgToken $Token
	${NSD_GetText} $DlgUid   $Uid
FunctionEnd

Section "install"
	SetOutPath "$INSTDIR"

	; 先停掉正在跑的旧实例：否则 exe 被占用，覆盖会失败。
	; 用 PowerShell 的 Stop-Process，**不用 taskkill** —— cmd 系工具语义模糊、返回难判断，
	; 在这台机器上还容易被执行策略/路径转换绊住；cmdlet 的行为是可预期的。
	nsExec::ExecToLog 'powershell -NoProfile -Command "Get-Process -Name stelarith-agent-qt -ErrorAction SilentlyContinue | Stop-Process -Force"'
	Sleep 800

	; ══ 装机包内容：分来源点名取件，**不通杀 deploy/** ══════════════════════════
	; 原来这里是一句 `File /r "deploy\*.*"`，问题有三个（2026-10-09 实测）：
	;   ① deploy/stelarith-agent-qt.exe 是**旧快照**（10-06），改的功能全不带；
	;   ② deploy/ 里从来没有 resources/ translations/ position/（windeployqt 产出，
	;      只在 build/）⇒ 装完照常启动、照常连云端，**只在有人订阅画面时崩**；
	;   ③ **🔒 会把 deploy/agent.env 打进包** —— 那是构建机自己的**真实设备令牌**，
	;      任何人 7z 解开安装包就能冒充该设备连云端（Delete 只在安装时删，包里仍有）。
	;   所以改成：运行时一律从 build/ 取，deploy/ 只点名取"构建不产出"的那几样。
	;
	; ⚠️ 目录一律写 `File /r "build\xxx"` **不带 `\*.*`**：带了通配符 NSIS 会把目录内容
	;    平铺到安装根，而 Qt 按 platforms\ 这种固定相对路径找插件 ⇒ 插件都在却找不到。

	; ── 运行时（build/：刚编出来的 exe + windeployqt 补全的 Qt）──
	File "build\stelarith-agent-qt.exe"
	File "build\Qt6*.dll"
	; ── OpenSSL 3 运行库（2026-10-10 补）────────────────────────────────────────────
	; 为什么必须**显式点名**（吃不到上面那条 `Qt6*.dll` 通配）：
	; 被控端静态链了 libdatachannel 做 WebRTC，而它的 DTLS 走 PostgreSQL 17 自带的
	; OpenSSL —— CMakeLists 里链的就是
	;   "C:/Program Files/PostgreSQL/17/lib/libssl.lib" / libcrypto.lib
	; 于是 exe 的**导入表**里直接写着 libssl-3-x64.dll / libcrypto-3-x64.dll；
	; 而 windeployqt 只认 Qt 全家，不认识它们 ⇒ 装完双击就是
	;   「stelarith-agent-qt.exe - 系统错误：由于找不到 libssl-3-x64.dll」。
	; 这两个 DLL 随包分发（OpenSSL 3.x，Apache-2.0，允许再分发）。
	File "build\libssl-3-x64.dll"
	File "build\libcrypto-3-x64.dll"
	File "build\QtWebEngineProcess.exe"
	File /r "build\platforms"
	File /r "build\imageformats"
	File /r "build\iconengines"
	File /r "build\generic"
	File /r "build\networkinformation"
	File /r "build\styles"
	File /r "build\tls"
	File /r "build\resources"
	File /r "build\translations"
	File /r "build\position"
	; ⚠️ 不拷 build\qml：被控端不用 QML（灵动岛是 Widgets 画的），这个目录是空的，
	;    写进来 makensis 会直接报 "no files found" 而中止。

	; ── 这两个 build/ 里没有（windeployqt 不拷），只在 deploy/ 素材库 ──
	;    QtWebEngine 的 DirectX/软件渲染回退栈，缺了在没独显的教室机上会黑屏。
	File "deploy\D3Dcompiler_47.dll"
	File "deploy\opengl32sw.dll"

	; ── deploy/ 独有的脚本与配置（构建从不产出这些）──
	File "deploy\install-autostart.ps1"
	File "deploy\agent.env.example"
	File "deploy\start-agent.bat"
	File "deploy\msi-post-install.cmd"

	; 写 agent.env —— 让 exe 直接可用，装机的人不用手改文件。
	; 兜底：静默安装（/S）不走向导页，这三个变量可能从未被赋值 —— 别把空值写进配置文件
	${If} $Url == ""
		StrCpy $Url "ws://127.0.0.1:8788/ws/agent"
	${EndIf}
	${If} $Uid == ""
		StrCpy $Uid "TEST1"
	${EndIf}

	; ⚠️ 注释行**必须纯 ASCII**：第一版这里写了中文，结果在 GBK 终端里显示成乱码
	;    （写文件的编码和读的人不一致）。配置文件里的注释不值得为它引一个编码问题。
	FileOpen $0 "$INSTDIR\agent.env" w
	FileWrite $0 "rem Generated by installer. Edit values, then restart the agent.$\r$\n"
	FileWrite $0 "set STE_QT_WS_URL=$Url$\r$\n"
	FileWrite $0 "set STE_QT_WS_TOKEN=$Token$\r$\n"
	FileWrite $0 "set STE_QT_UID=$Uid$\r$\n"
	FileWrite $0 "set STE_QT_SHOT_DIR=$INSTDIR\shots$\r$\n"
	FileClose $0

	; 建登录触发器计划任务 —— 逻辑在 ps1 里（-SkipCopy：文件已由本安装器放好）
	nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\install-autostart.ps1" -SkipCopy -Dest "$INSTDIR"'

	; 卸载信息（控制面板里能看到）
	WriteRegStr HKLM "Software\${APPID}" "InstallDir" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}" "DisplayName"     "${APPNAME}（星集控）"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}" "DisplayIcon"     "$INSTDIR\stelarith-agent-qt.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}" "DisplayVersion"  "${VER}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}" "Publisher"       "Stelarith"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}" "UninstallString" "$INSTDIR\uninstall.exe"
	WriteUninstaller "$INSTDIR\uninstall.exe"

	; ── 桌面 / 开始菜单快捷方式（2026-10-07 补）────────────────────────────
	; 为什么补：原先安装器压根没有 CreateShortCut，现存的「星集控被控端.lnk」是手工建的
	;   ——没有出处、卸载时删不掉、重装还会叠出第二份。快捷方式必须与安装器同生同死。
	; SetShellVarContext all ⇒ 下面的 $DESKTOP / $SMPROGRAMS 指向**所有用户**那一份
	;   （公共桌面 C:\Users\Public\Desktop、公共开始菜单 ProgramData\...\Programs）：
	;   教室机装完是直接切到学生账户用的，若只建当前用户的，换账户后桌面是空的。
	; 用完立刻切回 current —— NSIS 的注册表上下文也会跟着变，不还原后面的
	;   WriteRegStr 会写到 HKLM\Software\<APPID> 之外的分支去，卸载键就对不上了。
	SetShellVarContext all
	CreateDirectory "$SMPROGRAMS\${APPNAME}"
	CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\stelarith-agent-qt.exe" "" "$INSTDIR\stelarith-agent-qt.exe" 0
	CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\stelarith-agent-qt.exe" "" "$INSTDIR\stelarith-agent-qt.exe" 0
	CreateShortCut "$SMPROGRAMS\${APPNAME}\卸载 ${APPNAME}.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
	SetShellVarContext current
SectionEnd

Section "un.install"
	; 先删任务（不然它可能把进程再拉起来），再杀进程
	nsExec::ExecToLog 'powershell -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\install-autostart.ps1" -Uninstall'
	; 同样用 Stop-Process，不用 taskkill
	nsExec::ExecToLog 'powershell -NoProfile -Command "Get-Process -Name stelarith-agent-qt -ErrorAction SilentlyContinue | Stop-Process -Force"'
	Sleep 800

	; ── 删快捷方式（2026-10-07 补）────────────────────────────────────────
	; 安装段建的就得由卸载段收回去，否则公共桌面上会留下一枚点了没反应的孤儿 lnk。
	; 同样要回到 all 上下文：安装时建的是所有用户那份，用 current 删只会删空。
	SetShellVarContext all
	Delete "$DESKTOP\${APPNAME}.lnk"
	Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
	Delete "$SMPROGRAMS\${APPNAME}\卸载 ${APPNAME}.lnk"
	RMDir "$SMPROGRAMS\${APPNAME}"
	SetShellVarContext current

	Delete "$INSTDIR\uninstall.exe"

	; 删安装目录。为什么这里直接 RMDir /r 是安全的：
	;   本安装器**没有目录选择页**，$INSTDIR 恒为 "$PROGRAMFILES64\Stelarith" ——
	;   不可能指向盘根或别的程序的目录。当初去掉目录选择页，就是为了让这一次递归删除站得住脚。
	RMDir /r "$INSTDIR"

	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPID}"
	DeleteRegKey HKLM "Software\${APPID}"

	; 注意：**不删数据目录** %LOCALAPPDATA%\xingjikong（里面有历史日志与截图，
	; 留证比"清干净"重要；要清由人自己决定）
SectionEnd
