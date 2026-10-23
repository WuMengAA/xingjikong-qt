#pragma once
// 星集控被控端 · 班级域缩略图上报（#459 班级工作台第一环·端侧）
// 2026-10-23 · 与云端 class-events.js（#458）配对：agent→cloud→该班订阅者。
//
// ── 它是什么 ──────────────────────────────────────────────────────────
// 班级工作台（管理端）设备网格的画面源：每台被控端定时抓一张小图，编码成
// JPEG(base64)，以 class-event{kind:'thumbnail'} 经主 WS 出海。消费端在
// viewer（#460 ClassService），本模块只管「抓 → 编 → 交出海」，不管协议信封。
//
// ── 与 VNC 抓屏的关系（为什么不复用它）──────────────────────────────
// VncServer 的帧缓冲是私有的（m_fb），且它的节奏（10fps 720p）是「有人
// 远控时」才转的；缩略图是「看板开着就一直要」的。本模块**独立起一个
// 小尺寸 ScreenCapture 实例**（DXGI→GDI 门面自选，长边 320 保持比例）：
//   · DXGI 桌面复制（OutputDuplication）多实例合法（各 D3D11 device 各持
//     一份，互不干扰），成本 = 每 15s 一次 AcquireNextFrame(timeout=0 非阻塞)；
//   · 不动 VNC 热路径一行（它的帧率/脏区协议是远控专用，不掺和）。
//
// ── 闸门（与云端 #458 四道闸门对齐，端侧先把关）────────────────────
//  ① 未绑班 / WS 未连 → 本拍直接跳过（不抓屏，零空转——班级归属以云端
//     knownDevices 表为准，未绑班设备发了也会被云端拒收 ack ok:false）；
//  ② JPEG 三档降质（q50→35→25）守单条 ≤46KB（base64 后 ≤64KB 云端闸门
//     留出信封余量）；仍超 → 丢帧 + 告警，绝不发越界数据；
//  ③ 黑帧检测（抽样 64 点全零）→ 跳过（锁屏/登录桌面/纯 Session 0），
//     与 VNC 黑帧检查同款纪律——看板显示旧图胜过显示一块黑；
//  ④ payload **不带 classCode**（伪冒防护铁律：班级归属云端只认自己的表）。
//
// ── 零 QObject 依赖（故意的）────────────────────────────────────────
// 定时器由 main.cpp 的 QTimer 驱动（它认识 g_ws / g_classCode，本模块不
// 认识）；模块只有 tick() 纯逻辑 ⇒ 测试可直接静态链接，不起事件循环。
// 依赖方向：main → ThumbnailReporter；island 的 setCloudReporter 同款。

#pragma once

#include <QtGlobal>   // qint64
#include <QJsonObject>
#include <cstdint>
#include <functional>
#include <vector>

namespace ThumbnailReporter {

/// 帧源：返回 BGRX 紧凑缓冲（stride = w*4，小端内存布局 = QImage::Format_RGB32，
/// 见 screen_capture.h 像素格式段）。返回 nullptr = 本拍抓屏失败，调用方跳过。
/// 缓冲下次调用即被覆盖，调用方勿长期持有。
using FrameSource = std::function<const uint8_t *(int &w, int &h)>;

/// 发送器：main.cpp 注入（包好信封发给 g_ws）。payload = {kind, data}。
/// 模块不碰协议、不碰 g_ws（与 island 的 CloudReporter 同款注入）。
using Sender = std::function<void(const QJsonObject &payload)>;

/// 就绪门禁（绑班 && WS 已连），main.cpp 注入；未注入 = 恒就绪（测试默认）。
using ReadyCheck = std::function<bool()>;

// ── 注入（main.cpp 接线处 / 测试各调一次）────────────────────────────
void setFrameSource(FrameSource);
void setSender(Sender);
void setReadyCheck(ReadyCheck);

// ── 拍一拍（由 main.cpp 的 QTimer::timeout 驱动）──────────────────────
/// 跑一道完整流水线：就绪门禁 → 抓帧 → 黑帧检测 → JPEG 三档降质 → 发送。
/// 任何一步不过都安静跳过（记 stats，不抛异常出海——端侧上报是旁路，
/// 绝不因它反噬主流程）。@return true = 本拍真发了。
bool tick();

// ── 配置（env 现读，改值不用重启；间隔每拍重读）──────────────────────
bool enabled();          ///< STE_THUMB_REPORT（默认 1；"0"/"false"/"off" 关）
int intervalMs();        ///< STE_THUMB_INTERVAL_MS（默认 15000，clamp 2000..600000）
int maxEdge();           ///< STE_THUMB_MAX_EDGE（默认 320，长边，保持宽高比）

// ── 编码（独立可测，无抓屏依赖）──────────────────────────────────────
/// BGRX 紧凑帧 → JPEG QByteArray。base64 后体积守 kMaxJpegBytes：
/// q 起 50，超了依次降 35/25；仍超返回空（调用方丢帧，不越云端闸门）。
/// @param q 起始质量（测试可指定）
QByteArray encodeThumbJpeg(const uint8_t *bgrx, int w, int h, int q = 50);

// ── 合成帧（测试/探针共用，确定性：彩条 + 横向渐变，无随机）──────────
/// 造 w×h 的 BGRX 彩条帧（8 色竖条 + 从左到右的暗→亮斜坡）。
std::vector<uint8_t> makeSyntheticFrame(int w, int h);

// ── 统计（日志 / 探针断言用）────────────────────────────────────────
struct Stats {
    long long ticks = 0;       ///< tick() 被调次数
    long long sent = 0;        ///< 真发出次数
    long long skipped = 0;     ///< 门禁跳过次数（未就绪/抓屏失败/黑帧）
    long long dropped = 0;     ///< 降质三档仍超预算被丢次数
    long long lastBytes = 0;   ///< 最近一次发出的 JPEG 字节数
    long long lastW = 0, lastH = 0;
};
Stats stats();

} // namespace ThumbnailReporter
