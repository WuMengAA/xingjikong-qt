# Stelarith-xingjikong-qt · 长期项目笔记

## ⚠️ 被控端 control-qt 有两份副本，已经分叉（2026-10-05 发现）

| 路径 | git | 内容 | 现状 |
|---|---|---|---|
| `D:\Stelarith\Stelarith-control-qt` | 自己的仓库（commit `0733bf2`） | 有 `src/singleinstance.{h,cpp}`（单实例守卫，2026-10-05 加），exe 315,392 B | 已打 NSIS、已同步云端（`stelarith-agent-setup-0.5.1.exe` 70,664,927 B） |
| `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt` | 本仓库（xingjikong-qt）把该目录当 **gitlink/子模块条目** 记着，实际是普通目录副本 | `src/` 里**只有 main.cpp**，没有 singleinstance；2026-10-05 晚改了全屏通知（palette 背景 / ✓确认按钮 / paintEvent 圆角），exe 337,408 B | **缺单实例守卫** |

- 两份 `src/main.cpp` 的 md5 不同（`dc50f731…` vs `a6316fb1…`）。
- 权威发布链路以 **`D:\Stelarith\Stelarith-control-qt`** 为准（打包/NSIS/云端都走它）。
- 在 xingjikong-qt 副本里改代码前**先问用户**：是把它升级成权威副本，还是把改动移植回
  `D:\Stelarith\Stelarith-control-qt`。两边不同步会直接导致"打出来的包没有某个修复"。
- 同理 `Stelarith-viewer-qt`、`Stelarith-cloud-ws` 在 xingjikong-qt 下也各有一份副本。

## ⚠️ control-qt 两份副本的 diff 必须先归一化行尾（2026-10-05 踩到）

- `D:\Stelarith\Stelarith-control-qt\src\main.cpp` 是 **LF**；
  `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt\src\main.cpp` 是 **CRLF**。
- 直接 `diff` 两份会得到 `@@ -1,3064 +1,3376 @@` 一条巨型 hunk（"整文件都改了"的假象）。
  必须先两边 `.replace(b'\r\n', b'\n')` 归一化再 diff，真实差异是 15 块 / 529 行。
- 内容分叉现状：通知三改（全屏 palette / ✓确认按钮 / paintEvent 圆角）**两边都有**了
  （`D:\Stelarith` 那份 commit `6f5d99c` 移植的）；**OTA 自更新整块 + `kAppVersion="0.5.0"`
  仍只在 xingjikong 那份里**，没移植过来（等用户点单）。

## 被控端打包链路（2026-10-05 实测）

1. `build-qt-agent.bat`（cmake + ninja + MSVC 18 BuildTools，cl.exe 在 `bin\Hostx64\x64`）
2. cmake/ninja 在 python 虚拟环境里 → 自写 `.bat` 必须把
   `C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts` 加进 PATH，否则 `cmake: 无法运行`。
3. `windeployqt.exe` 在 `D:\Qt\6.8.1\msvc2022_64\bin\` —— ⚠️ **它会把瘦身时删掉的
   `deploy/translations`（30 个 .qm，42MB）、`position/`、`qmltooling/`、`resources/`
   重新搬回来，跑完必须删回去**，否则安装包凭空涨 20MB（89MB→70MB 就是这么来的）。
4. `deploy/tls/q{schannel,certonly,openssl}backend.dll` **要留**（连云端 WSS/TLS 需要）。
5. `makensis` 在 `C:\Program Files (x86)\NSIS\makensis.exe`（`where` 找不到，别白查）。
6. 绿色包用 `C:\Program Files\7-Zip\7z.exe a -tzip -mx=1`；**别把 zip 打进包目录里**。

## 沙箱里的构建小坑

- Git Bash 会把 `/I<path>` 这类参数当路径转换 → 跑 cl.exe 加 `/I` 时加 `MSYS_NO_PATHCONV=1` 或干脆用 cmake 自己算 include。
- 工作区外（如 `%TEMP%`）cl.exe 读 `D:\Qt\...` 头会 C1083；临时探针工程要**放在工程目录内**。
- 临时验证用的探针/`.bat`/`build/` 验完就删，别留在仓库里。
