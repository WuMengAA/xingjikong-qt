#pragma once
// 2026-10-27 · I 批次（第七刀）：**PPT 放映控制域**（PowerPoint / WPS 演示）。
//
// 产品口径（用户 00:46 原话）：「**尤为重要的 PPT 翻页工具等 PowerPoint 插件支持**」。
// 老师上课的真实动线是：站在讲台 → 手里一支翻页笔（或触摸屏）→ 要能翻 PPT。
// 而我们要替代 ICC CE 的那部分能力里，最常用的就是「上一页 / 下一页 / 跳到第 N 页」。
//
// ── 为什么独立成域（而不是塞回 main.cpp）────────────────────────────────
//  ① main.cpp 第八刀正在拆，任何新能力一律独立文件（用户明确要求）；
//  ② COM 调用有大量"试探 + 兼容 + 如实报错"的分支，塞进指令分发会把它再次撑肥；
//  ③ 域内可被独立探针链接验证（不启动整个 agent 就能验 COM 路径）。
//
// ── 依赖（决策见接续清单「一·五」第 2 条）──────────────────────────────
//  **手写 Win32 COM + IDispatch，不装 ActiveQt**。
//  本机 Qt 6.12 未装 QtAxContainer；且工程已有 `CoCreateInstance` 先例
//  （src/screen_capture_dxgi.cpp 的 CoInitializeEx、main.cpp 的 SAPI TTS）。
//  ⇒ 只多链一个 oleaut32.lib（GetActiveObject 在那边）。
//
// ── ⚠️ 本域最关键的一条纪律：**绝不"拉起" PowerPoint** ──────────────────
//  `CoCreateInstance(L"PowerPoint.Application")` 的行为是：**没开就启一个**。
//  一条 `ppt_status`（查状态）指令在老师机器上凭空弹出 PowerPoint 窗口，
//  是比"查不到"严重得多的事故。所以本域**只用 `GetActiveObject`**
//  （连到已在运行的实例），不 CoCreateInstance。
//  要启动 PPT 走既有 `launch_app`（名单里已有 powerpnt/wps），职责不混。
//
// ── ⚠️ WPS 与 MS Office 的 COM 互斥（ICC CE 原文坑，必须绕开）──────────
//  ICC CE 文档原话：「若要启用 WPS 支持，请确保 WPS 是否在『配置工具』中开启了
//  『WPS Office 相容第三方系统和软件』选项，该项目勾选并应用后，**将无法检测到
//  MS Office 的 COM 接口**。」
//  ⇒ 两者可能只有其一可达。本域的处理：
//    · 偏好可设（auto / powerpoint / wps），落 `ppt.json`；
//    · **每一次回执都带上"这次实际控的是哪个"**（`app` 字段），
//      绝不"悄悄切到 WPS 让老师以为在控 Office"；
//    · 指定了 powerpoint 而只探到 WPS 时 → **如实 failed**，并在 error 里说清
//      "现在是 WPS 可达，要控它请显式切到 wps"。
#include <QString>
#include <QStringList>

namespace PptDomain {

// 运行状态。**ok/error 描述"这次查询本身"，presenting/slide/total 描述 PPT 的实况** ——
// 两者分开，才不会把"没在放映"和"COM 连不上"混成一句"失败"。
struct Status {
    bool    ok = false;         // 查询链路是否走通（连上 COM 并拿到状态）
    QString error;              // 失败原因（ok=false 时必填，给老师看得懂的话）
    QString app;                // 实际控住的应用："powerpoint" / "wps"；空 = 一个都没连上
    bool    running = false;    // 该应用的 COM 服务在跑（能 attach 上）
    bool    presenting = false; // 正在放映
    int     slide = 0;          // 当前页（1 基）；未放映 = 0
    int     total = 0;          // 总页数；未知 = 0
    QString presentation;       // 演示文稿名（可空）
    QStringList available;      // 本次探测中**可达**的应用（用来说明"其实有 WPS 在跑"）
};

// 读/写偏好（"auto" / "powerpoint" / "wps"）。落盘失败不致命（内存值仍生效），
// 但会在上一次 setPreferredApp 的返回值里如实报 false。
void init(const QString &settingsFile);

QString preferredApp();
// 返回 false 只代表**取值非法**（未生效）；落盘成败另由 saved 报（值本身已在内存生效）。
bool setPreferredApp(const QString &app, QString *error, bool *saved = nullptr);

// 只 attach，**绝不启动**。见文件头「绝不拉起 PowerPoint」。
Status query();

// 翻页 / 跳页：成功后 out 里是**翻完之后的真实页码**（重新读回来的，不是把请求值回显）。
// 失败时返回 false 并写 error（没开 PPT / 没在放映 / 放映窗口取不到）。
bool step(int delta, Status *out, QString *error);
bool gotoSlide(int index, Status *out, QString *error);

// I3（与批注层联动）预留的**唯一**接缝：翻页后由调用方自己 `query()` 取新页码。
// 本批不做轮询钩子 —— 没有消费者的定时器是纯负担，等 H（批注内核）落地后
// 在 annotate_domain 里按页存取墨迹时再定"推送还是拉取"。

}   // namespace PptDomain
