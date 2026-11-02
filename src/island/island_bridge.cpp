#include "island_bridge.h"
#include "island.h"
#include "../media/whale_audio.h"
// 2026-10-27（H 批）：岛的「笔」面直接调批注域（本机直调，不经云端）
#include "../annotate/annotate_domain.h"
// 2026-11-01：菜单栏动态课表读课表数据域（PanelDomain::barLessonText）
#include "../domain/panel_domain.h"
// 2026-10-27（C 批）：岛「设」面的方法实现挪到 island_bridge_settings.cpp（600 行红线拆分），
// 这里只留声明与批注动作；实现侧需要的 system_volume.h / core/AgentSettings.h 也在那边 include。

#include <algorithm>
#include <QDateTime>
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
    // ── T 档（2026-10-18）：从「单条内容」改为输出**任务表** ──
    // 原来这里把 m_title/m_desc 包成一个元素的列表（见下方 git 历史里的旧注释），
    //    因为当时岛确实是"新内容顶掉旧内容"的模型。
    // 现在 IslandOverlay 有了 m_tasks（通知/文件/媒体/对讲各自一个 tag 的任务），
    //    所以这里直接输出全表 —— 界面终于能同时看到"收文件中 + 播放中 + 新通知"。
    //
    // ⚠️ 保持与旧契约同形：每项仍是 {k,t,d,p}（QML 的 StateList 不用改就能吃），
    //    新增 id/tag/state/priority 等字段供后续内联动作与排序用（QML 不读也不受影响）。
    // ⚠️ 不凭空造项：表里有什么就给什么，没有就空列表（空闲态）。
    QVariantList out;
    if (!m_host) return out;

    const QVector<IslandOverlay::Task> &tasks = m_host->tasks();
    // 空闲态：任务表里若还有 sticky 的常驻项（如对讲中），仍应显示 —— 岛收回但状态还在。
    // 原来"idle ⇒ 直接返回空"会把常驻状态一并吞掉，这里改成按表内容决定。
    for (const auto &t : tasks) {
        // 已完成（state=done）的不进状态列表：任务表保留它是为了回填/审计，
        // 界面不该再显示"已结束的文件传输"。
        if (t.state == 2) continue;
        // ⚠️ 文件传输**故意排除**：展开态有专门的 ftRow 进度卡（IslandRoot.qml），
        //    带百分比 + 已收/总字节 +「接收中」标，比这里一行干巴巴的进度条信息更全。
        //    两边都渲染 ⇒ 同一件事显示两遍，且 ftH(46px) 与 StateList 行高被**重复计入**
        //    几何预算（IslandRoot 的 fxH 求和）⇒ 传输中岛凭空被撑高、布局错位。
        //    ⇒ 单一真源：ft 进度只由 ftRow 画，StateList 不重复。
        if (t.tag == QStringLiteral("file")) continue;
        QVariantMap m;
        // k：图标名当类别标识，QML 的 iconNameFor() 据此选图标
        m.insert(QStringLiteral("k"), t.icon);
        m.insert(QStringLiteral("t"), t.title);
        m.insert(QStringLiteral("d"), t.desc);
        m.insert(QStringLiteral("p"), t.progress);        // -1 = 无进度条；0..1 = 进度
        // ⚠️ 广播的进度要**现算**：云端 30s 才给一次 position，若照搬 t.progress，
        //    两次快照之间进度条是静止的（看着像卡住）。这里的 broadcastProgress()
        //    用「锚点 + 本地时钟」外推，QML 每次重绘拿到的是新值。
        if (t.tag == QStringLiteral("broadcast")) {
            const double bp = m_host->broadcastProgress();
            if (bp >= 0) m.insert(QStringLiteral("p"), bp);
        }
        // ── 新增字段（T 档）──
        m.insert(QStringLiteral("id"), t.id);
        m.insert(QStringLiteral("tag"), t.tag);
        m.insert(QStringLiteral("state"), t.state);       // 0=running 1=paused 3=failed
        m.insert(QStringLiteral("priority"), t.priority); // 后续排序/抢占用
        m.insert(QStringLiteral("sticky"), t.sticky);
        out.append(m);
    }
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

// 岛上**正在显示**的那条通知的时间（2026-10-17）。空串 = 没有通知 / 媒体等无时间内容。
// QML 直接绑 bridge.noticeTime，不再自己算 —— 两端口径统一在 C++。
QString IslandBridge::noticeTime() const
{
    if (!m_host) return QString();
    return IslandOverlay::noticeTimeOf(m_host->noticeTs());
}

// 2026-10-27（F）：渠道透传给 QML（"课程提醒"这类自动提示要在岛本体上标出来）。
QString IslandBridge::channel() const
{
    return m_host ? m_host->channel() : QString();
}

// 2026-11-01：QML 交互上报 → 重置展开态 5s 无操作计时。
void IslandBridge::notifyActivity()
{
    if (m_host) m_host->noteActivity();
}

// 2026-11-02：按下/松手上报 → 按住期间冻结所有自动收起。
// QML 侧用 pressed/released 报（mouse 与 touch 在 QML 里都表现为 pressed）。
void IslandBridge::notifyTouching(bool down)
{
    if (m_host) m_host->setTouching(down);
}

// 2026-10-31：班级显示名（菜单栏中间）。main.cpp 在已知班级名时经 setClassName 推入。
QString IslandBridge::className() const
{
    return m_host ? m_host->className() : QString();
}

// 2026-11-01：菜单栏动态课表（当前课/下一节摘要）。
QString IslandBridge::barLesson() const
{
    return PanelDomain::barLessonText();
}

// 2026-11-01：今天看板（今日课表全量）—— 复用 PanelDomain 的 timetable 组装（含 now/next 态）。
QVariantMap IslandBridge::todayDash() const
{
    return PanelDomain::build(QStringLiteral("timetable"), QJsonObject()).data.toVariantMap();
}

// 2026-10-27（G4/G5/G6）：面板面名与数据。
// QJsonObject → QVariantMap 走 Qt 自带的隐式转换；QML 侧拿到的是普通 JS 对象，
// 直接 `data.lessons[i].subject` 读，不需要 JSON.parse。
QString IslandBridge::panelFace() const
{
    return m_host ? m_host->panelFace() : QString();
}

QVariantMap IslandBridge::panelData() const
{
    return m_host ? m_host->panelData().toVariantMap() : QVariantMap();
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

// ── 灵动岛 6 形态（2026-10-27 收口蓝图）──
QString     IslandBridge::morphMode() const { return m_host ? m_host->morphMode() : QStringLiteral("none"); }
QVariantMap IslandBridge::morphData() const { return m_host ? m_host->morphData() : QVariantMap(); }

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
    // 每次广播前先让 C++ 单点裁决当前该显示哪个形态（大脑算，QML 只渲染）。
    if (m_host) m_host->recomputeMorph();
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

// ── 灵动岛 6 形态（2026-10-27 收口蓝图）──
void IslandBridge::morphDemo(const QString &mode)
{
    if (m_host) m_host->setMorphDemo(mode);
}

void IslandBridge::morphAction(const QString &cmd)
{
    if (m_host) m_host->morphAction(cmd);
}

/* ══════════════════════════════════════════════════════════════
 *  批注 / 白板（2026-10-27 · H 批 · 岛是被控端的第一入口）
 *  ── 为什么在这里而不是走云端指令 ──
 *  老师上课点岛上的「笔」必须立刻有反应；绕一趟云端（WS→云端→WS）最快也要几百毫秒，
 *  而且云端断了就用不了。所以岛上是**本机直调**，云端指令（annotate_*）留给管理端。
 *  两条路走的是同一个域，状态只有一份。
 * ══════════════════════════════════════════════════════════════ */

// ── 以下「设」面方法（systemVolume / setSystemVolume / notifyVolume / setNotifyVolume /
//    notifyBlacklist / settingsAction）实现见 island_bridge_settings.cpp ──
