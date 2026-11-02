// viewerbackend 跨 TU 共享助手的实现（2026-11-02 · 屎山整理 #6）
//
// 这三个助手原本是 viewerbackend.cpp 里的 static / 匿名命名空间函数（内部链接）。
// 拆出五个域文件后它们成了未定义符号，所以升为外部链接并集中到这里 ——
// **只有一份真源**。为什么不在每个域各抄一份：抄两份必然漂移，
// 改一处漏一处，表现是「某些路径下画面该停却不停」这种极难复现的错。
//
// 逻辑与原实现逐字一致，只改了函数名（加 viewer 前缀防外部链接撞名）。
// 由 `_audit_viewerbackend_split.py` 的 E 断言逐字比对这部分没被顺手改坏。

#include "viewerbackend_shared.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

// ── 画面指纹 ────────────────────────────────────────────────────────────────
// 为什么不用「JPEG 字节全等」来判：被控端每次抓屏都会重新编码一次，
// 同一张画面编出来的字节不可能一模一样，字节比较会永远判不出静态。
quint64 viewerFrameFingerprint(const QImage &img)
{
    if (img.isNull()) return 0;
    const qsizetype total = static_cast<qsizetype>(img.sizeInBytes());   // Qt 里没有 byteCount()，别照抄旧代码
    if (total <= 0) return 0;
    const uchar *p = img.constBits();
    quint64 h = (quint64)img.width() << 32 | (quint64)img.height();
    constexpr qsizetype stride = 7919;    // 质数：抽样点不会周期性落在同一片区域
    for (int k = 0; k < 4096; ++k) {
        h = h * 131 + p[(qsizetype)k * stride % total];
    }
    return h;
}

// ── RFB → 主 WS 二进制帧 ────────────────────────────────────────────────────
// 出站帧序号（仅调试/丢包检测，非协议强依赖）。搬到共享层时一并带上，
// 否则 vnc 域调用它会拿到未定义符号。
static quint64 g_vncOutSeq = 0;

QByteArray viewerMakeVncFrame(const QString &uid, const QByteArray &rfb)
{
    QJsonObject h;
    h.insert(QStringLiteral("v"), 1);
    h.insert(QStringLiteral("type"), QStringLiteral("vnc-data"));
    h.insert(QStringLiteral("mime"), QStringLiteral("application/vnc"));
    h.insert(QStringLiteral("uid"), uid);
    h.insert(QStringLiteral("direction"), QStringLiteral("down")); // 管理端→被控端
    h.insert(QStringLiteral("seq"), double(++g_vncOutSeq));
    const QByteArray hb = QJsonDocument(h).toJson(QJsonDocument::Compact);
    QByteArray out;
    out.reserve(3 + hb.size() + rfb.size());
    out.append(char(1));                                  // 版本
    out.append(char((hb.size() >> 8) & 0xFF));            // 大端头长
    out.append(char(hb.size() & 0xFF));
    out.append(hb);
    out.append(rfb);
    return out;
}

// ── 开机自启注册表位置 ──────────────────────────────────────────────────────
const char *const kViewerRunKey =
    "HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run";
const char *const kViewerRunValue = "StelarithViewer";

// ── 桌面快捷方式路径 ────────────────────────────────────────────────────────
QString viewerDesktopLnk()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return d.isEmpty() ? QString() : (d + QStringLiteral("/星集控管理端.lnk"));
}
