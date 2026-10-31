// UiPageHost 僵尸页探针：pages 重算后旧实例是否残留且可见？
//
// 用户报告现象：「页面切换时有很多页面连续重叠在一起」。
// 怀疑：_buildAll() 只建不销毁、_sync() 只遍历**新的** pages
//       ⇒ 旧 id 的实例留在 pageArea 里且 visible 仍为 true，永久叠在最上层。
// 本探针不读代码猜，直接：
//   ① 建两页 → 记录 pageArea 子项数与可见数
//   ② 换掉清单里的一页（模拟 manifest 重算）→ 再数
//   ③ 若子项数只增不减、且可见数 > 1 ⇒ 僵尸页确认
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QDebug>

static int fails = 0;
static void chk(const char *name, bool ok, const QString &detail)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << " <- " << detail;
    if (!ok) ++fails;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("build/qml"));
    engine.addImportPath(QStringLiteral("build/qml/components"));

    const QString qml = QStringLiteral(R"(
import QtQuick
import QtQuick.Window
import "components"

Window {
    width: 640; height: 480
    visible: false
    property alias hostObj: host
    property int phase: 0

    QtObject {
        id: reg
        property var items: []
        property var env: ({ scope: "probe" })
        function navItems() { return items }
    }

    UiPageHost {
        id: host
        anchors.fill: parent
        registry: reg
        scope: "probe"
        window: parent
        entryBase: Qt.resolvedUrl("shell/pages/")
    }

    // pageArea 是 host 的第一个子项（Item），拿它数真实实例。
    // 用函数取值而不是 property 绑定：children 变化不会触发绑定重算。
    function areaItem() { return host.children.length > 0 ? host.children[0] : null }

    function stats() {
        var a = areaItem()
        if (!a) return "no-area"
        var n = 0, v = 0
        for (var i = 0; i < a.children.length; i++) { n++; if (a.children[i].visible) v++ }
        return n + "/" + v          // 总子项 / 可见子项
    }

    Component.onCompleted: {
        reg.items = [ { id:"a", title:"A", entry:"AgentSelfCheckPage.qml", order:1 },
                      { id:"b", title:"B", entry:"AgentRecordsPage.qml",  order:2 } ]
        Qt.callLater(function () {
            console.log("PH_STAGE1", stats(), "ids=", host.instantiatedIds().join(","))
            // ⚠️ 关键：必须**先切到 b**（让 b 变 visible=true），再换清单把 b 移出。
            //    否则 b 一直是 hidden，即使成为僵尸页也看不出来 —— 这正是我第一版探针
            //    漏掉的路径（visible 一直是 1，看不出问题）。
            host.goTo(1)
            console.log("PH_STAGE1b", stats(), "ids=", host.instantiatedIds().join(","))
            // 换掉 b → c（a 不变）。模拟 manifest 重算把"当前可见页"移出清单。
            reg.items = [ { id:"a", title:"A", entry:"AgentSelfCheckPage.qml", order:1 },
                          { id:"c", title:"C", entry:"AgentPluginsPage.qml", order:2 } ]
            Qt.callLater(function () {
                console.log("PH_STAGE2", stats(), "ids=", host.instantiatedIds().join(","))
                // 再切到第 1 页，看隐藏是否生效
                host.goTo(1)
                Qt.callLater(function () {
                    console.log("PH_STAGE3", stats(), "ids=", host.instantiatedIds().join(","))
                    Qt.quit()
                })
            })
        })
    }
}
)");

    QQmlComponent comp(&engine);
    comp.setData(qml.toUtf8(),
                 QUrl::fromLocalFile(QStringLiteral("build/qml/_probe_pagehost.qml")));
    if (comp.isError()) { qWarning() << comp.errorString(); return 2; }

    QHash<QString, QString> stages;
    QObject::connect(&engine, &QQmlEngine::quit, [&]() { app.quit(); });

    // 用 qInstallMessageHandler 抓 console.log（PH_STAGE*）
    static QHash<QString, QString> *sp = &stages;
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) {
        if (m.startsWith(QLatin1String("PH_STAGE"))) {
            const int sp1 = m.indexOf(QLatin1Char(' '));
            const QString key = m.left(sp1);
            (*sp)[key] = m.mid(sp1 + 1).trimmed();
            qInfo().noquote() << m;
        }
    });

    QObject *root = comp.create();
    if (!root) { qWarning() << "创建失败"; return 2; }
    app.exec();

    auto num = [&](const QString &k) {
        const QString s = stages.value(k);
        return s.section(QLatin1Char('/'), 0, 0).toInt();
    };
    auto vis = [&](const QString &k) {
        const QString s = stages.value(k);
        return s.section(QLatin1Char('/'), 1, 1).section(QLatin1Char(' '), 0, 0).toInt();
    };

    const int n1 = num("PH_STAGE1"), v1 = vis("PH_STAGE1");
    const int n2 = num("PH_STAGE2"), v2 = vis("PH_STAGE2");
    const int n3 = num("PH_STAGE3"), v3 = vis("PH_STAGE3");

    chk("① 两页时实例数 = 2", n1 == 2, QStringLiteral("n=%1 v=%2").arg(n1).arg(v1));
    chk("① 同时只有 1 页可见", v1 == 1, QStringLiteral("visible=%1").arg(v1));
    const int v1b = vis("PH_STAGE1b");
    chk("①b 切到第 2 页后仍只有 1 页可见", v1b == 1, QStringLiteral("visible=%1").arg(v1b));
    chk("② 换清单后实例数不该只增不减（僵尸页）", n2 <= 2,
        QStringLiteral("n=%1（换掉 1 页后仍是 %2 说明旧实例没销毁）").arg(n2).arg(n2));
    chk("② 换清单后仍只有 1 页可见", v2 == 1, QStringLiteral("visible=%1").arg(v2));
    chk("③ 切页后仍只有 1 页可见", v3 == 1, QStringLiteral("visible=%1").arg(v3));

    qInfo().noquote() << (fails == 0 ? "[结论] 无僵尸页" : "[结论] 有僵尸页/可见性失效");
    return fails == 0 ? 0 : 1;
}
