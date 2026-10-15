// 星集控被控端 · 灵动岛 QML 桥（2026-10-11）
//
// 目的：把 IslandOverlay 的**状态**暴露给 QML，让岛的外观交给 qml/island/IslandRoot.qml 画。
//
// ── 边界（重要）────────────────────────────────────────────────
// · 本类**不是**公开 API：只在 island.h 里前置声明 + private 成员指针，
//   main.cpp 一行都不用改（它只认 IslandOverlay 的公开方法）。
// · 逻辑（几何/动画/定时器/鼠标/生命周期）**全部留在 IslandOverlay**，
//   本类只做「读成员 → Q_PROPERTY」和「QML 回调 → 调 IslandOverlay 方法」。
// · 这不是"把岛搬到 QML"：是把**绘制**搬过去。#935 行里那些注释写满踩坑记录的
//   状态机（m_closing 淡出守卫 / kDragSlop 方向锁 / explorer /select）一个字都不动。
//
// ── 数据流 ─────────────────────────────────────────────────────
//   C++ 改状态 → IslandOverlay 调 bridge->sync() → NOTIFY 全发 → QML 重绘
//   QML 点按钮 → bridge.fireAction(label) → IslandOverlay 的 m_onAction 回调
//   QML 手势   → bridge.requestExpand(on) → IslandOverlay::setExpanded(on)
//
// ⚠️ sync() 用「一次性全 NOTIFY」而不是逐字段判重：岛的状态字段少（~10 个），
//    而判重逻辑一旦漏一个就会出现"改了不刷新"这种最难查的 bug。全发最多多几次
//    QML 绑定求值，代价可控。

#ifndef STELARITH_ISLAND_BRIDGE_H
#define STELARITH_ISLAND_BRIDGE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class IslandOverlay;

class IslandBridge : public QObject
{
    Q_OBJECT

    // ── 内容 ────────────────────────────────────────────────
    Q_PROPERTY(QString title    READ title    NOTIFY changed)
    Q_PROPERTY(QString desc     READ desc     NOTIFY changed)
    // desc 的 Markdown 预渲染版（**粗** / # 标题 / - 列表 / \n 换行）。
    // QML 用 Text { textFormat: RichText } 直接吃，避免 QML 侧解析 markdown 脆弱。
    Q_PROPERTY(QString descHtml READ descHtml NOTIFY changed)
    Q_PROPERTY(QString icon     READ icon     NOTIFY changed)
    // ── 形态 ────────────────────────────────────────────────
    // "capsule" | "centered" | "fullscreen"（对齐 IslandOverlay::Form）
    Q_PROPERTY(QString form     READ form     NOTIFY changed)
    // "" | "urgent" | "inform" | "remind"（仅全屏生效，白名单与旧 NotifyWindow 同）
    Q_PROPERTY(QString severity READ severity NOTIFY changed)
    // ── 主题 / 状态 ─────────────────────────────────────────
    Q_PROPERTY(bool light       READ light       NOTIFY changed)
    Q_PROPERTY(bool idle        READ idle        NOTIFY changed)
    Q_PROPERTY(bool expanded    READ expanded    NOTIFY changed)
    Q_PROPERTY(bool interactive READ interactive NOTIFY changed)
    // 非空 = 这条可点击打开所在文件夹（explorer /select）
    Q_PROPERTY(bool canOpen     READ canOpen     NOTIFY changed)
    // ── 交互 ────────────────────────────────────────────────
    // [{ label: "确认", role: "primary"|"secondary" }, ...]
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // 多状态聚合（清单 #2）。被控端当前是"一条内容"模型（showIsland 顶掉旧的），
    // 这个列表暂时由 IslandOverlay 组装成单元素；将来接任务中心时可直接扩多元素。
    // [{ k: "exam", t: "标题", d: "描述", p: -1 }, ...]
    Q_PROPERTY(QVariantList states  READ states  NOTIFY changed)

    // ── 小鲸鱼（2026-10-11 · dsh 移植）──────────────────────────
    // 通知来时，桥把 title 推给鲸鱼气泡（"标题复读机"）。
    // whaleBubble 非空 = 鲸鱼正在冒一句话。
    // whaleAudioEnabled = 音效开关（默认 true）。
    Q_PROPERTY(QString whaleBubble      READ whaleBubble      NOTIFY changed)
    Q_PROPERTY(bool     whaleAudioOn    READ whaleAudioOn     NOTIFY changed)

    // ── 历史通知（2026-10-11 · 持久化）────────────────────────────
    // 通知消失（dismiss/enterIdle/showIsland 顶掉旧通知）时自动归档到历史。
    // 历史列表由 IslandOverlay 维护（QVector + JSON 落盘）。
    // QML 侧展开态显示历史面板：点项恢复详情，可清空。
    // 每项格式：{title, desc, icon, ts, timeStr}
    Q_PROPERTY(QVariantList history  READ history  NOTIFY changed)

    // ── 文件传输进度（2026-10-11 · #198 接收进度条）──
    Q_PROPERTY(bool    fileTransferActive READ fileTransferActive NOTIFY changed)
    Q_PROPERTY(QString fileTransferName   READ fileTransferName   NOTIFY changed)
    Q_PROPERTY(qint64  fileTransferBytes  READ fileTransferBytes  NOTIFY changed)
    Q_PROPERTY(qint64  fileTransferTotal  READ fileTransferTotal  NOTIFY changed)

    // ── §6.3 语音频谱（2026-10-13 · 修「频谱显示到窗口去了」）──────────
    // 对讲中岛在**展开态**画 16 条频谱（spectrumBands 是 16 个 0..1）。
    // ⚠️ 单独一个 NOTIFY（spectrumChanged），**不挂全量 changed**：频谱每来一帧就变
    //    （~50Hz），跟着 changed 走会让 QML 所有绑定（含 history/states）每秒重算几十次。
    Q_PROPERTY(QVariantList spectrumBands  READ spectrumBands  NOTIFY spectrumChanged)
    Q_PROPERTY(bool         spectrumActive READ spectrumActive NOTIFY spectrumChanged)

    // ── P3 播放时段管理：岛底「正在播放」状态（2026-10-14）────────────
    // 展开/收起态底栏显示：当前曲目 + 已播/总时长 + 下一首封面小演示。
    // 更新频率低（~1Hz 计时器推进 elapsed），跟全量 changed() 走即可，不必单独信号。
    // ⚠️ qint64 走 C++ 属性、QML 用 real 承接（同 fileTransferBytes 口径，Qt 6.12 严格）。
    Q_PROPERTY(bool    mediaActive    READ mediaActive    NOTIFY changed)
    Q_PROPERTY(QString mediaTitle     READ mediaTitle     NOTIFY changed)
    Q_PROPERTY(qint64  mediaElapsedMs READ mediaElapsedMs NOTIFY changed)
    Q_PROPERTY(qint64  mediaTotalMs   READ mediaTotalMs   NOTIFY changed)
    Q_PROPERTY(QString mediaNextTitle READ mediaNextTitle NOTIFY changed)
    Q_PROPERTY(QString mediaNextCover READ mediaNextCover NOTIFY changed)
    // 2026-10-15 组件1（点歌互动）：媒体内容正占据岛面（收起态播放条/展开态互动区）。
    // 与 mediaActive 的分工：mediaActive=ffplay 在播（媒体行显隐，P3 语义）；
    // mediaPresenting=岛面当前画的是媒体。通知顶掉媒体时 presenting 仍为 true
    // （媒体还在播），QML 用 title===mediaTitle 判"岛面是媒体"。
    Q_PROPERTY(bool mediaPresenting READ mediaPresenting NOTIFY changed)
    // ── 组件7 播放四态（2026-10-15）──────────────────────────────
    // ""|starting|playing|ending|ended|blocked（相位语义与判定见 island.h）。
    // 相位由 C++ 单点判定（时长/剩余都只在 C++ 算），QML 只按串切文案/配色 ——
    // 避免"两端各算一遍阈值"这种必然漂移。
    Q_PROPERTY(QString mediaPhase READ mediaPhase NOTIFY changed)

    // ── 组件5 祝福弹幕（2026-10-15）──────────────────────────────
    // 岛内输入行投稿 → DanmakuWidget 全屏浮层（学生不可信源：30 字/100px/s/
    // 敏感词过滤/30s 冷却，规则在 DanmakuWidget，云端端点留 TODO 组件6）。
    // blessingCooldownSec：冷却剩余秒数（0=可发）。更新频率 ~1Hz 且只持续 30s，
    // 骑全量 changed()（与 mediaElapsedMs 同口径，不单独开信号）。
    Q_PROPERTY(int blessingCooldownSec READ blessingCooldownSec NOTIFY changed)

public:
    explicit IslandBridge(IslandOverlay *host, QObject *parent = nullptr);

    QString title() const;
    QString desc() const;
    QString descHtml() const;
    QString icon() const;
    QString form() const;
    QString severity() const;
    bool light() const;
    bool idle() const;
    bool expanded() const;
    bool interactive() const;
    bool canOpen() const;
    QVariantList actions() const;
    QVariantList states() const;

    // 小鲸鱼
    QString whaleBubble() const;
    bool whaleAudioOn() const;
    // QML → C++：鲸鱼被点击（rua），播按压音效
    Q_INVOKABLE void whaleClicked();
    // QML → C++：鲸鱼被长按（0.8s+），播松开音效
    Q_INVOKABLE void whaleLongPressed();

    // 历史通知
    QVariantList history() const;
    // 文件传输进度
    bool   fileTransferActive() const;
    QString fileTransferName() const;
    qint64  fileTransferBytes() const;
    qint64  fileTransferTotal() const;

    // §6.3 语音频谱
    QVariantList spectrumBands() const;
    bool spectrumActive() const;

    // P3 播放时段管理：岛底「正在播放」状态
    bool    mediaActive() const;
    QString mediaTitle() const;
    qint64  mediaElapsedMs() const;
    qint64  mediaTotalMs() const;
    QString mediaNextTitle() const;
    QString mediaNextCover() const;
    // 2026-10-15 组件1：媒体占据岛面（见上面 Q_PROPERTY 注释）
    bool mediaPresenting() const;
    // 组件7：播放四态相位（见上面 Q_PROPERTY 注释）
    QString mediaPhase() const;
    // 频谱状态变了就调这个：**只发频谱那一路**（高频，别用全量 sync()）。
    void syncSpectrum();
    // QML → C++：点历史项，恢复该通知到展开态
    Q_INVOKABLE void showHistoryItem(int index);
    // QML → C++：清空全部历史
    Q_INVOKABLE void clearHistory();

    // ── 组件5 祝福弹幕（2026-10-15）──────────────────────────────
    int blessingCooldownSec() const;
    // QML → C++：岛内输入行「投稿」。返回 DanmakuWidget::BlessingResult
    // （0=已入队 1=敏感词 2=冷却中 3=内容无效），QML 按码落成功/失败态。
    Q_INVOKABLE int sendBlessing(const QString &text);

    // ── 组件6 云端上报旁路（2026-10-15）──────────────────────────
    // QML → C++ → 岛 → CloudReporter（main.cpp 注入的 g_ws 发送器）。
    // 两条都是"点火后不管"：上报成败不影响本地行为（端侧先行），QML 无需回执。
    // 「好听」单击：reportFeedback("like", 1)；点歌提交：submitSongRequest(标题, 歌手, 来源)。
    Q_INVOKABLE void reportFeedback(const QString &kind, int value = 1);
    Q_INVOKABLE void submitSongRequest(const QString &title, const QString &artist = QString(),
                                       const QString &source = QStringLiteral("manual"));

    // 状态变了就调这个：一把全 NOTIFY。IslandOverlay 在每个改状态的点收口调用。
    void sync();

    // QML → C++：点了某枚动作按钮（文案）。
    // ⚠️ 复用 IslandOverlay 的既有语义：回调一次 → 清按钮 → 取消交互 → dismiss。
    //    不能只在 QML 里改个属性了事，否则 m_actions 还留在 C++ 里，下次上屏会带出来。
    Q_INVOKABLE void fireAction(const QString &label);

    // QML → C++：手势请求展开/收起。
    Q_INVOKABLE void requestExpand(bool on);

private:
    // 2026-10-13（用户「历史通知第一位优先显示管理端通知」）：历史**显示序** ——
    // 管理端在前、本地事件在后，组内保持原有先后（**稳定**，不重排时间序）。
    // history() 与 showHistoryItem() 必须共用它，否则「看到的第 N 条」≠「点开的第 N 条」。
    QVector<int> historyOrder() const;

    IslandOverlay *m_host = nullptr;   // 不持有（IslandOverlay 是单例，活得比本对象久）

signals:
    void changed();
    // §6.3 语音频谱专用通道（高频，见上面 Q_PROPERTY 注释）。
    void spectrumChanged();
};

#endif // STELARITH_ISLAND_BRIDGE_H
