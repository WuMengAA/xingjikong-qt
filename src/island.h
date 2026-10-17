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
// ── 2026-10-17 更正：上面那条纪律是**当时的**约束，不再是全时段铁律。
//    此后公开接口有以下**纯增量**变化（只加不改，老调用点零改动）：
//      · 2026-10-15：setQmlObjectProperty（模块联动）、setMediaSession 等 P3/组件接口
//      · 2026-10-17 P0-0：setNoticeTimeForNext（插件通知的发生时刻，一次性注入）
//    仍然成立的纪律：**既有签名不许改**、**行为不许悄悄变**；
//    新增接口一律「不调用即零影响」。

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
    //
    // ── 2026-10-17 形态多样化（#330 剩余件）：新增两档外框 ──────────────
    //   Banner  顶部通栏横幅：宽 = 屏宽 − 48（上限 1280）、高 72（无描述 60）。
    //           解决的是胶囊**做不到**的事 —— 长文案。胶囊宽度由内容定且上限 640，
    //           长句子会被迫折成两行、高度跟着涨；Banner 是定宽通栏 + 单行省略，
    //           视觉上是"系统的横幅"，只吃顶部一条，不占中间课件区。
    //   Mini    右下角迷你角标：112×112（无描述时 96×96 且收成正圆）。
    //           给"常驻轻状态"用（传输中/在播/静默计时）—— 这些内容不值得占用
    //           屏幕顶部正中，但完全看不见又不放心。点一下**升格**成顶部胶囊并展开
    //           （角标只有 112px，读不全；见 setExpanded 里的升格分支）。
    //           ⚠️ 本岛默认**鼠标穿透**（m_interactive=false），角标常驻右下角，
    //              默认吃鼠标会把"点任务栏/桌面右下角"吞掉。要可点，由调用方
    //              自己 setInteractive(true)（届时点击走升格；不开就纯展示）。
    //   ⚠️ 这两档都**不能带交互按钮**（在 showWithActions 里强制降级到 Centered）：
    //      72px 高的横幅和 112px 的角标放不下「确认 / 快捷回复」那一排。
    enum class Form { Capsule, Centered, Fullscreen, Banner, Mini };

    // 显示一条（icon 为 lucide 图标名；title 主文案，desc 次文案）
    // fromAdmin（2026-10-13）：这条是**管理端**下发的通知 —— 归档进历史时打标，
    //   历史列表把管理端通知排在最前（用户「历史通知第一位优先显示管理端通知」）。
    void showIsland(const QString &title, const QString &desc = QString(),
                    const QString &icon = QString(),
                    int durationMs = 5000, Form form = Form::Capsule,
                    bool fromAdmin = false);
    Form form() const { return m_form; }

    // ── 通知「发生时刻」与时间文本（2026-10-17 「通知写上时间」）──────────────
    // 岛的**本体**现在也显示时间（以前只有历史列表有）—— 老师看到一条通知时
    // 不该还需要展开历史才能知道它是几点来的。
    // ⚠️ 顺手修掉一个真 bug：archiveToHistory() 原来拿"归档那一刻"当 ts，
    //    而归档发生在被下一条顶掉 / 收回空闲时，不是通知出现时
    //    （10:00 弹出、10:30 被顶掉 ⇒ 历史记 10:30；23:59 弹出、00:03 顶掉
    //     还会被分到第二天的「今天」组）。现改为 showIsland 落点 m_shownTs。
    qint64 noticeTs() const { return m_shownTs; }

    // ── T 档：任务（Task）—— 岛的唯一数据单位（2026-10-17）────────────────────
    // 为什么引入：岛原来是"一条内容"模型 —— 新通知顶掉旧的，`states` 只喂 1 行
    //   （island_bridge.cpp:118-138 的注释原话）。于是"边收文件、边放歌、又来一条通知"
    //   这种并行进度根本表达不了，只能互相顶掉。
    // 设计原则：**任何要显示的东西都实例化为一个 Task**；岛只做两件事——
    //   仲裁出前台任务、渲染前台 + 任务列表。
    //
    // ⚠️ 本步只是**引入数据结构并承载既有状态源**（通知/文件/媒体/频谱），
    //   不改任何既有 API 的显示行为；抢占/回落/过期扫描是后续步骤，届时再接。
    //   现有 14 处 islandShow 调用仍走原路径，行为零变化。
    struct Task {
        QString id;             // 稳定标识（更新/完成/动作回传用）
        QString tag;            // 同类去重键（"notice"/"file"/"media"/"voice"/"spectrum"）
        QString kind;           // 语义类别（notice/alert/exam/voice/file/media/spectrum）
        QString source;         // 来源：self / admin / plugin:<id> / peer:<device>
        QString title;
        QString desc;
        QString icon;
        int     priority = 5;   // 1..10，抢占依据（本步仅记录，未参与仲裁）
        Form    form = Form::Capsule;
        QString severity;       // 仅 Fullscreen 生效
        bool    sticky = false; // 不自动过期（等动作 / 等完成）
        qint64  expireAtMs = 0; // 到期时刻（ms；sticky 时忽略）
        double  progress = -1;  // -1 = 无进度条
        QStringList actions;    // 按钮（本步仅记录，未接内联动作）
        int     state = 0;      // 0=running 1=paused 2=done 3=failed
        bool    fromAdmin = false;
        qint64  shownTs = 0;    // 发生时刻（秒）

        // 便捷构造：本工程是 C++17，不支持指派初始化器（.field=）——
        // 实测用 .tag=... 会报 C7555/C7560。这里按位置给出最常用的那些字段，
        // 其余保持默认；调用处写成 Task(tag, kind, source, title, icon, priority, sticky, progress)。
        Task(const QString &tg = QString(), const QString &kd = QString(),
             const QString &src = QString(), const QString &ti = QString(),
             const QString &ic = QString(), int pr = 5, bool st = false,
             double pg = -1)
            : tag(tg), kind(kd), source(src), title(ti), icon(ic),
              priority(pr), sticky(st), progress(pg) {}
    };

    // 任务表（本步只承载，尚未参与显示仲裁；供桥与快照读取）
    const QVector<Task> &tasks() const { return m_tasks; }
    // 登记/更新一条任务（同 tag 合并）。本步只记录，不影响显示。
    void upsertTask(const Task &t);

    // ── VoiceHub 广播状态（2026-10-18）────────────────────────────────────
    // 校园点歌/广播站的「正在播放」。数据由云端集中轮询 VoiceHub 后经 WS 下发
    // （不由本机直连：60 台机器各连一次 = 60 条外网长连接）。
    // ⚠️ 岛上的进度用**本地时钟外推**：云端只在 30s 快照里给 position，
    //    若照搬不动，两次快照之间进度条是死的（看着像卡住）。
    struct BroadcastInfo {
        QString title;
        QString artist;
        double  duration = 0;      // 秒；0 = 未知（不画进度条）
        double  position = 0;      // 秒；**云端响应那一刻**的值，之后本地累加
        bool    isPlaying = false; // false = 暂停（保留标记、进度冻结）
        QString publisherName;     // 仅展示，绝不做任何鉴权判断
        int     listenerCount = 0;
        bool    hasNextUp = false;
        QString nextUpTitle;
    };
    // 设置/更新广播任务（tag="broadcast"）；广播结束调 clearBroadcastTask()。
    void setBroadcastTask(const BroadcastInfo &info);
    void clearBroadcastTask();
    // 广播的实时进度（0..1，duration 未知返回 -1）—— 桥组装 states 时调，
    // 这样"本地外推"只存在一处，桥只负责搬运。
    double broadcastProgress() const;
    // 取"下一个该显示的任务"：priority 降序，同优先取最新（shownTs 大者）。
    // 跳过已完成(done)与已过期的。没有则返回 nullptr。
    // ⚠️ 本步只用于**回落**（前台空了接手下一个），不用作抢占（抢占是下一步）。
    const Task *nextDisplayTask() const;
    // 过期扫描：把到期且非 sticky 的任务标为 done。1Hz 调用，不挂每任务定时器。
    void sweepExpiredTasks();
    QTimer *m_taskSweep = nullptr;   // T 档：1Hz 任务过期扫描

    // ── 沙箱验证设施（2026-10-17 P0-0）：岛状态快照 / 立即归档 ──────────────────
    // 为什么需要：P0-0 的端到端断言要回答"插件推的这一条**这个进程的岛到底收没收到**"。
    //   · 截图不行：岛是独立置顶窗，同机并存的生产实例也开着 26789 插件口，截到的可能是它；
    //   · 历史文件不够：只在"被顶掉/收回空闲"时才写盘 —— 长文案通知（本批主要测的长标题）
    //     在 idleEnabled=false 下根本走不到归档路径，文件可以一直不更新（本轮实测如此）。
    //   historySnapshotJson()：走**桥的 history() 同一套组装**导出历史快照（JSON 文本，只读）；
    //   archiveNow()         ：立刻把当前内容归档（archiveToHistory 同一套分类/去重，会写盘）。
    // 两者都**不改变任何默认行为**：没人调用 = 与改动前完全一致。
    QString historySnapshotJson() const;
    void archiveNow();


    // P0-0（2026-10-17）：给**下一条** showIsland 指定"发生时刻"（毫秒，Unix epoch）。
    // 存在的理由：插件推的通知在它调用的那一刻就该有时间戳；由 agent 处理那一刻兜底
    // 会让忙时（帧解码/OTA 下载同时进行）的通知时间晚几百毫秒，跨整分钟就显示错。
    // ⚠️ 一次性：showIsland 消费后清零；**不调用**则行为与以前完全一致（走 currentSecsSinceEpoch）。
    // ⚠️ 只设正数；<=0 视为"清除"，不会让下一条通知变成 1970 年。
    void setNoticeTimeForNext(qint64 atMs) { m_noticeHintMs = atMs > 0 ? atMs : 0; }

    // 时间文本格式化（唯一口径；归档与 bridge 组装共用这三个函数，别各写一套）
    //   timeStr  = HH:mm —— 概览面历史面板有日期分组，行内再带日期是冗余；
    //   timeFull = 今天 HH:mm / 昨天 HH:mm / M/d HH:mm —— 历史面(face 2)是平铺
    //              列表、没有分组标题兜日期，必须自带；
    //   notice   = HH:mm（跨天时才补 M/d）—— 岛上正在显示的那条。
    static QString historyTimeOf(qint64 ts);
    static QString historyTimeFullOf(qint64 ts);
    static QString noticeTimeOf(qint64 ts);
    // 日期分组标题（今天 / 昨天 / M月d日）—— **按当前日期现算**，绝不持久化死值
    // （上一轮把分组写进 JSON，导致"昨天存的今天"隔天打开仍显示「今天」）。
    static QString historyDateGroupOf(qint64 ts);

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
    void paintCompactForm();   // Banner / Mini 的手绘兜底（QML 缺失时的退路，形态见 Form 注释）
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

    // ── 历史通知（2026-10-11 · 持久化；2026-10-17 「记录与逻辑」升级）──────
    // 一条历史通知的内存模型。JSON 落盘到 LOCALAPPDATA/xingjikong/island_history.json。
    //
    // 2026-10-17 升级（用户点单「通知记录与逻辑」）：
    //   ① 原来只有 title/desc/icon/ts/timeStr —— 无法分类、无法标未读、无法搜索。
    //      补 id/category/level/read/dateGroup 四个语义字段（见 docs/目标/
    //      灵动岛通用简单历史通知 §二「所有通知走同一套格式」）。
    //   ② dateGroup 在**归档时**就格式化好（"今天"/"昨天"/"M月d日"）—— 不让 QML
    //      现算：被控端 QML 没有可信的时区/本地化保证，且列表滚动时反复算日期。
    //   ③ 老 JSON 缺字段 → loadHistory 给默认值（read=false / category=消息 /
    //      level=info / dateGroup 由 ts 现推），**不丢任何历史条目**。
    struct HistoryItem {
        QString id;               // 唯一 id（"<ts>-<seq>"），QML 标记已读/删除用
        QString title;
        QString desc;
        QString icon;
        qint64  ts = 0;           // Unix 时间戳（秒）
        QString timeStr;          // 预格式化时间串（"HH:mm" 或 "昨天 HH:mm" 或 "M/d HH:mm"）
        QString dateGroup;        // 分组标题（"今天"/"昨天"/"10月3日"）
        QString category;         // 分类（指令/告警/消息/系统）
        QString level;            // 级别（info/success/warning/error）
        bool    read = false;     // 已读标记（未读 = 有蓝点 + 计入未读数）
        bool    fromAdmin = false; // 2026-10-13：管理端下发（历史排序时置顶）
    };
    QVector<HistoryItem> m_history;
    static constexpr int kMaxHistory = 50;   // 最多保留 50 条，超出按 FIFO 清旧
    // 当前这条通知**出现**的 Unix 秒（0=无通知）。见 public 区 noticeTs() 的说明：
    // 历史 ts 用它，而不是"归档那一刻"。
    qint64 m_shownTs = 0;
    // P0-0（2026-10-17）：「下一条通知的发生时刻」一次性注入位（毫秒，0=无）。
    // 插件边界用它把"插件调用那一刻"带进来；showIsland 消费后立刻清零。
    qint64 m_noticeHintMs = 0;
    static constexpr int kSpectrumBands = 16;   // §6.3 语音频谱带数（QML 按 bands 长度渲染）
    // 2026-10-13（用户「每条消息保存时间为一周，一周后清除历史」）：超过 7 天的历史
    // 在加载/归档时剔除（loadHistory 与 archiveToHistory 都过一遍 pruneHistory）。
    static constexpr qint64 kHistoryKeepSecs = 7 * 24 * 3600;
    void archiveToHistory();     // 把当前 m_title/m_desc/m_icon 归档
    void loadHistory();          // 从 JSON 加载
    void saveHistory() const;    // 写 JSON
    void pruneHistory();         // 剔除超期（>7 天）条目；不落盘，调用方决定何时 save
    QString historyPath() const; // LOCALAPPDATA/xingjikong/island_history.json

    // ── 历史记录逻辑（2026-10-17）────────────────────────────────
    // 返回空串 = **不该进历史**（纯技术状态：心跳/重连/更新下载/连接中）。
    // 判据写在 island.cpp 的 historyCategoryOf()，集中一处，新增触发源不用改归档代码。
    static QString historyCategoryOf(const QString &title, const QString &desc,
                                     const QString &icon, bool fromAdmin);
    // 级别由文案推断（失败→error，完成→success，提醒→warning，其余 info）。
    static QString historyLevelOf(const QString &title, const QString &desc);
    // （historyDateGroupOf / historyTime* 已上移到 public 区：bridge 组装时要用）
    // 未读条数（QML 角标用）
    int historyUnreadCount() const;
    // 把某条按 id 标已读（找不到静默忽略）；返回是否真的变了
    bool markHistoryRead(const QString &id);
    // 全部标已读；返回本次由未读转已读的条数
    int  markAllHistoryRead();
    // 按 id 删一条；返回是否删掉了
    bool removeHistory(const QString &id);
    // 生成 id（ts + 同秒内递增序号，避免同秒两条撞 id）
    QString nextHistoryId(qint64 ts) const;

    QString m_title;
    QString m_desc;
    QString m_icon;
    // T 档：任务表（本步只承载，未参与仲裁）
    QVector<Task> m_tasks;
    // VoiceHub 广播进度锚点（云端给 position 的时刻 + 本地时钟，用于外推）
    BroadcastInfo m_bcInfo;
    double m_bcAnchorPos = 0;
    qint64 m_bcAnchorAtMs = 0;
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