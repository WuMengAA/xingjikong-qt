// 星集控管理端 · 互动审批桥（2026-10-15 · 灵动岛扩展组件6 配套）
//
// 两个职责：
//  ① **审批面板的数据面** —— 从云端 HTTP `/api/interact/*` 拉待审队列、提交审批动作。
//     端点与鉴权见 cloud-ws/src/index.js（Bearer 令牌，与 /api/devices 同一条路）。
//  ② **共享灵动岛的桥面** —— 用户点单「管理端审批面板里的灵动岛用被控端一模一样的」：
//     岛的真源在根仓库 `Stelarith-shared-qml/island/IslandRoot.qml`，两端共用一份；
//     岛读的是**全局上下文属性 `bridge`** ⇒ 管理端也必须挂一个 `bridge`。
//     本类就是那个对象（main.cpp 里 setContextProperty("bridge", ...)）。
//
// ── 边界（重要）─────────────────────────────────────────────────
// · 管理端的岛**只做预览**：把面板选中的待审条目渲染成"学生会看到的样子"。
//   所以岛读的那些交互方法（fireAction / requestExpand / showHistoryItem…）
//   在这里是最小实现/空实现 —— 它们不会在预览场景被点到。
// · ⚠️ 属性名与语义**必须与被控端 IslandBridge 对齐**：两侧读同一份 QML，
//   少一个属性岛不会崩，只会静默显示空值 —— 这是最难查的一类"看起来正常"。
//   （被控端清单可用 `grep -o 'bridgeRef\.[A-Za-z_]*' IslandRoot.qml | sort -u` 复核。）

#ifndef STELARITH_INTERACT_BRIDGE_H
#define STELARITH_INTERACT_BRIDGE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class ViewerBackend;

class InteractBridge : public QObject
{
    Q_OBJECT

    // ── 岛桥面：内容（对齐被控端 IslandBridge）──────────────────
    Q_PROPERTY(QString title    READ title    NOTIFY changed)
    Q_PROPERTY(QString desc     READ desc     NOTIFY changed)
    Q_PROPERTY(QString descHtml READ descHtml NOTIFY changed)
    Q_PROPERTY(QString icon     READ icon     NOTIFY changed)
    Q_PROPERTY(QString severity READ severity NOTIFY changed)
    // ── 岛桥面：主题 / 状态 ────────────────────────────────────
    Q_PROPERTY(bool light       READ light       WRITE setLight    NOTIFY changed)
    Q_PROPERTY(bool idle        READ idle        NOTIFY changed)
    Q_PROPERTY(bool expanded    READ expanded    WRITE setExpanded NOTIFY changed)
    Q_PROPERTY(bool interactive READ interactive NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actions  NOTIFY changed)
    Q_PROPERTY(QVariantList states  READ states   NOTIFY changed)
    Q_PROPERTY(QString whaleBubble  READ whaleBubble  NOTIFY changed)
    Q_PROPERTY(bool    whaleAudioOn READ whaleAudioOn NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history  NOTIFY changed)
    // ── 岛桥面：文件传输 / 频谱（管理端不产生，恒空）─────────────
    Q_PROPERTY(bool   fileTransferActive READ fileTransferActive NOTIFY changed)
    Q_PROPERTY(qint64 fileTransferBytes  READ fileTransferBytes  NOTIFY changed)
    Q_PROPERTY(qint64 fileTransferTotal  READ fileTransferTotal  NOTIFY changed)
    Q_PROPERTY(QVariantList spectrumBands  READ spectrumBands  NOTIFY changed)
    Q_PROPERTY(bool         spectrumActive READ spectrumActive NOTIFY changed)
    // ── 岛桥面：播放（组件7 四态也走这条）─────────────────────
    Q_PROPERTY(bool    mediaActive    READ mediaActive    NOTIFY changed)
    Q_PROPERTY(QString mediaTitle     READ mediaTitle     NOTIFY changed)
    Q_PROPERTY(qint64  mediaElapsedMs READ mediaElapsedMs NOTIFY changed)
    Q_PROPERTY(qint64  mediaTotalMs   READ mediaTotalMs   NOTIFY changed)
    Q_PROPERTY(QString mediaNextTitle READ mediaNextTitle NOTIFY changed)
    Q_PROPERTY(QString mediaNextCover READ mediaNextCover NOTIFY changed)
    Q_PROPERTY(bool    mediaPresenting READ mediaPresenting NOTIFY changed)
    Q_PROPERTY(QString mediaPhase      READ mediaPhase      NOTIFY changed)
    // ── 岛桥面：祝福弹幕冷却（管理端恒 0 = 可发）────────────────
    Q_PROPERTY(int blessingCooldownSec READ blessingCooldownSec NOTIFY changed)

    // ── 审批面板数据面 ────────────────────────────────────────
    // state: "idle" 未拉 / "loading" 拉取中 / "ok" 成功 / "error" 失败
    Q_PROPERTY(QString state READ state NOTIFY dataChanged)
    Q_PROPERTY(QString error READ error NOTIFY dataChanged)
    Q_PROPERTY(QVariantList songRequests    READ songRequests    NOTIFY dataChanged)
    Q_PROPERTY(QVariantList blessings       READ blessings       NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap  feedbackSummary READ feedbackSummary NOTIFY dataChanged)
    // "pending" / "approved" / "rejected" / ""（全部）—— 面板筛选用
    Q_PROPERTY(QString filterState READ filterState WRITE setFilterState NOTIFY dataChanged)

public:
    explicit InteractBridge(ViewerBackend *backend, QObject *parent = nullptr);

    // ── 岛桥面 getter ──────────────────────────────────────────
    QString title() const { return m_title; }
    QString desc() const { return m_desc; }
    QString descHtml() const { return m_desc; }
    QString icon() const { return m_icon; }
    QString severity() const { return QString(); }
    bool light() const { return m_light; }
    void setLight(bool v) { if (m_light == v) return; m_light = v; emit changed(); }
    // idle：无通知、无状态 ⇒ 岛彻底消失（对齐被控端 IslandBridge::idle 语义）。
    // ⚠️ 2026-10-16 主界面接入：原来只看 m_title，管理端多设备状态走 states
    //   ⇒ 必须一并判空，否则"有状态但没通知"时岛会误判为空而不显示。
    bool idle() const { return m_title.isEmpty() && m_states.isEmpty(); }
    bool expanded() const { return m_expanded; }
    void setExpanded(bool v) { if (m_expanded == v) return; m_expanded = v; emit changed(); }
    // 管理端主界面接入后岛是**可交互**的（点击展开/收起）；审批面板预览仍设 false。
    bool interactive() const { return m_interactive; }
    void setInteractive(bool v) { if (m_interactive == v) return; m_interactive = v; emit changed(); }
    QVariantList actions() const { return {}; }
    QVariantList states() const { return m_states; }
    QString whaleBubble() const { return QString(); }
    bool whaleAudioOn() const { return false; }
    QVariantList history() const { return {}; }
    bool fileTransferActive() const { return false; }
    qint64 fileTransferBytes() const { return 0; }
    qint64 fileTransferTotal() const { return 0; }
    QVariantList spectrumBands() const { return {}; }
    bool spectrumActive() const { return false; }
    bool mediaActive() const { return m_mediaActive; }
    QString mediaTitle() const { return m_mediaTitle; }
    qint64 mediaElapsedMs() const { return m_mediaElapsedMs; }
    qint64 mediaTotalMs() const { return m_mediaTotalMs; }
    QString mediaNextTitle() const { return m_mediaNextTitle; }
    QString mediaNextCover() const { return m_mediaNextCover; }
    bool mediaPresenting() const { return m_mediaPresenting; }
    QString mediaPhase() const { return m_mediaPhase; }
    int blessingCooldownSec() const { return 0; }

    // ── 岛桥面：QML → C++ ─────────────────────────────────────
    // 管理端主界面接入后，以下不再只是"预览空实现"：
    Q_INVOKABLE void fireAction(const QString &) {}
    // 管理端把主界面的多设备状态表喂进来（[{k,t,d,p}]，与被控端同结构）。
    Q_INVOKABLE void setIslandStates(const QVariantList &s) {
        if (m_states == s) return;
        m_states = s;
        emit changed();
    }
    // 瞬态提示（对齐被控端岛的统一通知入口）：title/desc/icon + 毫秒后自动收起。
    // 管理端 Main.qml 的 onIslandNote 走这里（原来调 DynamicIsland.showNote）。
    Q_INVOKABLE void showNote(const QString &title, const QString &desc,
                              const QString &icon, int ms);
    Q_INVOKABLE void requestExpand(bool on) { setExpanded(on); }
    Q_INVOKABLE void showHistoryItem(int) {}
    Q_INVOKABLE void clearHistory() {}
    Q_INVOKABLE void whaleClicked() {}
    Q_INVOKABLE void whaleLongPressed() {}
    Q_INVOKABLE int  sendBlessing(const QString &) { return 3; }   // 预览态不发
    Q_INVOKABLE void reportFeedback(const QString &, int = 1) {}
    Q_INVOKABLE void submitSongRequest(const QString &, const QString & = QString(),
                                       const QString & = QStringLiteral("manual")) {}

    // ── 审批数据面 ────────────────────────────────────────────
    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QVariantList songRequests() const { return m_songs; }
    QVariantList blessings() const { return m_blessings; }
    QVariantMap feedbackSummary() const { return m_feedback; }
    QString filterState() const { return m_filterState; }
    void setFilterState(const QString &s);

    // 拉三份数据（点歌待审 / 祝福待审 / 反馈汇总）。面板 open() 时调。
    Q_INVOKABLE void reload();
    // 审批动作：action = "approve" | "reject"。成功后就地改本地条目的 state
    // （不整表重拉：审批后面板停在原滚动位置，体验更稳）。
    Q_INVOKABLE void handleSong(int id, const QString &action);
    Q_INVOKABLE void handleBlessing(int id, const QString &action);

    // ── 岛预览 ────────────────────────────────────────────────
    // 把选中条目渲染成"学生在教室机上会看到的样子"。
    //   kind: "song"（点歌 → 岛播放条）/ "blessing"（祝福 → 岛通知态）/
    //         "feedback"（反馈 → 岛通知态）/ ""（清空回 idle）
    // title/sub 由 QML 直接给（面板已有这两段文字，不必再经 C++ 组装一遍）。
    Q_INVOKABLE void preview(const QString &kind, const QString &title,
                             const QString &sub = QString());
    Q_INVOKABLE void clearPreview();
    QString previewKind() const { return m_previewKind; }

    // 主题跟随管理端（QML 在主题变化时调）。预览岛的深浅色要和管理端一致。
    void setDark(bool dark);

signals:
    void changed();        // 岛桥面全量 NOTIFY（与被控端同范式）
    void dataChanged();    // 审批数据面

private:
    void finishReload();
    void postHandle(const QString &kind, int id, const QString &action);
    void applyHandleResult(const QString &kind, int id, const QString &action, bool ok,
                           const QString &msg);

    ViewerBackend *m_backend = nullptr;   // 不持有（main.cpp 的栈对象，活得比本类久）
    QNetworkAccessManager *m_nam = nullptr;

    // 岛桥面状态
    QString m_title, m_desc, m_icon;
    bool    m_light = false;
    bool    m_expanded = true;      // 预览默认展开（面板里就是要看展开态）
    QVariantList m_states;          // 管理端多设备状态（主界面喂入）
    bool    m_interactive = false;  // 主界面接入时置 true
    QTimer *m_noteTimer = nullptr;  // showNote 的自动收起
    bool    m_mediaActive = false;
    bool    m_mediaPresenting = false;
    QString m_mediaTitle;
    qint64  m_mediaElapsedMs = 0;
    qint64  m_mediaTotalMs   = 0;
    QString m_mediaNextTitle, m_mediaNextCover, m_mediaPhase;
    QString m_previewKind;

    // 审批数据面状态
    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_filterState = QStringLiteral("pending");
    QVariantList m_songs, m_blessings;
    QVariantMap  m_feedback;
    int m_pendingReplies = 0;
};

#endif // STELARITH_INTERACT_BRIDGE_H
