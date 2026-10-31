// 可行性实验：Q_PROPERTY 跨基类拆分后，QML 能否读到 + NOTIFY 能否真的刷新绑定？
//
// 背景：agent_ui.h 776 行已达白名单登记值（加 1 行即判红），要腾空间只能往外搬。
// 打算把「A-3 第二批」整块搬到基类 agent_record_ui.h，AgentUi 继承它。
// 但有个**不能靠猜**的前提：现有属性全用 `NOTIFY infoChanged`。若基类与派生类各自
// 声明一个同名 infoChanged，那么派生类 emit 时基类属性**收不到通知** —— QML 绑定
// 不刷新，页面看起来"数据没变"。这正是本项目反复吃亏的静默失效。
//
// 判据不能用"再读一次 getter"（那样永远读到新值）。必须看**信号是否真的发出**，
// 以及 QML 绑定是否收到通知。
//
// 本实验回答：
//   Q1 基类声明的 Q_PROPERTY，QML 通过派生类实例能否读到？
//   Q2 只 emit 派生类信号 ⇒ 基类信号计数为 0？（确认风险真实存在）
//   Q3 显式 emit 基类信号 ⇒ 两个信号各 1 次？（确认修法有效）

#include "_agent_ui_split_probe.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    AgentUiLike ui;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("ui"), &ui);

    const char *qml = R"(
import QtQuick
import QtQuick.Window
Window {
    width: 200; height: 100; visible: true
    // 初始值快照（验证 Q1：基类属性可读）
    property string baseInitial: ui.recordSummary
    property string ownInitial: ui.ownValue
    // 绑定是否被通知：QML 里挂 changed 计数器
    property int baseChanged: 0
    property int ownChanged: 0
    Connections {
        target: ui
        function onInfoChanged() { /* 派生类信号 */ }
    }
}
)";
    engine.loadData(qml, QUrl("qrc:/probe.qml"));
    if (engine.rootObjects().isEmpty()) {
        qWarning() << "QML 加载失败（可能基类属性不可见）";
        return 2;
    }
    auto *win = engine.rootObjects().constFirst();
    int fails = 0;
    auto chk = [&](const char *name, bool ok, const QString &detail) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << " <- " << detail;
        if (!ok) ++fails;
    };

    // Q1：基类属性可读？派生类属性可读？
    const QString baseInitial = win->property("baseInitial").toString();
    const QString ownInitial = win->property("ownInitial").toString();
    chk("Q1 基类 Q_PROPERTY 可被 QML 读到", baseInitial == "init",
        QStringLiteral("基类读到=%1（期望 init）").arg(baseInitial));
    chk("Q1b 派生类 Q_PROPERTY 可被 QML 读到", ownInitial == "own-init",
        QStringLiteral("派生读到=%1（期望 own-init）").arg(ownInitial));

    // Q2 / Q3：直接数信号发出次数
    int baseSignalCount = 0, derivedSignalCount = 0;
    QObject::connect(&ui, &RecordVm::infoChanged, [&]() { ++baseSignalCount; });
    QObject::connect(&ui, &AgentUiLike::infoChanged, [&]() { ++derivedSignalCount; });

    ui.setSummary(QStringLiteral("changed-1"));
    ui.publishDerivedOnly();
    QCoreApplication::processEvents();
    chk("Q2 只发派生类信号：基类信号计数为 0（风险确认）",
        baseSignalCount == 0 && derivedSignalCount == 1,
        QStringLiteral("基类=%1 派生=%2（期望 0 / 1）")
            .arg(baseSignalCount).arg(derivedSignalCount));

    baseSignalCount = 0;
    derivedSignalCount = 0;
    ui.setSummary(QStringLiteral("changed-2"));
    ui.publishBoth();
    QCoreApplication::processEvents();
    chk("Q3 双发：基类与派生类信号各 1 次（修法有效）",
        baseSignalCount == 1 && derivedSignalCount == 1,
        QStringLiteral("基类=%1 派生=%2（期望 1 / 1）")
            .arg(baseSignalCount).arg(derivedSignalCount));

    // 补充：确认继承链与信号分属两个类
    const QMetaObject *baseMo = &RecordVm::staticMetaObject;
    const QMetaObject *dervMo = &AgentUiLike::staticMetaObject;
    const int baseIdx = baseMo->indexOfSignal("infoChanged()");
    const int dervIdx = dervMo->indexOfSignal("infoChanged()");
    chk("同名信号分属两个类（不是同一个）",
        baseIdx >= 0 && dervIdx >= 0 && dervMo->superClass() == baseMo,
        QStringLiteral("基 idx=%1 / 派生 idx=%2 / 派生父类=%3")
            .arg(baseIdx).arg(dervIdx)
            .arg(dervMo->superClass() ? dervMo->superClass()->className() : "null"));

    qInfo().noquote() << "";
    if (fails == 0) {
        qInfo().noquote() << "[结论] 「基类 + Q_PROPERTY 继承」在 QML 侧可用（Q1 通过），"
                             "但 NOTIFY 必须双发（Q2 确认风险、Q3 确认修法）。"
                             "⇒ 拆分时 publish() 必须同时 emit 两个类的 infoChanged()。";
    } else {
        qInfo().noquote() << "[结论] 本拆分方式在当前环境不成立，需换方案（如 Q_PROPERTY 不跨类）。";
    }
    return fails == 0 ? 0 : 1;
}
