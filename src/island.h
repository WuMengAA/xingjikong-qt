// 星集控被控端 · 灵动岛（2026-10-07，设计文档 3.8 被控端版）
//
// 需求（用户 2026-10-07）：被控端的活动（通知 / 语音 / 广播 / 考试 / 录像等）
// **全部经过灵动岛**——顶部居中胶囊，形状与大小、位置按状态转变（灵动），
// 带缓动动画；信息展示 + 交互穿透（点击可穿透到下层，不挡操作）；
// 深色 / 浅色两套配色。
//
// 本组件是 QWidget 实现（被控端 Qt Widgets 栈，不引 QML）：
//   · 顶部居中，随内容宽度自适应；高度 56px 胶囊 ↔ 168px 展开，QPropertyAnimation 缓动
//   · 深/浅色由系统（Qt::colorScheme）或调用方 setLightMode() 决定
//   · 默认交互穿透（WA_TransparentForMouseEvents）；需要点按（如确认/查看）时
//     调用方 setInteractive(true) 关闭穿透
//   · 自动收起：时长到后淡出；紧急/交互模式不自动关
//
// 与被删 NotifyWindow 的关系（2026-10-14 P1）：NotifyWindow 已删除，灵动岛是被控端
// **唯一展示入口**——全屏遮罩/居中弹窗/顶部胶囊/确认/紧急全部由岛覆盖（见 island.cpp）。
// 历史注释里"与旧 NotifyWindow 同思路/同白名单"指的就是这次被吸收的那套窗口做法。
//
// ── 2026-10-11 · 绘制层改 QML（用户裁决）──────────────────────────────
// 用户要求"被控端 QWidget 改 QML"。本次只把**绘制**搬走，**逻辑一行不动**：
//   · 视觉层 → qml/island/IslandRoot.qml（+ 7 个组件），由 QQuickWidget 承载
//   · 逻辑层（几何/动画/定时器/鼠标/生命周期/单例）→ 仍在本文件原样保留
//   · 桥：island_bridge.{h,cpp}（private 实现细节，不为它新增公开接口）
// ⚠️ 本文件的**公开接口一个字符都没改**（12 个方法 + 1 个信号），
//    所以 main.cpp 的 5 处调用点 + notifyFromParams 全都零改动。
//    这是本次改造最重要的风险控制点 —— 任何需要改 main.cpp 的方案都视为越界。

#ifndef STELARITH_ISLAND_H
#define STELARITH_ISLAND_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>
#include <QJsonObject>     // 2026-10-15 组件6：CloudReporter 的 payload 形状
#include <QPointer>
#include <QTimer>
#include <QPropertyAnimation>
#include <functional>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;      // 2026-10-11：resizeEvent(QResizeEvent*) 需要
class QQuickWidget;
class IslandBridge;
class DanmakuWidget;     // 2026-10-15 组件5：祝福弹幕全屏浮层（懒建，见 ensureDanmaku）

class IslandOverlay : public QWidget
{
    Q_OBJECT
public:
    // 单例：被控端全局唯一（同时间只显示一个灵动岛，新状态顶掉旧的）
    static IslandOverlay *instance();

    // Idle = 空闲时的矮胶囊（2026-10-07 用户要求"无任务、通知则收回顶部，显示半胶囊+横杠箭头"）。
    // 空闲态不隐藏、不消失：收成顶部一条常驻的半胶囊，中间一根横杠+箭头，
    // 点一下 / 往下一拉（触屏）就展开看最近那条活动。
    // 2026-10-13 用户「界面太小了放大到 640x320」：展开态高度 168 → 320。
    // （宽度在 QML implicitWidth：IslandRoot.qml expanded 400 → 640；宿主 QQuickWidget
    //   走 SizeRootObjectToView，宽度以 root implicit 为准，C++ 这里的注释同步说明。）
    enum Shape { Idle = 44, Collapsed = 56, Expanded = 320 };

    // ── 呈现形态（2026-10-09 · #98 ClassIsland 式）──────────────────────────
    // 用户原话："弹窗则居中，全屏则放大全屏，要有过渡。"
    //   Capsule    顶部居中胶囊（Idle/Collapsed/Expanded 三档都归它）—— 绝大多数瞬态提示
    //   Centered   屏幕正中的卡片（通知这类"要停下来看清"的内容）
    //   Fullscreen 铺满整屏（紧急广播/强制提示，压住整个桌面）
    // ⚠️ 与 Shape 正交：Shape 只描述**胶囊那三档高度**，Form 描述"这条提示摆在哪、多大"。
    //    把两者混成一个枚举会让"全屏"被迫带一个高度值，是错的。
    enum class Form { Capsule, Centered, Fullscreen };

    // 显示一条（icon 为 lucide 图标名；title 主文案，desc 次文案）
    // fromAdmin（2026-10-13）：这条是**管理端**下发的通知 —— 归档进历史时打标，
    //   历史列表把管理端通知排在最前（用户「历史通知第一位优先显示管理端通知」）。
    void showIsland(const QString &title, const QString &desc = QString(),
                    const QString &icon = QString(),
                    int durationMs = 5000, Form form = Form::Capsule,
                    bool fromAdmin = false);
    Form form() const { return m_form; }

    // ── 带交互按钮的通知（2026-10-09 · 把「确认」并入灵动岛本体）───────────────
    // actions 非空 ⇒ 本岛接管鼠标（不穿透）、**不自动收起**，等用户点中某一枚；
    // 点击后回调 onAction(该按钮文案)，随后收回空闲态。form 建议用 Centered/Fullscreen
    // （胶囊那档太矮放不下按钮，调用方需自行升到 Centered）。
    // durationMs<=0 表示"等点击、不超时"；不建议给确认类设超时（会吞掉学生的确认动作）。
    void showWithActions(const QString &title, const QString &desc, const QString &icon,
                         Form form, const QStringList &actions,
                         std::function<void(const QString &)> onAction,
                         int durationMs = 0, bool fromAdmin = false);
    // 仅全屏形态的配色档（remind/inform/urgent），空 = 默认黑底。与旧 NotifyWindow 同白名单。
    void setSeverity(const QString &severity);

    // 空闲态：没有任务/通知时收回顶部显示半胶囊（默认 true）。
    void setIdleEnabled(bool on);
    bool idleEnabled() const { return m_idleEnabled; }
    bool isIdle() const { return m_idle; }
    // 开机就让空闲胶囊常驻（不等第一条通知先出现过）
    void ensureIdle();

    // 切换展开（点击展开看详情）/ 收起
    void setExpanded(bool on);
    bool isExpanded() const { return m_expanded; }

    // 深/浅色：true=浅色（白底黑字），false=深色（黑底白字，默认）
    void setLightMode(bool light);
    bool lightMode() const { return m_light; }

    // 交互穿透：true=点击穿透到下层应用（纯展示，默认）；false=本岛接收鼠标（可点确认/查看）
    void setInteractive(bool on);
    bool interactive() const { return m_interactive; }

    // 点击后要打开的本地路径（收文件那类"可点开看清楚"的提示）。
    // 设了它，鼠标按下就直接打开所在文件夹并选中该文件，不再只是展开/收起。
    // 注意：只支持打开**文件夹并选中文件**（explorer /select），绝不 ShellExecute 打开
    // 文件本身 —— 被控端是学生机，远程下发的是 docx/pdf 也可能是别的东西，
    // 直接"打开"等于替老师/系统决定运行什么，风险不可控。
    void setOpenPath(const QString &path);
    void clearOpenPath();
    QString openPath() const { return m_openPath; }

    // 立即隐藏（不再等自动收起）。空闲态开启时改为"收回成半胶囊"而非彻底消失。
    void dismiss();

    // ── 内置模块的 QML 注入（2026-10-17 · VoiceHub 联动 #329）────────────
    // 把模块状态对象设到岛 QML **根对象属性**（IslandRoot 的 voicehubObj）。
    // 为什么走 rootObject()->setProperty 而不是 context property：模块在
    // main.cpp 里是岛构造**之后**才注册的，context property 后补不一定被既有
    // 绑定拾取；根对象属性是 QML 一等公民，setProperty 即时触发依赖它的绑定
    // 重算（IslandRoot 侧 `property var voicehubObj: null` + vhRef null 守卫，
    // 模块被停用时 QML 自动回到「无联动」渲染）。
    // rootObject 为 null（QML 没加载成功、手绘兜底中）时静默失败——被控端不能
    // 因为一个外观文件缺失就起不来（同 setSource 处的容错口径）。
    void setQmlObjectProperty(const QString &name, QObject *obj);

    // ── 文件传输进度（2026-10-11 · #198 接收进度条）──
    // file_push 接收期间把进度推到岛：name 文件名 / bytes 已收 / total 总字节（<=0 未知）。
    // 首次调用弹出常驻传输卡（不自动收起），之后只更新进度；
    // 收到 file_done 字节比对通过后由调用方 clearFileTransfer()（成功卡会顶掉它）。
    void setFileTransfer(const QString &name, qint64 bytes, qint64 total);
    void clearFileTransfer();

    // ── §6.3 语音频谱（2026-10-13）────────────────────────────────────
    // 喂一小段语音对讲 PCM → 岛在**展开态**画 16 位频谱（16 条随幅度跳动，静音归零）。
    // ⚠️ 2026-10-13 修 bug「被控端频谱显示到窗口去了」：原实现是 main.cpp 里
    //    `g_spectrum = new SpectrumWidget()`（**无 parent**）+ `show()` ⇒ 频谱在桌面
    //    (20,20) 弹出一个**独立顶层窗口**，挡在课件上、看着像野窗口。
    //    规格 §6.3 明写「语音对讲 → 被控端灵动岛**展开**；展开态显示 16 位频谱」
    //    ⇒ 现在频谱由岛自己画（QML），不再有任何独立窗口。
    //    feedSpectrum() 会顺带保证岛处于展开态（否则展开态那块频谱区看不到）。
    void feedSpectrum(const QByteArray &pcm, int sampleRate = 16000, int bits = 16);
    // 对讲结束（audio.stop）→ 频谱归零收起（并让岛回到收起/空闲）。
    void setSpectrumActive(bool on);
    bool spectrumActive() const { return m_spectrumOn; }

    // ── P3 播放时段管理：岛底「正在播放」状态 ──────────────────────
    // media_session_start 成功 → setMediaSession（标题/总时长/下一首/封面）；
    // media_session_stop / 自动截断 → clearMediaSession。桥读这些成员给 QML 画底栏。
    // 时长(ms)走 qint64（QML 侧用 real 承接，同 fileTransfer 口径）。
    void setMediaSession(const QString &title, qint64 totalMs,
                         const QString &nextTitle, const QString &nextCover);
    void clearMediaSession();
    // 计时器推进已播毫秒（ffplay 无进度回调，用墙上钟估）；mediaOn 时同步给 QML。
    void advanceMediaElapsed(int ms);
    bool mediaOn() const { return m_mediaOn; }
    // 当前曲目名（岛底栏「正在播放」+ 播放时段对话框现状展示用；缺省为文件名）。
    QString mediaTitle() const { return m_mediaTitle; }

    // ── 组件7 播放四态细化（2026-10-15，用户点单）──────────────────
    // 原呈现只有「在播 / 不在播」两分：学生看不出"刚要开始 / 快结束了 /
    // 已经播完 / 为什么没播"。这里补一个**相位**，相位由 C++ 单点判定
    // （口径唯一，QML 只按串切文案/配色，不自己算）：
    //   ""         无媒体内容
    //   "starting" 广播开始 —— setMediaSession 后 2.5s 内（提示"即将播放"）
    //   "playing"  播放中   —— 常态
    //   "ending"   快结束   —— 剩余 ≤ max(15s, 总长 10%)（提示"即将结束 + 下一首"）
    //   "ended"    播完     —— ffplay 自行退出，保留歌名 2.5s 再收起
    //   "blocked"  禁播     —— 时段拦截，播放条家族的一态（alert + 无进度，3.5s 收）
    QString mediaPhase() const { return m_mediaPhase; }
    // 禁播提示：main.cpp 在 media_session_start 被播放时段拦住时调（替代原来的
    // 纯 failed 回执——学生端也该知道"为什么没播"，不是只有管理端看到 failed）。
    void mediaBlocked(const QString &reason);
    // 播放自然结束：main.cpp 接 ffplay 的 finished 后调。
    // ⚠️ 与"手动停"分开：stopMediaSession()（media_session_stop / 时段自动截断）
    //    不走这里，否则"老师点了停止"会被岛报成"播放结束"。
    void mediaFinished();

    // ── 组件5 祝福弹幕（2026-10-15）──────────────────────────────
    // 岛内输入行投稿 → DanmakuWidget 全屏浮层（设计文档裁决：弹幕是独立浮层，
    // 非岛组件；岛内只留输入行）。端侧先行：本机冷却 + 敏感词闸门都在
    // DanmakuWidget；云端投稿/审核端点由组件6 经 CloudReporter 旁路上报。
    // 返回 DanmakuWidget::BlessingResult：0=已入队 1=敏感词 2=冷却中 3=内容无效。
    int sendBlessing(const QString &text);
    // 祝福冷却剩余秒数（0=可发）。QML 倒计时骑 bridge 的 blessingCooldownSec。
    int blessingCooldownSec() const;

    // ── 组件6 云端上报旁路（2026-10-15）──────────────────────────
    // 岛**不直接依赖** main.cpp 的 g_ws / 云端协议：由 main.cpp 注入一个发送器
    // （lambda 闭包捕获 g_ws + makeEnvelope），岛只负责"什么时候该上报什么"。
    // 这样 island 单测/离屏探针不注入也能跑（reporter 为空即静默跳过），
    // 且将来换传输方式（直连/插件宿主转发）不用改岛一个字符。
    // payload 形状见 island.cpp reportXxx 的各注释（与云端 interact.js 的字段对应）。
    using CloudReporter = std::function<void(const QString &type, const QJsonObject &payload)>;
    void setCloudReporter(CloudReporter reporter);
    // 反馈/点赞上报（kind: "like" | "feedback"）。QML「好听」单击后经桥调到这里。
    void reportFeedback(const QString &kind, int value = 1);
    // 点歌提交上报（source: "search" | "hot" | "manual"）。QML 搜索页提交钮经桥调到这里。
    void submitSongRequest(const QString &title, const QString &artist, const QString &source);

signals:
    void expandedChanged(bool expanded);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;      // 2026-10-11：QQuickWidget 跟着铺满
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;    // 触控：下拉展开 / 上推收起
    void mouseReleaseEvent(QMouseEvent *) override; // 触控：拖动过就不算点击

private:
    explicit IslandOverlay(QWidget *parent = nullptr);
    ~IslandOverlay() override;

    void onFadeFinished();     // 淡入/淡出共用 finished：只有"正在淡出"才 hide（见 .cpp 注释）

    void openInShell();        // 按 m_openPath 打开所在文件夹并选中该文件
    bool hitOpenButton(const QPoint &pt) const;   // 「▸ 打开」按钮命中区
    QVector<QRect> actionButtonRects() const;                        // 交互按钮行（确认/快捷回复）
    bool hitActionButton(const QPoint &pt, int *index) const;        // 命中哪一枚按钮
    void enterIdle();          // 收回成空闲半胶囊
    void paintBigForm();       // Centered / Fullscreen 的独立绘制（与胶囊绘制分开，互不干扰）
    QRect geometryForIdle() const;
    QRect geometryForContent(int shapeHeight) const;
    QRect geometryForForm(Form f) const;   // 按形态算目标几何
    void animateGeometry(const QRect &target);   // 弹性缓动改位置/大小
    // （原 positionForCurrent()/applyTheme() 只有声明、全工程无定义，按"不留看起来有能力其实没有的接口"删掉）

    // ── 组件5 祝福弹幕（2026-10-15）──────────────────────────────
    // 懒建并缓存弹幕层单例；首次拿到时把冷却倒计时信号（1Hz）转到 bridge，
    // 由 sync() 全量 NOTIFY 送到 QML（与 media elapsed 同频率先例）。
    DanmakuWidget *ensureDanmaku();

    // ── 组件6 云端上报旁路（2026-10-15）──────────────────────────
    // main.cpp 注入的发送器（见公开区 CloudReporter）。空 = 未接云端
    // （离屏探针/单测场景）：所有上报静默跳过，本地行为不受影响。
    CloudReporter m_cloudReporter;
    // 上报收口：reporter 空即跳过。三个公开上报方法（reportFeedback /
    // submitSongRequest / sendBlessing 内的 blessing）都走它。
    void reportToCloud(const QString &type, const QJsonObject &payload);

    // ── 历史通知（2026-10-11 · 持久化）──────────────────────────
    // 一条历史通知的内存模型。JSON 落盘到 LOCALAPPDATA/xingjikong/island_history.json。
    struct HistoryItem {
        QString title;
        QString desc;
        QString icon;
        qint64  ts = 0;           // Unix 时间戳（秒）
        QString timeStr;          // 预格式化时间串（"HH:mm" 或 "昨天 HH:mm" 或 "M/d HH:mm"）
        bool    fromAdmin = false; // 2026-10-13：管理端下发（历史排序时置顶）
    };
    QVector<HistoryItem> m_history;
    static constexpr int kMaxHistory = 50;   // 最多保留 50 条，超出按 FIFO 清旧
    static constexpr int kSpectrumBands = 16;   // §6.3 语音频谱带数（QML 按 bands 长度渲染）
    // 2026-10-13（用户「每条消息保存时间为一周，一周后清除历史」）：超过 7 天的历史
    // 在加载/归档时剔除（loadHistory 与 archiveToHistory 都过一遍 pruneHistory）。
    static constexpr qint64 kHistoryKeepSecs = 7 * 24 * 3600;
    void archiveToHistory();     // 把当前 m_title/m_desc/m_icon 归档
    void loadHistory();          // 从 JSON 加载
    void saveHistory() const;    // 写 JSON
    void pruneHistory();         // 剔除超期（>7 天）条目；不落盘，调用方决定何时 save
    QString historyPath() const; // LOCALAPPDATA/xingjikong/island_history.json

    QString m_title;
    QString m_desc;
    QString m_icon;
    bool m_fromAdmin = false;   // 2026-10-13：当前这条是否来自管理端（归档时写进 HistoryItem）
    int m_durationMs = 5000;
    bool m_expanded = false;
    Form m_form = Form::Capsule;
    bool m_light = false;
    bool m_interactive = false;   // 默认穿透
    bool m_idle = true;           // 空闲与否（无任务/通知）
    bool m_idleEnabled = true;    // 空闲半胶囊开关
    bool m_hadContent = false;    // 从来没显示过内容 ⇒ 展开时给"暂无通知"
    bool m_contentActive = false; // 这条内容还在活动期（决定收起是回"标题胶囊"还是直接回空闲半胶囊）
    // 淡出中：m_fadeAnim 的 finished 被淡入/淡出共用，只有这个为真时 finished 才 hide。
    // ⚠️ 少了它，dismiss 建立的 finished→hide 会在**下一次淡入结束**时把新提示灭掉
    //（STE_QT_ISLAND_IDLE=0 下表现为"第一条之后每条提示只闪 220ms"）。
    bool m_closing = false;
    // 触控手势：按下点（**全局坐标**）+ 是否按下 + 是否已判定为拖拽
    // ⚠️ 用全局坐标：拖拽会同时触发几何动画，窗口自己会位移（空闲 y=10 → 展开 y=24），
    //    局部坐标 ev->pos() 会被窗口位移污染，同一屏幕位置算出的 dy 会漂。
    QPointF m_pressGlobal;
    bool m_pressed = false;       // ⚠️ 不能用 m_pressGlobal.isNull() 当哨兵：(0,0) 是合法按下点
    bool m_dragged = false;

    QString m_openPath;           // 非空 = 这条提示可点击打开（explorer /select）

    // ── 交互按钮（2026-10-09 · 确认并入岛本体）──
    // 非空 ⇒ 岛体不穿透、不自动收，等点击；点击后回调一次即清空并收回。
    QStringList m_actions;
    std::function<void(const QString &)> m_onAction;
    QString m_severity;           // 仅全屏形态配色：remind/inform/urgent（空=黑底）

    // ── #198 文件传输进度 ──
    bool   m_ftActive = false;    // 岛上是否正显示一条文件传输卡
    QString m_ftName;             // 传输中的文件名
    qint64 m_ftBytes = 0;         // 已接收字节
    qint64 m_ftTotal = 0;         // 总字节（<=0 表示大小未知）

    // ── §6.3 语音频谱（2026-10-13）──
    bool m_spectrumOn = false;        // 对讲中（展开态显示频谱区）
    QVector<float> m_spectrumBands;   // 16 个 0..1 能量（IslandBridge 读它给 QML）
    qint64 m_spectrumLastMs = 0;      // 最近一次有数据的时刻（静音 800ms 归零，与旧件同口径）

    // ── P3 播放时段管理：岛底「正在播放」状态 ──
    bool   m_mediaOn = false;         // 当前有媒体在播（岛底栏显隐）
    QString m_mediaTitle;             // 当前曲目名（缺省用文件名）
    qint64  m_mediaElapsedMs = 0;     // 已播毫秒（由计时器推进；ffplay 无回调，用墙上钟估）
    qint64  m_mediaTotalMs = 0;       // 总时长（管理端下发；<=0 未知）
    QString m_mediaNextTitle;         // 下一首名（"下一首封面小演示"）
    QString m_mediaNextCover;         // 下一首封面路径/URL（空=不显示）
    // 2026-10-15 组件1（点歌互动）：媒体内容正占据岛面 —— 收起态画播放条、
    // 展开态画互动区/弹幕行（QML mediaIsContent = 本标志 && 标题==媒体标题）。
    // 另一职责：showIsland 归档守卫的"上一条是媒体 ⇒ 不归档"判据
    // （"正在播放"不是通知，不进历史；歌2顶歌1也不归档）。
    bool    m_mediaPresenting = false;

    // ── 组件7 播放四态（2026-10-15）──
    // 见公开区注释；相位字符串 + 一个复用的单发定时器（starting→playing /
    // blocked→清 / ended→收起三种迁延都走它，避免为每态各养一个 QTimer）。
    QString m_mediaPhase;
    QTimer *m_mediaPhaseTimer = nullptr;
    void armMediaPhaseTimer(int ms);

    QTimer *m_autoClose = nullptr;
    QTimer *m_dismissGuard = nullptr;   // dismiss 淡出的兜底：动画没走完也强制 hide
    QTimer *m_expandGuard = nullptr;    // 展开态硬回收：交互态展开不能被 m_autoClose 的守卫永远吞掉
    // A3（2026-10-11）：空闲态时钟刷新。500ms 一跳、只在"空闲且可见"时 update()，
    // 保证跨分不延迟（半秒一查足够），活动态/隐藏态下不做任何重绘（零开销）。
    QTimer *m_clockTick = nullptr;

    // ── A4（2026-10-11 · §6.1）：展开态长描述横向滚动（marquee）──
    // 需求：展开后标题+正文**滚动式展示、持续直到人为点击**。
    // ⚠️ 不引 MarqueeText 子控件：本岛是**纯手绘 QWidget、无子控件布局**（见本文件顶部
    //    设计说明），塞子控件要自己管 geometry/主题/生命周期，反而破坏既有结构。
    //    滚动直接做在 paintEvent 里：一个像素偏移 m_marqueeX，由 m_clockTick 推进，
    //    起停跟随"展开态 + 文本是否溢出"，点击/收起即归零。
    int  m_marqueeX = 0;            // 描述文本左缘相对绘制区左缘的偏移（负=已向左滚出）
    int  m_marqueeTextW = 0;        // 当前描述文本的像素宽（判断要不要滚）
    bool m_marqueeRun = false;      // 本条展开内容是否需要滚动（文本宽 > 可视宽）
    QPropertyAnimation *m_geoAnim = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;

    // ── 2026-10-11 · QML 绘制层 ─────────────────────────────────
    // 承载 qml/island/IslandRoot.qml 的子控件（铺满本窗口）。
    // ⚠️ 它是**子 QWidget**：本窗口的 WA_TransparentForMouseEvents 不会自动继承到它，
    //    所以 setInteractive() 里要同步给 m_quick 设同一个属性（见 island.cpp）。
    //    漏掉这一步的表现是"标称穿透、实际挡点击"，教室大屏上属于事故。
    QQuickWidget *m_quick = nullptr;
    IslandBridge *m_bridge = nullptr;

    // ── 组件5 祝福弹幕：弹幕层单例缓存（2026-10-15）──
    // 只是 danmakuWidget() 的缓存引用（单例活得比岛久），不负责生命周期。
    DanmakuWidget *m_danmaku = nullptr;

    friend class IslandBridge;   // 桥要读私有成员（见 island_bridge.cpp 的头注释）
    friend class IslandPrivate;   // 既有前向声明（历史遗留，保留）
};

#endif // STELARITH_ISLAND_H