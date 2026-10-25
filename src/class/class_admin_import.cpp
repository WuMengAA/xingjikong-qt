// 班级课表导入 / 学生批量导入 / 本地文件读取（2026-10-25 · 从 class_admin.cpp 拆出）
//
// 拆件原因：class_admin.cpp 已 617 行（门禁红线 600，实现文件）。这里整块是
// 「**导入**」这条支路：ClassIsland 档案、学生 CSV、读本地文件 —— 与建班/成员/发布
// 那些逐条动作没有共享状态，拆出来两边都变短，也不会再往 600 上叠。
//
// 纪律（与 class_admin.cpp 同口径）：失败一律 opFinished/reauthRequired，绝不静默；
// 云端给的 groups（档案含多班）必须转给 UI 让老师选，服务端不猜、客户端也不猜。
#include "class_admin.h"
#include "../viewerbackend.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

/* ── ClassIsland 档案导入 + 学生批量导入（2026-10-25）────────────────────
 * 为什么另开 postJson：这两个操作的**成功回执要读响应字段**（课表的群/天数/节数、
 * 学生的 新增/更新/跳过），而 post() 只发一句文案。失败路径保持同一套口径
 * （403 重认证门 → 云端人话 → errorString 兜底），不另造第二套错误语义。
 * 群里带 groups 的失败（档案含多班课表）也要转出去：UI 得能接着弹选择器。
 */
void ClassAdmin::postJson(const QString &action, const QJsonObject &body,
                          std::function<void(const QJsonObject &)> done)
{
    if (!m_backend) return;
    const QString base = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        emit opFinished(action, false, QStringLiteral("云端地址未配置，无法执行班级操作"));
        return;
    }
    QNetworkRequest req(QUrl(base + QStringLiteral("/api/class/") + action));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());

    auto *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, action, done] {
        r->deleteLater();
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (code == 200 && o.value(QStringLiteral("ok")).toBool()) {
            done(o);
            return;
        }
        QString msg = o.value(QStringLiteral("error")).toString();
        const QString sem = o.value(QStringLiteral("code")).toString();
        if (code == 403 && sem == QLatin1String("REAUTH_REQUIRED")) {
            if (msg.isEmpty()) msg = QStringLiteral("长时间未验证，操作被拦截，请重新登录一次");
            emit reauthRequired(msg);
            emit opFinished(action, false, msg);
            return;
        }
        // 档案含多个课表群：把群清单转出去，UI 直接弹选择器（不让老师重选文件）
        const QJsonArray gs = o.value(QStringLiteral("groups")).toArray();
        if (!gs.isEmpty()) {
            QVariantList out;
            out.reserve(gs.size());
            for (const QJsonValue &g : gs) {
                const QJsonObject go = g.toObject();
                QVariantMap m;
                m.insert(QStringLiteral("id"), go.value(QStringLiteral("id")).toString());
                m.insert(QStringLiteral("name"), go.value(QStringLiteral("name")).toString());
                m.insert(QStringLiteral("days"), go.value(QStringLiteral("days")).toInt());
                out.append(m);
            }
            emit timetableGroupsList(out);
        }
        if (msg.isEmpty()) {
            msg = r->error() == QNetworkReply::NoError
                ? QStringLiteral("HTTP %1").arg(code)
                : r->errorString();
        }
        emit opFinished(action, false, msg);
    });
}

void ClassAdmin::timetableGroups(const QString &profileJson)
{
    QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        emit opFinished(QStringLiteral("timetable-groups"), false,
                        QStringLiteral("选的不是 ClassIsland 档案（JSON 解析失败）"));
        return;
    }
    QJsonObject b;
    b.insert(QStringLiteral("profile"), doc.object());
    postJson(QStringLiteral("timetable-groups"), b, [this](const QJsonObject &o) {
        const QJsonArray gs = o.value(QStringLiteral("groups")).toArray();
        QVariantList out;
        out.reserve(gs.size());
        for (const QJsonValue &g : gs) {
            const QJsonObject go = g.toObject();
            QVariantMap m;
            m.insert(QStringLiteral("id"), go.value(QStringLiteral("id")).toString());
            m.insert(QStringLiteral("name"), go.value(QStringLiteral("name")).toString());
            m.insert(QStringLiteral("days"), go.value(QStringLiteral("days")).toInt());
            out.append(m);
        }
        emit timetableGroupsList(out);
    });
}

void ClassAdmin::importTimetable(const QString &code, const QString &profileJson,
                                 const QString &group)
{
    QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        emit opFinished(QStringLiteral("timetable-import"), false,
                        QStringLiteral("选的不是 ClassIsland 档案（JSON 解析失败）"));
        return;
    }
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("profile"), doc.object());
    if (!group.trimmed().isEmpty()) b.insert(QStringLiteral("group"), group.trimmed());
    postJson(QStringLiteral("timetable-import"), b, [this](const QJsonObject &o) {
        const QString gname = o.value(QStringLiteral("group")).toObject()
                               .value(QStringLiteral("name")).toString();
        emit opFinished(QStringLiteral("timetable-import"), true,
                        QStringLiteral("已导入课表：%1（%2 天 / %3 节）")
                            .arg(gname)
                            .arg(o.value(QStringLiteral("days")).toInt())
                            .arg(o.value(QStringLiteral("lessons")).toInt()));
    });
}

void ClassAdmin::importStudents(const QString &code, const QVariantList &rows)
{
    QJsonArray arr;
    for (const QVariant &v : rows) arr.append(QJsonObject::fromVariantMap(v.toMap()));
    if (arr.isEmpty()) {
        emit opFinished(QStringLiteral("students-import"), false,
                        QStringLiteral("CSV 里没有可导入的行"));
        return;
    }
    QJsonObject b;
    b.insert(QStringLiteral("classCode"), code);
    b.insert(QStringLiteral("rows"), arr);
    postJson(QStringLiteral("students-import"), b, [this](const QJsonObject &o) {
        emit opFinished(QStringLiteral("students-import"), true,
                        QStringLiteral("导入完成：新增 %1 / 更新 %2 / 跳过 %3")
                            .arg(o.value(QStringLiteral("added")).toInt())
                            .arg(o.value(QStringLiteral("updated")).toInt())
                            .arg(o.value(QStringLiteral("skipped")).toInt()));
    });
}

QString ClassAdmin::readTextFile(const QString &path, int maxBytes)
{
    m_fileError.clear();
    const QString p = path.trimmed();
    if (p.isEmpty()) {
        m_fileError = QStringLiteral("没有选择文件");
        return QString();
    }
    const QFileInfo fi(p);
    if (!fi.exists() || !fi.isFile()) {
        m_fileError = QStringLiteral("文件不存在：%1").arg(p);
        return QString();
    }
    if (maxBytes > 0 && fi.size() > maxBytes) {
        m_fileError = QStringLiteral("文件过大（%1 KB，上限 %2 KB）")
                          .arg(fi.size() / 1024).arg(maxBytes / 1024);
        return QString();
    }
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) {
        m_fileError = QStringLiteral("打不开文件（权限不足？）");
        return QString();
    }
    QByteArray raw = f.readAll();
    f.close();
    // UTF-8 BOM：ClassIsland 档案/Excel 导出的 CSV 常带，不剥掉 JSON.parse 会直接失败
    if (raw.startsWith("\xEF\xBB\xBF")) raw.remove(0, 3);
    return QString::fromUtf8(raw);
}

