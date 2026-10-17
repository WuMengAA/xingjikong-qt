#include "island_bridge.h"
#include "island.h"
#include "whale_audio.h"

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
    // 2026-10-17（#330 形态多样化）：QML 侧新增两档外框。
    // ⚠️ 字符串必须与 QML 的 `_formKind` 判定逐字一致（IslandRoot 里按小写比）。
    case IslandOverlay::Form::Banner:     return QStringLiteral("banner");
    case IslandOverlay::Form::Mini:       return QStringLiteral("mini");
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

// ── 历史通知（2026-10-11 · 持久化；2026-10-17 记录与逻辑升级）────────
// 从 IslandOverlay 的 m_history 取，桥不缓存。
//
// 2026-10-17：加了分类/关键词两个筛子。**筛在排序之前**，且 history() 与
// showHistoryItem() 都走这一个函数 —— 这是关键：上一轮（10-13）已经踩过
// 「管理端置顶后 m_history 下标 ≠ 显示序」，如果这次再在 history() 里单独过滤、
// 而 showHistoryItem 用未过滤的 order，就会变成"点第 3 条打开第 5 条"。
QVector<int> IslandBridge::historyOrder() const
{
    QVector<int> order;
    if (!m_host) return order;
    const int n = m_host->m_history.size();
    order.reserve(n);

    // 「全部」/空 = 不限分类；关键词为空 = 不搜
    const QString cat = (m_histCategory == QStringLiteral("全部")) ? QString() : m_histCategory;
    const QString kw  = m_histKeyword.trimmed();

    auto pass = [&](const IslandOverlay::HistoryItem &h) {
        if (!cat.isEmpty() && h.category != cat) return false;
        if (!kw.isEmpty()) {
            // 标题 + 描述 都搜（老师通常记得一句内容而不是标题）
            if (!h.title.contains(kw, Qt::CaseInsensitive)
                && !h.desc.contains(kw, Qt::CaseInsensitive)) return false;
        }
        return true;
    };

    for (int i = 0; i < n; ++i)
        if (m_host->m_history.at(i).fromAdmin && pass(m_host->m_history.at(i)))
            order.append(i);   // 第一批：管理端下发
    for (int i = 0; i < n; ++i)
        if (!m_host->m_history.at(i).fromAdmin && pass(m_host->m_history.at(i)))
            order.append(i);   // 第二批：本机事件
    return order;
}

QVariantList IslandBridge::history() const
{
    QVariantList out;
    if (!m_host) return out;
    const QVector<int> order = historyOrder();
    for (int idx : order) {
        const auto &h = m_host->m_history.at(idx);
        QVariantMap m;
        m.insert(QStringLiteral("id"), h.id);
        m.insert(QStringLiteral("title"), h.title);
        m.insert(QStringLiteral("desc"), h.desc);
        m.insert(QStringLiteral("icon"), h.icon);
        m.insert(QStringLiteral("ts"), h.ts);
        m.insert(QStringLiteral("timeStr"), h.timeStr);
        m.insert(QStringLiteral("dateGroup"), h.dateGroup);   // 2026-10-17：ListView section 用
        m.insert(QStringLiteral("category"), h.category);     // 指令/告警/消息/系统
        m.insert(QStringLiteral("level"), h.level);           // info/success/warning/error
        m.insert(QStringLiteral("read"), h.read);             // 未读点
        m.insert(QStringLiteral("fromAdmin"), h.fromAdmin);   // QML 可据此加"管理端"标记
        out.append(m);
    }
    return out;
}

int IslandBridge::historyUnreadCount() const
{
    return m_host ? m_host->historyUnreadCount() : 0;
}

void IslandBridge::setHistoryCategoryFilter(const QString &c)
{
    if (m_histCategory == c) return;
    m_histCategory = c;
    emit changed();
}

void IslandBridge::setHistoryKeyword(const QString &k)
{
    if (m_histKeyword == k) return;
    m_histKeyword = k;
    emit changed();
}

// ── 文件传输进度（2026-10-11 · #198 接收进度条）──
bool   IslandBridge::fileTransferActive() const { return m_host ? m_host->m_ftActive : false; }
QString IslandBridge::fileTransferName() const  { return m_host ? m_host->m_ftName : QString(); }
qint64  IslandBridge::fileTransferBytes() const { return m_host ? m_host->m_ftBytes : 0; }
qint64  IslandBridge::fileTransferTotal() const { return m_host ? m_host->m_ftTotal : 0; }

// ── §6.3 语音频谱（2026-10-13）──
QVariantList IslandBridge::spectrumBands() const
{
    QVariantList out;
    if (!m_host) return out;
    out.reserve(m_host->m_spectrumBands.size());
    for (float b : m_host->m_spectrumBands) out.append(double(b));
    return out;
}
bool IslandBridge::spectrumActive() const { return m_host ? m_host->m_spectrumOn : false; }

// ── P3 播放时段管理：岛底「正在播放」状态（2026-10-14）──
bool    IslandBridge::mediaActive() const    { return m_host ? m_host->m_mediaOn : false; }
QString IslandBridge::mediaTitle() const     { return m_host ? m_host->m_mediaTitle : QString(); }
qint64  IslandBridge::mediaElapsedMs() const { return m_host ? m_host->m_mediaElapsedMs : 0; }
qint64  IslandBridge::mediaTotalMs() const   { return m_host ? m_host->m_mediaTotalMs : 0; }
QString IslandBridge::mediaNextTitle() const { return m_host ? m_host->m_mediaNextTitle : QString(); }
QString IslandBridge::mediaNextCover() const { return m_host ? m_host->m_mediaNextCover : QString(); }
bool    IslandBridge::mediaPresenting() const { return m_host ? m_host->m_mediaPresenting : false; }
// 组件7：播放四态相位（走公开 getter，不直读成员 —— 相位由 IslandOverlay 单点维护）
QString IslandBridge::mediaPhase() const { return m_host ? m_host->mediaPhase() : QString(); }

// ── 组件5 祝福弹幕（2026-10-15）────────────────────────────────
int IslandBridge::blessingCooldownSec() const
{
    return m_host ? m_host->blessingCooldownSec() : 0;
}

int IslandBridge::sendBlessing(const QString &text)
{
    if (!m_host) return 3;   // 没宿主 = 内容无效（桥不该独立于岛存在，防御性返回）
    const int code = m_host->sendBlessing(text);
    // 结果码 + 冷却倒计时都要让 QML 立刻看到（成功 → 钮禁用+倒计时起跳；
    // 失败 → 失败态文案），所以走全量 sync() 而不是只发冷却那一路。
    sync();
    return code;
}

// ── 组件6 云端上报旁路（2026-10-15）──────────────────────────────
// 只转发到岛（岛经 CloudReporter 决定发不发）；没有 sync() —— 这两条
// 不改变任何 QML 可见状态，为高频旁路单独 NOTIFY 纯属浪费。
void IslandBridge::reportFeedback(const QString &kind, int value)
{
    if (m_host) m_host->reportFeedback(kind, value);
}

void IslandBridge::submitSongRequest(const QString &title, const QString &artist, const QString &source)
{
    if (m_host) m_host->submitSongRequest(title, artist, source);
}

void IslandBridge::syncSpectrum()
{
    // 只发频谱那一路：高频（每帧一次），不能拖累全量 changed（见头文件里的说明）
    emit spectrumChanged();
}

void IslandBridge::showHistoryItem(int index)
{
    if (!m_host) return;
    // ⚠️ index 是**显示序**（historyOrder）里的位置，**不是** m_history 的下标 ——
    //    两列表在"管理端置顶"后不再相同，直接拿 index 索引 m_history 会点开错的条。
    //    2026-10-17：加了分类/关键词过滤后，显示序还会被筛子缩短，所以必须走同一个
    //    historyOrder()（它已经内含过滤），这里一行都不用改 —— 这也是当初把
    //    排序收口成一个函数的好处。
    const QVector<int> order = historyOrder();
    if (index < 0 || index >= order.size()) return;
    const auto &h = m_host->m_history.at(order.at(index));
    // 打开即已读（设计稿 §5.4「跳转后自动标记已读」）
    if (!h.read) {
        m_host->markHistoryRead(h.id);
        emit changed();
    }
    // 把历史项恢复为岛的通知（纯展示，不自动关）；来源标记一并带回去（再归档时不丢标）
    m_host->showIsland(h.title, h.desc, h.icon, 0, IslandOverlay::Form::Capsule, h.fromAdmin);
}

void IslandBridge::showHistoryById(const QString &id)
{
    if (!m_host || id.isEmpty()) return;
    // 用 id 定位：不受过滤/重排影响（QML 列表滚动或筛选变化后 index 会漂）
    for (const auto &h : m_host->m_history) {
        if (h.id != id) continue;
        if (!h.read) {
            m_host->markHistoryRead(h.id);
            emit changed();
        }
        m_host->showIsland(h.title, h.desc, h.icon, 0, IslandOverlay::Form::Capsule, h.fromAdmin);
        return;
    }
    qWarning("[island] showHistoryById：找不到 id=%s（可能已被 7 天清理）", qPrintable(id));
}

void IslandBridge::markHistoryRead(const QString &id)
{
    if (!m_host) return;
    if (m_host->markHistoryRead(id)) emit changed();
}

void IslandBridge::markAllHistoryRead()
{
    if (!m_host) return;
    if (m_host->markAllHistoryRead() > 0) emit changed();
}

void IslandBridge::removeHistory(const QString &id)
{
    if (!m_host) return;
    if (m_host->removeHistory(id)) emit changed();
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
    // 鲸鱼被短按 → 播按压音（ya1.wav 小黄鸭按压）
    if (whaleAudioOn())
        WhaleAudio::instance()->play(WhaleAudio::Sound::PressYa, 0.9);
}

void IslandBridge::whaleLongPressed()
{
    // 鲸鱼被长按 → 播松开音（ya2.wav 小黄鸭松开）
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
