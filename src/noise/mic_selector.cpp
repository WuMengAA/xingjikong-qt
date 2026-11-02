#include "mic_selector.h"

#include "noise_monitor.h"   // 采集类在 src/ 根（本工程未把 src/ 加进 include 路径）

#include <QPointer>
#include <QSettings>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include <climits>

namespace {
// 采集实例：仅"课中换麦"需要它；不在跑时本类直接读写持久化值。
QPointer<NoiseMonitor> g_live;

const QString kKey = QStringLiteral("noiseInputDevice");
}   // namespace

MicSelector *MicSelector::instance()
{
    static MicSelector *s_inst = nullptr;
    if (!s_inst) s_inst = new MicSelector();
    return s_inst;
}

MicSelector::MicSelector(QObject *parent) : QObject(parent) {}

void MicSelector::setLiveMonitor(NoiseMonitor *m) { g_live = m; }

int MicSelector::current() const
{
    if (g_live) return g_live->inputDevice();
    return QSettings().value(kKey, -1).toInt();
}

QString MicSelector::currentName() const
{
    const int id = current();
    if (id < 0) return QStringLiteral("系统默认");
    const QStringList names = NoiseMonitor::listInputDevices();
    return names.value(id, QStringLiteral("麦克风 %1").arg(id + 1));
}

QString MicSelector::statusText() const
{
    return QStringLiteral("当前：%1").arg(currentName());
}

QVariantList MicSelector::options() const
{
    const int cur = current();
    QVariantList out;

    // 第一项恒为"系统默认"（-1）：用户换错麦想退回默认时不用猜哪个是原来的。
    QVariantMap def;
    def.insert(QStringLiteral("id"), -1);
    def.insert(QStringLiteral("name"), QStringLiteral("系统默认"));
    def.insert(QStringLiteral("supporting"),
               cur < 0 ? QStringLiteral("当前使用") : QStringLiteral("点击切换"));
    out << def;

    const QStringList names = NoiseMonitor::listInputDevices();
    for (int i = 0; i < names.size(); ++i) {
        QVariantMap d;
        d.insert(QStringLiteral("id"), i);
        d.insert(QStringLiteral("name"), names.at(i));
        d.insert(QStringLiteral("supporting"),
                 cur == i ? QStringLiteral("当前使用") : QStringLiteral("点击切换"));
        out << d;
    }
    return out;
}

void MicSelector::select(int deviceId)
{
    const int count = static_cast<int>(NoiseMonitor::listInputDevices().size());
    if (deviceId < -1 || deviceId >= count) return;   // 越界忽略（界面索引陈旧时不炸）
    if (g_live) {
        g_live->setInputDevice(deviceId);   // 采集在跑：当场切换 + 自己写盘
    } else {
        persist(deviceId);                  // 未采集：只落盘，下次打开采集生效
    }
    emit currentChanged();
}

void MicSelector::persist(int deviceId) const
{
    QSettings s;
    if (s.value(kKey, INT_MIN).toInt() != deviceId) {
        s.setValue(kKey, deviceId);
        s.sync();
    }
}

void MicSelector::registerQmlType()
{
    // 幂等：静态初始化 + 装配期各调一次，重复注册视为已注册。
    static bool done = false;
    if (done) return;
    done = true;
    qmlRegisterSingletonInstance("Stelarith.Mic", 1, 0, "MicSelector", MicSelector::instance());
}

// 为什么放在静态初始化而不是 main 的装配段：
//   QML 里 `import Stelarith.Mic 1.0` 在**模块未注册时是硬错误**（整页空白且不报业务错）。
//   装配段虽然也早于面板创建，但探针/其它入口可能自带引擎先加载设置页 ⇒ 那时还没注册。
//   静态初始化早于 main ⇒ 任何引擎都拿得到。qmlRegisterSingletonInstance 只写 Qt 的类型
//   注册表（内部函数局部静态量），不依赖 QCoreApplication 已创建。
namespace {
struct QmlTypeAutoReg {
    QmlTypeAutoReg() { MicSelector::registerQmlType(); }
};
[[maybe_unused]] const QmlTypeAutoReg g_qmlTypeAutoReg;
}   // namespace
