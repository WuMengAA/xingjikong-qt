# Qt for Android 可行性评估（2026-10-09）

> 范围：星集控两端（被控端 control-qt / 管理端 viewer-qt）移植到 Android 的可行性。
> 依据：Qt 6.8 官方文档 + 本机工具链实测 + 两端源码依赖盘点（grep blob）。
> 结论：**管理端可行（工作量中等）；被控端基本不可行（职责与 Android 模型冲突）。**

---

## 1. 环境事实（本机实测）

| 项 | 状态 | 说明 |
|----|------|------|
| Android SDK | ✅ 完整 | `D:\Android\Sdk`：platforms **31/33/34/35/36**、build-tools 34-37、NDK **28.2.13676358**、cmake、emulator、system-images |
| JDK | ✅ 17.0.12 LTS | 正合 Qt 6.8 要求（JDK 17）|
| Gradle/AGP | ⚠️ 需装 | Qt 6.8 要 Gradle 8.14.3 + AGP 8.10.1 |
| **Qt Android 套件** | ❌ **缺** | 只有 msvc2022_64；需 Qt 维护工具加装 `android_arm64_v8a`（Qt for Android 库二进制）|
| mkspec | ✅ 有 | msvc2022_64 内含 `mkspecs/android-clang`——交叉编译配置存在 |

## 2. 官方支持矩阵（Qt 6.8，[doc.qt.io/qt-6.8/mobiledevelopment.html](https://doc.qt.io/qt-6.8/mobiledevelopment.html)）

| 项 | 官方要求 | 本机 | 匹配 |
|----|---------|------|------|
| Android 版本 | API 28–36（Android 9–16）| platforms 31–36 | ✅ |
| NDK | **r26b / r27c**（Clang 17.0.2）| **28.2**（更新）| ⚠️ 官方"recommended same NDK as Qt build"；28 通常向后兼容但**首选降到 r26/r27** |
| JDK | 17 | 17.0.12 | ✅ |
| 架构 | arm64-v8a / x86_64 / x86 / armeabi-v7a | —（Qt 套件装齐后可用）| ✅ |
| 打包 | CMake 多 ABI APK/AAB | — | ✅ |

## 3. 两端依赖盘点（grep blob，非看提交信息）

### 3.1 被控端 control-qt —— ❌ 重度 Windows 绑定

| 依赖 | 处数 | Android 状态 |
|------|------|-------------|
| QSystemTrayIcon（托盘）| 12 | ❌ Android **无托盘概念**（需整个去掉/改前台服务）|
| waveOut/winmm（音频）| 8 | ❌ Windows 专属，Android 用别的（Qt Multimedia AudioSink）|
| QProcess + ffmpeg（编解码/录屏）| 49 | ❌ Android **无 PATH/任意进程 spawn**，沙箱模型不允许 |
| QWebEngine（Chromium 采集页）| 12 | ❌ **Qt for Android 不支持 WebEngine** |
| WASAPI / SAPI（TTS）| 9 | ❌ Windows 专属 |
| windows.h 原生 API | 1 | ❌ |

### 3.2 管理端 viewer-qt —— ✅ 可行（QML 为主）

| 依赖 | 处数 | Android 状态 |
|------|------|-------------|
| QSystemTrayIcon | 0 | ✅ 无 |
| QWebEngine（离屏收流）| 15 | ⚠️ 不支持——**但我们的 libdatachannel 方案（ldc_receiver）恰好替代** |
| QSettings | 7 | ✅ Android 有（映射系统设置）|
| 网络（WS/HTTP）| 13 | ✅ Android 网络栈正常 |
| winmm 音频 | 1 | ⚠️ 语音播放，换 Qt Multimedia |

## 4. 可行性结论

### 管理端 → Android（老师用平板/手机管理教室）—— ✅ 可行，中等工作量
- 技术栈 **QML + 网络 + QSettings** 全部 Android 原生支持
- 唯一硬障碍 QWebEngine 收流 **已被「换 other webrtc」方案绕过**（libdatachannel 跨平台，C++ 库可在 Android 交叉编译）
- 工作项：
  1. 装 Qt android_arm64_v8a 套件 + Gradle/AGP
  2. NDK 降到 r26/r27（官方推荐）
  3. 语音播放 waveOut → Qt Multimedia
  4. 触控适配（大屏按钮/间距）——项目本就设计触控（QTouchEvent）
  5. UI 密度适配平板（当前 1180×700 窗口模型 → Android 全屏）

### 被控端 → Android —— ❌ 不建议
- 职责冲突：被控端是**教室机常驻代理**（托盘、计划任务、全屏拦截、ffmpeg 录屏、SAPI TTS）——全部依赖 Windows 桌面模型
- Android 是移动沙箱：无托盘、后台受限（Android 12+ 前台服务限制）、不能 spawn ffmpeg 进程、无全屏系统级拦截
- 即使强移，也是「重写」而非「移植」——性价比极低
- **除非**业务变成「学生 Android 平板被控」（全屏考试/广播），那是全新产品形态，需 Android 原生 SDK 而非 Qt

## 5. 建议路线（若要做）

```
Phase 0（0.5 天）：装 Qt android_arm64_v8a 套件 + Gradle 8.14.3；NDK 降到 r26b
Phase 1（1–2 天）：viewer-qt 最小 Android 构建（去掉 WebEngine 依赖 → ldc_receiver）+ 跑通 APK
Phase 2（2–3 天）：触控/平板 UI 适配 + 语音播放换 Multimedia + 打包 AAB
验证：Android 模拟器（本机有 system-images）或真机
```

## 6. 风险与未验证项

- **未验证**：NDK 28.2 与 Qt 6.8.1 的兼容性（官方只保证 r26b/r27c；28 可能可用但无承诺）
- **未验证**：libdatachannel 在 Android 的交叉编译（依赖 OpenSSL——Android 需打包 libcrypto/ssl .so，官方有 [android-openssl-support](https://doc.qt.io/qt-6.8/android-openssl-support.html) 指引）
- 管理端现有 `oauthlogin.cpp` 用 QWebEngine 做 OAuth 窗（15 处之一）——Android 需换 Android 系统浏览器/WebView
- 桌面端「托盘常驻/关窗隐藏」语义在 Android 不存在——UI 生命周期需重设计

---

*评估基于 2026-10-09 实测 + Qt 6.8 官方文档；环境缺 Qt Android 套件是唯一硬性前置（一次维护安装）*
