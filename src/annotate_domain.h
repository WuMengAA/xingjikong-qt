#pragma once
// 2026-10-27：**批注 / 白板内核**（H 批，替代 ICC CE 的画图地位）。
//
// 为什么自研而不是调起 ICC CE（用户 00:42 裁决）：
//   "集成 ICC CE 相关元素，代替 ICC CE 的画图地位…成为我们软件的一部分，重构版的软件参考"
//   ⇒ 能力吸收进本进程，老师不需要再装第三方。
//
// ── 三条硬约束（都是用户/项目踩过的）──────────────────────────────
// 1. **禁用 QWidget**（用户 00:46："QWidget 尽量不要使用，会淘汰的"）
//    ⇒ 窗口用 `QQuickView`（QWindow 派生，不是 Widget），不用 QQuickWidget。
//    ⚠️ island.cpp:1887 提醒过 QQuickView 有"窗口跑到后面、点不到"的问题 ——
//       本域用 `Qt::WindowStaysOnTopHint` + `WS_EX_TOPMOST` 双保险（exam_mode.cpp:197 的经验），
//       并且必须盖住 PPT 全屏放映。
// 2. **渲染用 `QQuickPaintedItem`**（QPainter 画进 scenegraph），不用 QML Canvas ——
//    墨迹一多 Canvas 会掉帧（项目已有 QML 性能痛点）。
// 3. **穿透用 show/hide，不切 window flags** —— Qt 改 flags 会重建原生窗口
//    （闪烁 + 丢状态），批注层每按一次键就闪一下是不可接受的。
// 4. **无焦点触控**（用户 2026-10-27 07:09 点单）—— 落笔不许激活窗口：
//    一旦激活，前台从 PPT 放映切到我们，老师的翻页笔 / 空格翻页 / F5 立刻失效。
//    三层落实：`Qt::WindowDoesNotAcceptFocus`（Qt 会给 WS_EX_NOACTIVATE）
//    + `WS_EX_NOACTIVATE` 显式补一遍 + 原生事件过滤把 `WM_MOUSEACTIVATE` 回成
//    `MA_NOACTIVATE`（Qt 的 windows 插件仍可能自己请求激活）。
//    代价：窗口不接受焦点 ⇒ QML 收不到按键 ⇒ 逃生口改用**低层键盘钩子**
//    （只在批注层可见期间装，只吃 ESC/Ctrl+Z/Ctrl+Y/Ctrl+Shift+Z/Ctrl+E 五个组合，
//      其余键原样放行）。钩子回调只做队列投递，绝不阻塞。
// 5. **置顶上限：UAC 之上不谈，任务管理器之下**（用户同一条点单）。
//    Windows 没有"设置绝对层级"的 API，实际只有三档：
//      普通窗口 < 本层（WS_EX_TOPMOST） < 任务管理器（topmost + 系统特权） < UAC 安全桌面
//    ⇒ 本域只做 `HWND_TOPMOST`；若任务管理器正置顶可见，则把自己 `hwndInsertAfter`
//      插到它下面（「仅次于任务管理器」）。UAC 提示跑在独立安全桌面，本层**天生**压不过，
//      且绝不尝试（禁用 SetThreadDesktop / 抢 Winlogon 桌面那一套）。
//
// ── 与 PPT 的交界（I3 接口已留）────────────────────────────────
// 翻页后调用方用 `PptDomain::query()` 取页码，再 `setPageKey()` 让本域按页存取墨迹
// （对应 ICC CE 的 PPTInkManager）。本域不认识 PPT，只认"页键"。
#include <QColor>
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPainterPath>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QVector>

class QQuickView;
class QScreen;
class QTimer;

namespace AnnotateDomain {

// 一笔（一次按下到抬起）。橡皮也是"一笔"，只是合成模式不同。
struct Stroke {
    QVector<QPointF> pts;
    QColor  color;
    qreal   width = 4.0;
    bool    highlighter = false;   // 荧光笔：半透明 + 加宽 + 圆头
    bool    eraser = false;        // 橡皮：清掉命中区域的既有墨迹
};

/* ── 渲染器：QML 里声明为 `AnnotateCanvas` ──────────────────────── */
class Canvas : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY changed)
    Q_PROPERTY(int pageIndex READ pageIndex NOTIFY changed)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(QString tool READ tool NOTIFY changed)
public:
    explicit Canvas(QQuickItem *parent = nullptr);

    void paint(QPainter *p) override;

    // 输入（QML 的 MultiPointTouchArea / MouseArea 转发逻辑坐标）
    Q_INVOKABLE void beginStroke(qreal x, qreal y);
    Q_INVOKABLE void addPoint(qreal x, qreal y);
    Q_INVOKABLE void endStroke();

    // 编辑
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearAll();

    // 工具与样式
    Q_INVOKABLE void setTool(const QString &tool);      // pen / highlighter / eraser
    Q_INVOKABLE void setColor(const QString &hex);      // "#RRGGBB"
    Q_INVOKABLE void setWidth(qreal w);                 // 1..40

    int strokeCount() const { return m_strokes.size(); }
    QString tool() const { return m_tool; }

    // 白板背景（空 = 透明批注层；非空 = 不透明白板）
    void setBoardColor(const QColor &c);

    // 按页存取（I3：PPT 每页批注）。key 为空表示不分页。
    void setPageKey(const QString &key);
    QString pageKey() const { return m_pageKey; }

    // ── 白板多页（H2）────────────────────────────────────────────
    // 页序 = key 首次出现的顺序（不是字典序：PPT 跳到第 7 页再回第 3 页时，
    // 列表顺序应按"进入过"排，而不是把 '10' 排到 '2' 前面）。
    Q_INVOKABLE void addPage();      // 末页之后新建一页并切过去
    Q_INVOKABLE void nextPage();     // 下一页；已在末页则新建（连续讲课的自然动作）
    Q_INVOKABLE void prevPage();
    int pageCount() const { return m_pageOrder.size(); }
    int pageIndex() const;           // 1 基；0 = 还没分页（空 key）

    // 状态快照（给指令回执用）
    QString statusLine() const;

    // QML 按 ESC 时调它 → 域 hide（批注层全屏吃输入，必须有本地逃生口，
    // 否则老师被自己的批注层困住，只能等管理端发 stop）
    Q_INVOKABLE void requestStop() { emit stopRequested(); }

signals:
    void changed();
    void stopRequested();

private:
    void pushHistory();          // 把当前 m_strokes 压栈（改动前调用）
    QPainterPath pathOf(const Stroke &s) const;
    void switchTo(const QString &key);   // 存旧页 → 换入新页（不动页序）

    QVector<Stroke> m_strokes;
    QVector<QVector<Stroke>> m_undo;   // 撤销栈（快照式，简单可靠；墨迹量级不大）
    QVector<QVector<Stroke>> m_redo;
    QHash<QString, QVector<Stroke>> m_pages;   // 页键 → 墨迹（切页时换出/换入）
    QStringList m_pageOrder;                   // 页序（首次出现顺序）

    QString m_tool = QStringLiteral("pen");
    QColor  m_color = QColor(0xE5, 0x39, 0x35);   // 默认红笔（教室场景最常用、最显眼）
    qreal   m_width = 4.0;
    QColor  m_boardColor;                         // 无效 = 透明（批注模式）
    QString m_pageKey;

    bool m_drawing = false;
    Stroke m_cur;
};

/* ── 域：窗口生命周期 + 指令入口 ───────────────────────────────── */
class Overlay : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    static Overlay *instance();

    // 显示/隐藏（穿透 = hide，不切 flags）
    bool start(QString *err);
    void stop();
    bool isActive() const;

    Canvas *canvas() const { return m_canvas; }

    // 白板模式：bg 为空 → 透明批注层；"white"/"black"/"green"/"#RRGGBB" → 不透明白板
    void setBoard(const QString &bg);
    // 当前白板背景原样（空 = 透明批注层）—— 给管理端回执用
    QString boardBg() const { return m_boardBg; }

    QString statusLine() const;

    // 只建窗口不显示（探针用：验 QML 能否加载 / canvas 能否 findChild、
    // WS_EX_NOACTIVATE 是否真落上，不弹全屏、不碰桌面）。
    // 成功后 m_view 已存在但未 show。
    bool createHidden(QString *err);

    // 原生窗口句柄（void* = HWND；探针/诊断用，未建窗前为 nullptr）
    void *nativeHandle() const { return m_hwnd; }

    // 低层键盘钩子投递进来的热键
    // （1=ESC 收层 / 2=撤销 / 3=重做 / 4=清空 / 5=上一页 / 6=下一页）
    void handleHotkey(int action);

    // 原生事件过滤：拦 WM_MOUSEACTIVATE（无焦点触控最后一层保险）
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    // 置顶层级断言（任务管理器之下 / 其余 topmost）
    void assertZOrder();

private:
    Overlay() = default;
    bool ensureView(QString *err);    // 懒建 QQuickView（首次 start 时）
    void applyWindowStyles();         // WS_EX_TOPMOST|NOACTIVATE|TOOLWINDOW
    void installKeyboardHook();
    void uninstallKeyboardHook();

    QQuickView *m_view = nullptr;
    Canvas     *m_canvas = nullptr;   // QML 创建后 findChild 回填
    bool        m_boardMode = false;
    QString     m_boardBg;

    void  *m_hwnd = nullptr;          // HWND 缓存（void* 以免头文件拖进 <windows.h>）
    void  *m_kbHook = nullptr;        // HHOOK 同上
    QTimer *m_zTimer = nullptr;       // 3s 一次的置顶层级纠偏
};

// 注册 QML 类型（在创建 view 之前调用一次；重复调用安全）
void registerQmlTypes();

} // namespace AnnotateDomain
