; 星集控管理端 · NSIS 安装器（首次安装 / 二次升级 / 保留配置）
; 版本号来自 build/version.nsh（与 MSI、应用内同源，由 scripts/gen-wxs-viewer.mjs 生成）。
; 用法（在 viewer-qt/ 目录下）：makensis scripts/make-installer.nsi
;   产出 dist/stelarith-viewer-setup-<版本>.exe，默认安装到 **$PROGRAMFILES64\StelarithViewer（纯 ASCII）**
;
; 三种安装方式：
;   1) 首次安装：检测不到旧安装 → 走引导（欢迎 → 目录 → 安装 → 完成），默认目录纯 ASCII。
;   2) 二次升级：.onInit 读注册表旧路径 → 沿用该路径覆盖文件（旧路径是 ASCII 时）；
;      若读到的是**旧版中文默认目录**（$PROGRAMFILES64\Stelarith\星集控管理端）→ 迁移到新
;      ASCII 目录并把旧目录删掉（viewer.env 先抢出来）。
;      viewer.env（老师配的站点/令牌）先备份再还原，不被默认包冲掉（对应铁律 #13 的 /XF viewer.env）。
;   3) OTA：由应用内 Updater 自更新（apply-update.cmd）完成，不经此安装器；
;      此处只覆盖"人手动装/升"两种场景，避免和 OTA 抢同一目录。

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "..\build\version.nsh"

Unicode true
SetCompressor /SOLID lzma

!define APPNAME "星集控管理端"
!define EXE "viewer-qt.exe"
!define REGKEY "Software\Stelarith\Viewer"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\StelarithViewer"

Name "${APPNAME}"
; ⚠️ NSIS 的 OutFile 相对路径是相对**脚本所在目录**（本文件在 scripts/ 下），
;    不是"你在哪个目录敲 makensis"。写 "dist\..." 会产出到 scripts\dist\ 里去。
;    所以这里显式回一级到工程根，与被控端的 dist\stelarith-agent-setup.exe 对齐。
OutFile "..\dist\stelarith-viewer-setup-${VERSION_RAW}.exe"
InstallDir "$PROGRAMFILES64\StelarithViewer"
; ⚠️ 这里**故意不用 InstallDirRegKey**。它的语义是"注册表里记过就直接装回原处"，
;    而旧版（0.6.24-rc.2 及以前）的默认目录是**中文**的
;    （$PROGRAMFILES64\Stelarith\星集控管理端）—— 于是无论把上面的默认值改成什么，
;    重装永远沿用注册表里那条中文路径 ⇒ 用户看到的还是中文目录（这是"改了却没生效"的真因）。
;    改为在 .onInit 里手动裁决（见下）：非中文旧路径才沿用；命中旧中文默认则迁移。
!define LEGACY_DEFAULT "$PROGRAMFILES64\Stelarith\星集控管理端"
Var /GLOBAL LEGACYDIR

RequestExecutionLevel admin

; ── 安装目录裁决（为什么不用 InstallDirRegKey 见上面 InstallDir 的说明）──
;   规则：
;     · 没装过                  → 用 ASCII 默认 $PROGRAMFILES64\StelarithViewer
;     · 旧路径 == 旧中文默认     → 记进 $LEGACYDIR，走"装到新目录 + 删旧目录"的迁移
;     · 其它（自定义 / 已 ASCII） → 沿用原处，正常覆盖升级（不做"装两份"）
Function .onInit
  StrCpy $LEGACYDIR ""
  ReadRegStr $0 HKLM "${REGKEY}" "InstallDir"
  StrCmp $0 "" init_done
  StrCmp $0 "${LEGACY_DEFAULT}" legacy_path
  StrCpy $INSTDIR $0
  Goto init_done
legacy_path:
  StrCpy $LEGACYDIR $0
init_done:
FunctionEnd

; ── 界面（三步引导：欢迎 / 目录 / 安装 / 完成）──
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

; ── 安装段 ──
Section "Main" SEC_MAIN
  SetOutPath "$INSTDIR"

  ; ── 升级时先停掉正在跑的旧实例 ────────────────────────────────────────
  ; 为什么必须有：被控端 installer.nsi 在 136 行已经有同样处理，管理端原先**只在卸载段**
  ; 停实例（131 行），安装/升级段漏了。后果：老师正开着管理端时点升级安装，
  ; viewer-qt.exe / Qt6*.dll 被占用 → 覆盖失败、留下半截目录、下次启动崩。
  ; 首次安装时进程不存在，Get-Process -ErrorAction SilentlyContinue 静默跳过，安全。
  ; 用 PowerShell 的 Stop-Process，不用 taskkill（cmd 系语义模糊、返回难判断，
  ; 在这台机器上还容易被执行策略/路径转换绊住）。
  nsExec::ExecToLog 'powershell -NoProfile -Command "Get-Process -Name viewer-qt -ErrorAction SilentlyContinue | Stop-Process -Force"'
  Sleep 800

  ; ── 旧版中文目录迁移：先把旧目录里的 viewer.env 抢出来（比新目录里可能存在的更"真"）──
  ;    仅在 .onInit 判定为"旧中文默认路径"（$LEGACYDIR 非空）时执行。
  StrCmp $LEGACYDIR "" no_legacy_backup
    IfFileExists "$LEGACYDIR\viewer.env" 0 no_legacy_backup
      CopyFiles "$LEGACYDIR\viewer.env" "$PLUGINSDIR\viewer.env.bak"
  no_legacy_backup:

  ; 升级保护：先把老师配的 viewer.env 备份，覆盖完再还原
  ; （默认包里的 viewer.env 是占位值，整目录覆盖会把真实配置冲回默认）
  IfFileExists "$INSTDIR\viewer.env" backup_env no_backup
  backup_env:
    CopyFiles "$INSTDIR\viewer.env" "$PLUGINSDIR\viewer.env.bak"
  no_backup:

  ; 复制运行时全量
  ; ───────────────────────────────────────────────────────────────────────
  ; ⚠️ 这里原来写的是 `File /r "..\deploy\*"`，打出来的是一个**装了也跑不起来**的包
  ;    （2026-10-08 实测）：deploy/ 是 Qt DLL 的「素材库」，不是构建产物，它缺三样
  ;      ① resources/ + translations/ + position/ —— windeployqt 产出，只在 build/ 有
  ;         （缺了 → 照常启动、照常连云端，**只在要看远程画面时崩**，连日志都懒得留）
  ;      ② qml/Stelarith/ —— 我们自己的界面本体（缺了 → QML 加载失败 → 双击没反应）
  ;      ③ exe 还是旧的（停在 10-04）
  ;    现改为：**以 build/ 为主源**（那里才是刚编的产物 + windeployqt 补全的运行时），
  ;    只有两个「构建从来不产出」的配置文件仍从 deploy/ 取。
  ;
  ; ⚠️ 目录一律写成 `File /r "..\build\platforms"` 这种**不带 \*.* **的形式：
  ;    带了 `\*.*` NSIS 会把目录内容平铺到安装根，Qt 却按 platform/ 这种相对路径找插件，
  ;    结果是插件都在、但一个都找不到。
  File "..\build\viewer-qt.exe"
  File "..\build\Qt6*.dll"
  File "..\build\QtWebEngineProcess.exe"

  File /r "..\build\platforms"
  File /r "..\build\imageformats"
  File /r "..\build\iconengines"
  File /r "..\build\generic"
  File /r "..\build\networkinformation"
  File /r "..\build\styles"
  File /r "..\build\tls"
  File /r "..\build\resources"        ; ← WebEngine：icudtl.dat / *.pak
  File /r "..\build\translations"
  File /r "..\build\position"
  File /r "..\build\qml"              ; ← 一次性带上 Qt 官方模块 + 我们的 qml/Stelarith/

  ; 这两个是配置文件，构建不产出它们；deploy/ 才是它们的家
  File "..\deploy\viewer.env"
  File "..\deploy\start-viewer.cmd"

  ; 测试用的离屏插件不上教室机（自检时才需要，进包只会让人误会它有别的用途）
  Delete "$INSTDIR\platforms\qoffscreen.dll"
  ; deploy/ 里的调试残留
  Delete "$INSTDIR\v-err.txt"

  ; 还原 viewer.env（仅当升级前确有这份配置）
  IfFileExists "$PLUGINSDIR\viewer.env.bak" restore_env no_restore
  restore_env:
    CopyFiles "$PLUGINSDIR\viewer.env.bak" "$INSTDIR\viewer.env"
  no_restore:

  ; ── 迁移收尾：删掉旧的「中文」安装目录（仅迁移时执行）──
  ;    旧目录里的程序已被覆盖到新目录，这里只清残留；老师配置已在上面抢出来。
  StrCmp $LEGACYDIR "" no_legacy_cleanup
    RMDir /r "$LEGACYDIR"
    RMDir "$PROGRAMFILES64\Stelarith"   ; 旧父目录空了就顺手收掉（被控端另装在 $PROGRAMFILES64\Stelarith，非空则自动跳过）
  no_legacy_cleanup:

  ; 卸载器
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; 注册表：安装路径 + 卸载入口（控制面板"程序和功能"能看见、能卸）
  WriteRegStr HKLM "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION_RAW}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Stelarith"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE},0"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

; 开始菜单 + 桌面快捷方式（入口清晰，老师双击就能开）
; ⚠️ SetShellVarContext all：写到**所有用户**那一份（公共桌面 / 公共开始菜单）。
;    机房这台机器常常是管理员装、老师另一个账户用 —— 只建当前用户那份的话，
;    换账户后桌面空空如也，会被当成"没装上"。用完必须切回 current，
;    否则下面的 WriteRegStr 会跟着上下文跑偏、卸载键对不上。
SetShellVarContext all
CreateDirectory "$SMPROGRAMS\Stelarith"
CreateShortcut "$SMPROGRAMS\Stelarith\${APPNAME}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
CreateShortcut "$SMPROGRAMS\Stelarith\卸载 ${APPNAME}.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
SetShellVarContext current
SectionEnd

; ── 卸载段 ──
Section "Uninstall"
  ; 停掉正在跑的实例，否则 exe/Qt DLL 被占用，RMDir 会剩下半截目录
  nsExec::ExecToLog 'powershell -NoProfile -Command "Get-Process -Name viewer-qt -ErrorAction SilentlyContinue | Stop-Process -Force"'
  Sleep 600

  ; ⚠️ 必须切到 all 上下文：安装时 SetShellVarContext all 建的是**公共**桌面/开始菜单，
  ;    这里用默认的 current 删，只会去删另一个空目录，桌面上那枚 lnk 永远删不掉。
  SetShellVarContext all
  Delete "$SMPROGRAMS\Stelarith\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\Stelarith\卸载 ${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\Stelarith"
  SetShellVarContext current

  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "${REGKEY}"
SectionEnd
