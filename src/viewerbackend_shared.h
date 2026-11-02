// viewerbackend 跨翻译单元共享的助手（2026-11-02 · 屎山整理 #6）
//
// ## 为什么需要这个文件
// viewerbackend.cpp 从 2995 行拆出五个域文件后，编译立刻报三类符号找不到：
//   · `makeVncFrame` / `g_vncOutSeq` —— 原本 static（文件内链接）
//   · `frameFingerprint` / `kStaticStreak` —— 原本在匿名命名空间（内部链接）
//   · `viewerDesktopLnk` —— 原本 static
// 内部链接的东西，换个 TU 就看不见了 —— 这不是「拆错了」，是「拆之前它们是私有的」。
// C++ 允许同类成员函数跨 TU（private 照访），但**不允许内部链接的自由函数跨 TU**，
// 两者不要混为一谈。
//
// ## 处置原则
// 这些助手有 2 个以上 TU 在用，所以**必须**升为外部链接（而不是在每个域里各抄一份 ——
// 抄两份必然漂移，改一处漏一处，表现是「某些路径下画面不变、另一些变」这种极难复现的错）。
// 实现统一放 `viewerbackend_shared.cpp`，只有一份真源。
//
// ⚠️ 命名一律加 `viewer` 前缀：这些是全局自由函数，`frameFingerprint` 这种通用名
// 放在外部链接里迟早和别的模块撞名（链接期报重复定义，报错点在调用处，很难查）。

#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

QT_BEGIN_NAMESPACE
class QImage;
QT_END_NAMESPACE

/**
 * 静态区判定的连续帧数阈值。
 *
 * 为什么放头文件里当 constexpr：每个 TU 各持一份同值常量，无链接开销，
 * 且改阈值只需改这一处 —— 这正是内部链接做不到的事。
 * 8 帧 ≈ 0.5 秒（被控端 15fps）：太短会误判，太长画面发僵。
 */
constexpr int kStaticStreak = 8;

/**
 * 画面指纹：宽高 + 整块缓冲里的伪随机采样和。同尺寸同画面 → 同指纹。
 *
 * 为什么不用「JPEG 字节全等」来判：被控端每次抓屏都会重新编码一次，
 * 同一张画面编出来的字节不可能一模一样，字节比较会永远判不出静态。
 */
quint64 viewerFrameFingerprint(const QImage &img);

/**
 * 把一段原始 RFB 字节包成主 WS 的 vnc-data 二进制帧（信封格式与画面帧一致）。
 *
 * 内部维护出站序号（仅调试/丢包检测用，非协议强依赖）。
 */
QByteArray viewerMakeVncFrame(const QString &uid, const QByteArray &rfb);

/**
 * 开机自启的注册表位置与值名。
 *
 * 为什么是 extern const char*：两处要用（proc 域写注册表、诊断域展示当前状态），
 * 而写死字符串两遍就是两份真源 —— 改一处漏一处会让「界面说已自启、实际没启」。
 */
extern const char *const kViewerRunKey;
extern const char *const kViewerRunValue;

/**
 * 桌面快捷方式（.lnk）的完整路径。
 *
 * 放在共享层是因为两处都要它：开机自启/建快捷方式（proc 域）与诊断信息展示。
 */
QString viewerDesktopLnk();
