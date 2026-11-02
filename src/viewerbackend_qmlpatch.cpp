// QML 热更补丁（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）
//
// // 搬出理由：热更补丁是纯文件操作（下载完成后的落盘与解压），不碰网络。

#include "viewerbackend.h"
#include "viewerbackend_shared.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

void ViewerBackend::checkQmlPatch()
{
#if defined(Q_OS_ANDROID)
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    const QString manifestUrl = httpUrl + QStringLiteral("/api/qml-patch");
    QNetworkRequest req{ QUrl(manifestUrl) };
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, httpUrl, token]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) { logf("[viewer] qml-patch 清单失败（HTTP %d）", status); return; }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject obj = doc.object();
        const QString ver = obj.value(QStringLiteral("version")).toString();
        if (ver.isEmpty()) return;
        const QString liveDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/qml-live");
        const QString verFile = liveDir + QStringLiteral("/.version");
        if (QFile::exists(verFile)) {
            QFile vf(verFile);
            if (vf.open(QIODevice::ReadOnly)) {
                const QString localVer = QString::fromUtf8(vf.readAll()).trimmed();
                if (localVer == ver) { logf("[viewer] qml-patch 已是最新 %s", ver.toUtf8().constData()); return; }
            }
        }
        // 版本不同 → 下载 zip
        const QString zipUrl = httpUrl + QStringLiteral("/api/qml-patch/download");
        QNetworkRequest dz{ QUrl(zipUrl) };
        if (!token.isEmpty()) dz.setRawHeader("Authorization", "Bearer " + token);
        QNetworkReply *zr = m_nam->get(dz);
        connect(zr, &QNetworkReply::finished, this, [this, zr, liveDir, ver]() {
            zr->deleteLater();
            const int zs = zr->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (zs != 200) { logf("[viewer] qml-patch 下载失败（HTTP %d）", zs); return; }
            const QByteArray zipData = zr->readAll();
            // 自写 store-zip 解压（云端 qml-patch 是 store 模式无压缩）：
            // 读 EOCD → 中央目录 → 每条本地头 → 直接拷数据。不依赖 Qt private API。
            QDir().mkpath(liveDir);
            const bool ok = unzipStore(liveDir, zipData);
            if (ok) {
                { QFile vf(liveDir + QStringLiteral("/.version"));
                  if (vf.open(QIODevice::WriteOnly)) { vf.write(ver.toUtf8()); } }
                logf("[viewer] qml-patch 更新到 %s，下次启动生效", ver.toUtf8().constData());
            } else {
                logf("[viewer] qml-patch 解压失败");
            }
        });
    });
#endif // Q_OS_ANDROID
}
// ── store 模式 zip 解压（qml-patch 用；无压缩，只读中央目录拷数据）──
// 格式：中央目录每条 46 字节头（sig 0x02014b50）+ 名字 + 本地文件头 30 字节 + 数据。
bool ViewerBackend::unzipStore(const QString &outDir, const QByteArray &zip)
{
    // ⚠️ 2026-10-12 修（安全审计 #4/#5）：zip slip 任意路径写 + 越界读。
    //   · name 直接取自 zip 中央目录，原来没做路径规范化 —— 一个名称含
    //     `../../../Start Menu/Programs/Startup/evil.bat` 的 zip 能在任意路径落文件。
    //   · cdOff/cdSize/lho/compSize 全来自内容，原来无边界检查可直接越界读内存。
    const QString root = QDir::cleanPath(outDir);
    const int rootLen = root.length();

    // 路径必须落在 outDir 内（含反斜杠当分隔符规范化；首段不允许 ..）
    auto pathInside = [&root, rootLen](const QString &raw) -> QString {
        // 反斜杠（Windows 分隔符）也当分隔符处理 → replace('/') 后 cleanPath 统一
        QString p = raw;
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString canon = QDir::cleanPath(root + QLatin1Char('/') + p);
        if (!canon.startsWith(root + QLatin1Char('/')) && canon != root) return QString();
        // 绝对路径（如 C:/x）会被 cleanPath 冲掉前缀 → 必然不 startsWith root → 上面已拒
        if (QDir::isAbsolutePath(raw)) return QString();
        return canon;
    };

    // 找 EOCD（结尾 22+ 字节，sig 0x06054b50）
    for (int i = zip.size() - 22; i >= 0; --i) {
        if (zip.size() - i < 22) break;
        const uchar *p = (const uchar *)zip.constData() + i;
        if (p[0] == 0x50 && p[1] == 0x4b && p[2] == 0x05 && p[3] == 0x06) {
            quint32 cdSize, cdOff;
            memcpy(&cdSize, p + 12, 4);
            memcpy(&cdOff, p + 16, 4);
            // 中央目录偏移/大小必须整段落在 zip 内
            if ((qint64)cdOff > zip.size() || (qint64)cdSize > zip.size() - (qint64)cdOff)
                return false;
            // 解析中央目录
            int pos = (int)cdOff;
            const int cdEnd = pos + (int)cdSize;
            while (pos + 46 <= cdEnd) {
                const uchar *c = (const uchar *)zip.constData() + pos;
                if (c[0] != 0x50 || c[1] != 0x4b || c[2] != 0x01 || c[3] != 0x02) break;
                quint32 method, lho, compSize, uncompSize, nameLen, extraLen, commentLen;
                memcpy(&method, c + 10, 2);
                memcpy(&compSize, c + 20, 4);
                memcpy(&uncompSize, c + 24, 4);
                memcpy(&nameLen, c + 28, 2);
                memcpy(&extraLen, c + 30, 2);
                memcpy(&commentLen, c + 32, 2);
                memcpy(&lho, c + 42, 4);
                const QByteArray name = QByteArray((const char *)c + 46, (int)nameLen);
                pos += 46 + (int)nameLen + (int)extraLen + (int)commentLen;
                if (name.isEmpty() || name.endsWith('/')) continue;   // 目录项跳过
                if (method != 0) continue;                            // 非 store 跳过
                // 本地头在 lho：30 字节头 + 名字 + 数据 —— qint64 比较防 32 位溢出绕过
                const qint64 need = (qint64)lho + 30 + (qint64)nameLen + (qint64)compSize;
                if (need > zip.size()) continue;
                const uchar *lh = (const uchar *)zip.constData() + lho;
                const char *data = zip.constData() + lho + 30 + (int)nameLen;
                // zip slip：路径必须留在 outDir 内，越界（.. / 绝对路径）整条丢弃
                const QString out = pathInside(QString::fromUtf8(name));
                if (out.isEmpty()) {
                    logf("[viewer] 拒绝解压越界路径：%.200s", QString::fromUtf8(name).toUtf8().constData());
                    continue;
                }
                QDir().mkpath(QFileInfo(out).absolutePath());
                QFile wf(out);
                if (wf.open(QIODevice::WriteOnly)) wf.write(data, (qint64)compSize);
                Q_UNUSED(uncompSize)
            }
            return true;
        }
    }
    return false;
}
