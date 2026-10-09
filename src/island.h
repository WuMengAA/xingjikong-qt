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
// 与 NotifyWindow 的关系：NotifyWindow 保留（全屏遮罩/居中弹窗/紧急确认仍在它那里），
// 灵动岛接管 **island 形态** 与各类瞬态提示（语音/广播/考试倒计时等）——见 island.cpp。

#ifndef STELARITH_ISLAND_H
#define STELARITH_ISLAND_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPointer>
#include <QTimer>
#include <QPropertyAnimation>
#include <functional>

class QMouseEvent;
class QPaintEvent;

class IslandOverlay : public QWidget
{
    Q_OBJECT
public:
    // 单例：被控端全局唯一（同时间只显示一个灵动岛，新状态顶掉旧的）
    static IslandOverlay *instance();

    // Idle = 空闲时的矮胶囊（2026-10-07 用户要求"无任务、通知则收回顶部，显示半胶囊+横杠箭头"）。
    // 空闲态不隐藏、不消失：收成顶部一条常驻的半胶囊，中间一根横杠+箭头，
    // 点一下 / 往下一拉（触屏）就展开看最近那条活动。
    enum Shape { Idle = 36, Collapsed = 56, Expanded = 168 };

    // ── 呈现形态（2026-10-09 · #98 ClassIsland 式）──────────────────────────
    // 用户原话："弹窗则居中，全屏则放大全屏，要有过渡。"
    //   Capsule    顶部居中胶囊（Idle/Collapsed/Expanded 三档都归它）—— 绝大多数瞬态提示
    //   Centered   屏幕正中的卡片（通知这类"要停下来看清"的内容）
    //   Fullscreen 铺满整屏（紧急广播/强制提示，压住整个桌面）
    // ⚠️ 与 Shape 正交：Shape 只描述**胶囊那三档高度**，Form 描述"这条提示摆在哪、多大"。
    //    把两者混成一个枚举会让"全屏"被迫带一个高度值，是错的。
    enum class Form { Capsule, Centered, Fullscreen };

    // 显示一条（icon 为 lucide 图标名；title 主文案，desc 次文案）
    void showIsland(const QString &title, const QString &desc = QString(),
                    const QString &icon = QString(),
                    int durationMs = 5000, Form form = Form::Capsule);
    Form form() const { return m_form; }

    // ── 带交互按钮的通知（2026-10-09 · 把「确认」并入灵动岛本体）───────────────
    // actions 非空 ⇒ 本岛接管鼠标（不穿透）、**不自动收起**，等用户点中某一枚；
    // 点击后回调 onAction(该按钮文案)，随后收回空闲态。form 建议用 Centered/Fullscreen
    // （胶囊那档太矮放不下按钮，调用方需自行升到 Centered）。
    // durationMs<=0 表示"等点击、不超时"；不建议给确认类设超时（会吞掉学生的确认动作）。
    void showWithActions(const QString &title, const QString &desc, const QString &icon,
                         Form form, const QStringList &actions,
                         std::function<void(const QString &)> onAction,
                         int durationMs = 0);
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

signals:
    void expandedChanged(bool expanded);

protected:
    void paintEvent(QPaintEvent *) override;
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

    QString m_title;
    QString m_desc;
    QString m_icon;
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

    QTimer *m_autoClose = nullptr;
    QTimer *m_dismissGuard = nullptr;   // dismiss 淡出的兜底：动画没走完也强制 hide
    QTimer *m_expandGuard = nullptr;    // 展开态硬回收：交互态展开不能被 m_autoClose 的守卫永远吞掉
    QPropertyAnimation *m_geoAnim = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;

    friend class IslandPrivate;
};

#endif // STELARITH_ISLAND_H