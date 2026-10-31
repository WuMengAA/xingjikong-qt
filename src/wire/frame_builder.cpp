#include "frame_builder.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

// 协议版本（《星集控-协议规范v1-2026-10-03.md》）。仅被本文件三个打包函数使用，
// 随帧构建模块迁出 main.cpp 匿名域（main 拆分 #12）。
constexpr int kProtocolVersion = 1;

// 本机设备码（main.cpp 文件作用域外部链接，同 upload 模块 extern 复用模式）
extern QString g_uid;

// 远控 vnc-data 上行帧序号（2026-10-18 · 统一方案 A）：本地 VNC 产出的 RFB 字节 → 云端。
// 与画面帧同 v1 信封，区别：type=vnc-data / mime=application/vnc / direction=up / seq 自增
// （云端按 uid 原样中继到订阅该设备的管理端，不重写信封）。
static quint64 g_vncUpSeq = 0;

// 语音通话上行帧序号（2026-10-09）
static int g_audioUpSeq = 0;

QByteArray makeFrameBytes(const QString &uid, int seq, const QByteArray &jpeg)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("frame"));
    h.insert(QStringLiteral("uid"), uid);
    h.insert(QStringLiteral("seq"), seq);
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    h.insert(QStringLiteral("mime"), QStringLiteral("image/jpeg"));
    h.insert(QStringLiteral("bytes"), jpeg.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + jpeg.size());
    out.append(char(kProtocolVersion));                 // 1B 版本
    out.append(char((header.size() >> 8) & 0xFF));      // 2B 大端 headerLen
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(jpeg);
    return out;
}

// 远控 H264 上行帧序号（A 档第②步，2026-10-31）：被控端 MF 编码 NALU → 云端。
// 与 vnc-data 同 v1 信封、同中继语义，仅 type/mime 不同。
static quint64 g_h264UpSeq = 0;

// 远控 vnc-data 上行帧（2026-10-18 · 统一方案 A）：本地 VNC 产出的 RFB 字节 → 云端。
QByteArray makeVncFrameBytes(const QByteArray &rfb)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("vnc-data"));
    h.insert(QStringLiteral("uid"), g_uid);
    h.insert(QStringLiteral("seq"), QJsonValue(static_cast<qint64>(++g_vncUpSeq)));
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    h.insert(QStringLiteral("mime"), QStringLiteral("application/vnc"));
    h.insert(QStringLiteral("direction"), QStringLiteral("up"));
    h.insert(QStringLiteral("bytes"), rfb.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + rfb.size());
    out.append(char(kProtocolVersion));                 // 1B 版本
    out.append(char((header.size() >> 8) & 0xFF));      // 2B 大端 headerLen
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(rfb);
    return out;
}

// 语音通话上行帧（2026-10-09 · 语音通话设计 §4.1）：学生 mic → 云端。
// 与画面帧同 v1 结构，区别在 mime（云端据此走 duplex 单播回老师）。
// 2026-10-21（P1）：载荷从裸 PCM 换成 Opus 包 —— mime=audio/opus 标记编码类型。
// ⚠️ 必须**按 mime 区分而不是按会话态猜**：滚动升级期间新旧端共存，
//    旧管理端只认 audio/pcm（裸 PCM），新管理端两种都能解。
QByteArray makeAudioFrameBytes(const QByteArray &payload, bool opus)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("frame"));
    h.insert(QStringLiteral("uid"), g_uid);
    h.insert(QStringLiteral("seq"), ++g_audioUpSeq);
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    // ⚠️ QStringLiteral 只接受字面量（宏展开成 u"" 运算符），不能塞三元表达式。
    h.insert(QStringLiteral("mime"),
             opus ? QStringLiteral("audio/opus") : QStringLiteral("audio/pcm"));
    h.insert(QStringLiteral("bytes"), payload.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + payload.size());
    out.append(char(kProtocolVersion));
    out.append(char((header.size() >> 8) & 0xFF));
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(payload);
    return out;
}

// 远控 H264 上行帧（A 档第②步，2026-10-31）：被控端 MF 编码出的 annex-B NALU → 云端。
// 与 vnc-data 同 v1 信封、同中继语义（云端按 uid 原样中继到订阅该设备的管理端）；
// 区别：type=h264-data / mime=video/h264-annexb / direction=up / seq 自增。
QByteArray makeH264FrameBytes(const QByteArray &nalu)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), kProtocolVersion);
    h.insert(QStringLiteral("type"), QStringLiteral("h264-data"));
    h.insert(QStringLiteral("uid"), g_uid);
    h.insert(QStringLiteral("seq"), QJsonValue(static_cast<qint64>(++g_h264UpSeq)));
    h.insert(QStringLiteral("ts"), QJsonValue(QDateTime::currentMSecsSinceEpoch()));
    h.insert(QStringLiteral("mime"), QStringLiteral("video/h264-annexb"));
    h.insert(QStringLiteral("direction"), QStringLiteral("up"));
    h.insert(QStringLiteral("bytes"), nalu.size());
    const QByteArray header = QJsonDocument(h).toJson(QJsonDocument::Compact);

    QByteArray out;
    out.reserve(3 + header.size() + nalu.size());
    out.append(char(kProtocolVersion));                 // 1B 版本
    out.append(char((header.size() >> 8) & 0xFF));      // 2B 大端 headerLen
    out.append(char(header.size() & 0xFF));
    out.append(header);
    out.append(nalu);
    return out;
}
