# 星集控 · Android 平台差异点（2026-10-13 建立）

> 目的：`#if defined(Q_OS_ANDROID)` 分支散落在各工程各处，本文件是**唯一清单**——
> 排查 Android 问题时对着它过一遍，不会漏。新增 Android 分支时先看这里，改动后更新这里。
> 原则：平台差异藏在**实现**里（空桩/条件编译），调用方尽量无感知。

## 一、viewer-qt（管理端/手机 App）

| 位置 | 差异 | 说明 |
|---|---|---|
| `audio_capture.{h,cpp}` vs `audio_capture_android.cpp` | 桌面 winmm waveIn；Android AAudio（API 26+）| CMake `STE_AUDIO_CAPTURE_SRC` 选源；16kHz/单声道/16bit PCM，20ms 帧 |
| `audio_playback.cpp` | Android 空桩（`Q_OS_ANDROID` 分支不实现）| 桌面 waveOut；Android 无 waveOut |
| `singleinstance.h` | Android 无 Windows 互斥体 | `#ifndef Q_OS_ANDROID` 包 `windows.h` + `HANDLE m_mutex`；Android 只走 QLockFile 文件锁 |
| `main.cpp` QML 加载 | Android 用 `qrc:/Main.qml`（QML 整树嵌 qrc）；桌面磁盘加载 | Android 优先本地热更 `qml-live/`，无则回退 qrc；加载按来源分支（qrc 用 QUrl，磁盘用 fromLocalFile）|
| `main.cpp` 软渲染 | Android 可开软件渲染（`QT_QUICK_BACKEND`/GPU 问题开关）| 2026-10-11：桌面 Main.qml 手机 GPU completion SIGABRT → 软件渲染开关 |
| `viewerbackend.cpp` 权限 | Android 运行期权限（`QtAndroidPrivate::requestPermission`）| RECORD_AUDIO（语音）/ POST_NOTIFICATIONS（通知栏）/ 读文件 SAF 不需要权限 |
| `viewerbackend.cpp` 语音 | Android `startSpeaking` 先要 RECORD_AUDIO，授权回调才开麦 | 桌面直接采 |
| `viewerbackend.cpp` 文件选择 | Android SAF（ACTION_GET_CONTENT）异步 + 拷缓存；桌面 QFileDialog 同步 | `pickFile()` Android 分支 + `copyContentUriToCache()` |
| `viewerbackend.cpp` 通知 | Android 系统通知栏（`android_notify.cpp` QJniObject + NotificationChannel）| 桌面走托盘/灵动岛 |
| `viewerbackend.cpp` QML 热更 | `checkQmlPatch` + 自写 zip 解压（unzipStore）| Android 本地 qml-live 优先 |
| `viewerbackend.cpp` JNI | 大量 QJniObject/QJniEnvironment（SAF、权限、缓存路径）| 含 zip slip 防护（`pathInside`）|
| `main.cpp` 单实例 | Android 跳过（`STE_SINGLEINSTANCE_SRC` 空）| 多开无害 |
| `main.cpp` app.rc | Android 不编 `.rc` 版本资源 | CMake `STE_APP_RC` 空 |

## 二、control-qt（被控端 agent）

| 位置 | 差异 | 说明 |
|---|---|---|
| `singleinstance.h` | 同 viewer（Android 条件编译已回灌 2026-10-12）| 两端字节一致承诺 |
| 音频/通知 | 当前 Windows-only（无 Android 分支）| 被控端是教室机 Windows 为主 |
| `main.cpp` setApplicationName | 2026-10-12 补齐（QSettings 落点）| 与管理端对齐 |

## 三、云端（Stelarith-cloud）

- 无 Android 代码——纯 Node.js 服务。

## 四、测试/验证清单（Android 实机）

- 语音：首次按住讲话弹 RECORD_AUDIO 授权框 → 授权后开麦 → 松手停
- 通知：notify 动作 → 通知栏横幅（Android 13+ 需 POST_NOTIFICATIONS）
- 文件：文件传输 → SAF 系统选择器弹窗 → 选文件 → 进度 → 被控端落盘
- 后台保活：切后台 5 分钟 → 回前台立即重连（watchdog 45s 半开判定）
- 热更：`checkQmlPatch` → 下载 zip 解压 qml-live → 下次启动用新 UI

## 五、新增 Android 分支的规矩

1. 先查本文件是否已有该差异点（有 → 参考实现；没有 → 新增后更新本文件）
2. 平台差异尽量收敛到**独立 .cpp**（如 audio_capture_android.cpp / android_notify.cpp），
   别把 `#ifdef` 散进长函数
3. CMake 用 `STE_IS_ANDROID`（`CMAKE_SYSTEM_NAME` 判定，非裸 ANDROID 变量）
4. Android 构建验证：`make-apk.ps1`（qt-cmake → build → 拷 .so → gradle → apksigner）
