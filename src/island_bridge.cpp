#include "island_bridge.h"
#include "island.h"
#include "whale_audio.h"

#include <QDebug>
#include <QRegularExpression>
#include <QSettings>

IslandBridge::IslandBridge(IslandOverlay *host, QObject *parent)
    : QObject(parent), m_host(host)
{
    // ⚠️ 不在这里 sync()：构造时 QML 引擎还没起来，发信号没人听。
    //    IslandOverlay 建完桥会自己 sync() 一次。
}

// ── 读侧：全部从 host 现读，不缓存 ───────────────────────────────
// 缓存会造成"两个真源"（C++ 成员 + 桥字段），改一处忘一处就出鬼。
// 岛的状态字段就十来个，每次读的代价远低于维护两份状态的代价。

QString IslandBridge::title() const    { return m_host ? m_host->m_title : QString(); }
QString IslandBridge::desc() const     { return m_host ? m_host->m_desc : QString(); }
QString IslandBridge::icon() const     { return m_host ? m_host->m_icon : QString(); }

// ── desc 的 Markdown → HTML 预渲染（QML RichText 直接吃）─────────────
// 支持的子集（够通知用，不做全量 CommonMark）：
//   **x**      → <b>x</b>（加粗，可跨行）
//   # x        → 大一号标题
//   - x        → 列表项（缩进 + 圆点）
//   \n         → 换行（RichText 里 <br>）
// 任何未匹配的 markdown 都当纯文本；所有输入先 HTML 转义，杜绝注入。
QString IslandBridge::descHtml() const
{
    QString s = desc();
    if (s.isEmpty()) return s;

    // 1) 先转义，防止任意文本被当 HTML 解析
    QString out;
    out.reserve(s.size() + 32);
    for (const QChar c : s) {
        if      (c == QLatin1Char('&')) out += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<')) out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>')) out += QStringLiteral("&gt;");
        else if (c == QLatin1Char('"')) out += QStringLiteral("&quot;");
        else                             out += c;
    }

    // 2) 行内加粗（**...**），用 QString 正则，匹配到行尾也算（允许跨不换行）
    out.replace(QRegularExpression(QStringLiteral(R"(\*\*([^*]+)\*\*)")),
                QStringLiteral("<b>\\1</b>"));

    // 3) 逐行处理标题 / 列表 / 换行
    QString html;
    html.reserve(out.size() + 64);
    QStringList lines = out.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    bool inList = false;
    for (const QString &raw : lines) {
        QString line = raw;
        QString body;
        if (line.startsWith(QStringLiteral("# "))) {
            body = QStringLiteral("<span style='font-size:15pt;font-weight:600'>")
                 + line.mid(2) + QStringLiteral("</span>");
        } else if (line.startsWith(QStringLiteral("- "))) {
            if (!inList) { html += QStringLiteral("<ul style='margin:0;padding:0'>"); inList = true; }
            html += QStringLiteral("<li style='margin-left:14px'>") + line.mid(2) + QStringLiteral("</li>");
            continue;
        } else {
            body = line;
        }
        if (inList) { html += QStringLiteral("</ul>"); inList = false; }
        html += body + QStringLiteral("<br>");
    }
    if (inList) html += QStringLiteral("</ul>");
    // 去掉末尾多余 <br>
    if (html.endsWith(QStringLiteral("<br>"))) html.chop(4);
    return html;
}

QString IslandBridge::severity() const { return m_host ? m_host->m_severity : QString(); }
bool IslandBridge::light() const       { return m_host ? m_host->m_light : false; }
bool IslandBridge::idle() const        { return m_host ? m_host->m_idle : true; }
bool IslandBridge::expanded() const    { return m_host ? m_host->m_expanded : false; }
bool IslandBridge::interactive() const { return m_host ? m_host->m_interactive : false; }

bool IslandBridge::canOpen() const
{
    return m_host && !m_host->m_openPath.isEmpty();
}

QString IslandBridge::form() const
{
    if (!m_host) return QStringLiteral("capsule");
    switch (m_host->m_form) {
    case IslandOverlay::Form::Centered:   return QStringLiteral("centered");
    case IslandOverlay::Form::Fullscreen: return QStringLiteral("fullscreen");
    case IslandOverlay::Form::Capsule:
    default:                              return QStringLiteral("capsule");
    }
}

QVariantList IslandBridge::actions() const
{
    QVariantList out;
    if (!m_host) return out;
    for (int i = 0; i < m_host->m_actions.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("label"), m_host->m_actions.at(i));
        // 第一枚是主按钮（实心反白），与 paintBigForm() 里 `primary = (i==0)` 同口径
        m.insert(QStringLiteral("role"),
                 i == 0 ? QStringLiteral("primary") : QStringLiteral("secondary"));
        out.append(m);
    }
    return out;
}

QVariantList IslandBridge::states() const
{
    // 被控端当前是「一条内容」模型：showIsland 新内容顶掉旧内容，没有并行多状态。
    // 为了 QML 侧 StateList 能直接用，这里把单条内容包成一个元素的列表。
    //
    // ⚠️ 不在这里凭空造「N 项」：管理端那套聚合是 7 类状态源（指令/文件/告警…）同时在线，
    //    被控端没有那个数据源。硬编一个假多状态 = 界面在骗人。
    //    真要多状态，得先让 showIsland 支持并存（另一个立项），不是在这里补。
    QVariantList out;
    if (!m_host) return out;
    if (m_host->m_idle) return out;                       // 空闲态没有状态行
    if (m_host->m_title.isEmpty()) return out;            // 没内容就不给列表

    QVariantMap m;
    // k：图标名当类别标识，QML 的 iconNameFor() 据此选图标
    m.insert(QStringLiteral("k"), m_host->m_icon);
    m.insert(QStringLiteral("t"), m_host->m_title);
    m.insert(QStringLiteral("d"), m_host->m_desc);
    m.insert(QStringLiteral("p"), -1);                    // -1 = 无进度条
    out.append(m);
    return out;
}

// ── 小鲸鱼（2026-10-11 · dsh 移植）──────────────────────────────
// "标题复读机"：通知上屏时鲸鱼气泡冒出 title 文字。
// 岛在空闲态（m_idle=true）或内容刚消失时不冒——只在有真实内容时冒。
// m_whaleBubble 从 m_host->m_title 取（与岛标题同源，不另存）。
QString IslandBridge::whaleBubble() const
{
    if (!m_host) return {};
    if (m_host->m_idle) return {};
    if (m_host->m_title.isEmpty()) return {};
    return m_host->m_title;
}

bool IslandBridge::whaleAudioOn() const
{
    // 2026-10-12 用户裁决「通知/状态/灵动岛音效**默认关闭**」：默认静音，避免
    // 教室日常被通知音打断。QSettings 持久化开关（settings/whaleAudioOn），
    // 界面（若有）改它即可；不改就一直是静音。
    return QSettings().value(QStringLiteral("settings/whaleAudioOn"), false).toBool();
}

// ── 历史通知（2026-10-11 · 持久化）──────────────────────────────
// 从 IslandOverlay 的 m_history 取，桥不缓存。
QVariantList IslandBridge::history() const
{
    QVariantList out;
    if (!m_host) return out;
    for (const auto &h : m_host->m_history) {
        QVariantMap m;
        m.insert(QStringLiteral("title"), h.title);
        m.insert(QStringLiteral("desc"), h.desc);
        m.insert(QStringLiteral("icon"), h.icon);
        m.insert(QStringLiteral("ts"), h.ts);
        m.insert(QStringLiteral("timeStr"), h.timeStr);
        out.append(m);
    }
    return out;
}

// ── 文件传输进度（2026-10-11 · #198 接收进度条）──
bool   IslandBridge::fileTransferActive() const { return m_host ? m_host->m_ftActive : false; }
QString IslandBridge::fileTransferName() const  { return m_host ? m_host->m_ftName : QString(); }
qint64  IslandBridge::fileTransferBytes() const { return m_host ? m_host->m_ftBytes : 0; }
qint64  IslandBridge::fileTransferTotal() const { return m_host ? m_host->m_ftTotal : 0; }

void IslandBridge::showHistoryItem(int index)
{
    if (!m_host) return;
    if (index < 0 || index >= m_host->m_history.size()) return;
    const auto &h = m_host->m_history.at(index);
    // 把历史项恢复为岛的通知（纯展示，不自动关）
    m_host->showIsland(h.title, h.desc, h.icon, 0, IslandOverlay::Form::Capsule);
}

void IslandBridge::clearHistory()
{
    if (!m_host) return;
    m_host->m_history.clear();
    m_host->saveHistory();
    sync();
}

void IslandBridge::whaleClicked()
{
    // 鲸鱼被短按 → 播按压音（Ya1.wav 小黄鸭按压）
    if (whaleAudioOn())
        WhaleAudio::instance()->play(WhaleAudio::Sound::PressYa, 0.9);
}

void IslandBridge::whaleLongPressed()
{
    // 鲸鱼被长按 → 播松开音（Ya2.wav 小黄鸭松开）
    if (whaleAudioOn())
        WhaleAudio::instance()->play(WhaleAudio::Sound::ReleaseYa, 1.0);
}

void IslandBridge::sync()
{
    emit changed();
}

// ── QML → C++ ────────────────────────────────────────────────────

void IslandBridge::fireAction(const QString &label)
{
    if (!m_host) return;
    // 与 mouseReleaseEvent() 里点中按钮那段**同一条路径**：
    // 取回调 → 清按钮 → 取消交互 → dismiss → 最后调回调。
    // ⚠️ 顺序不能反：先触发回调的话，回调里可能又 showIsland（比如"需要帮助"要弹出说明），
    //    那时 m_actions 还没清，新内容会带着旧按钮上屏。
    const QString reply = label.isEmpty() ? m_host->m_actions.value(0) : label;
    auto cb = m_host->m_onAction;
    m_host->m_actions.clear();
    m_host->m_onAction = nullptr;
    m_host->setInteractive(false);
    m_host->dismiss();
    sync();
    if (cb) cb(reply);
}

void IslandBridge::requestExpand(bool on)
{
    if (!m_host) return;
    m_host->setExpanded(on);
}
