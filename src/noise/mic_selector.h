#pragma once
// 麦克风选择器 —— 给 QML 用的薄桥（2026-10-31 · 用户点单「麦克风切换」）
//
// 为什么单独一个类而不是往 AgentUi 里塞方法：
//   AgentUi 与 AgentShell 都在 agent_ui.h（707 行，规模门禁正好卡在登记值），
//   加十行就红。选择器的逻辑本来就属噪音域，放这里更对位。
//
// 与 NoiseMonitor 的分工：
//   · 枚举设备 / 读写选择 = waveIn 与 QSettings，本类负责（不依赖采集实例是否在跑）；
//   · 真正的采集 = NoiseMonitor，本类只把选择结果交给它（在跑则当场生效）。
//   所以"课中换麦"能立刻切，"课前换麦"落在 QSettings，下次采集按新设备打开。
//
// ⚠️ 用户铁律「QML 零逻辑」：界面要显示的文案（当前项 / 是否选中）全部由本类算好，
//    QML 只做 `model: mic.options` 与 `onClicked: mic.select(id)`，不写三元不拼接。
#include <QObject>
#include <QVariantList>

class NoiseMonitor;

class MicSelector : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int current READ current NOTIFY currentChanged)          // -1 = 系统默认
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY currentChanged) // 「当前：xxx」
    Q_PROPERTY(QVariantList options READ options NOTIFY currentChanged) // [{id,name,supporting}]

public:
    static MicSelector *instance();

    int current() const;
    QString currentName() const;
    /** 卡片副标题（"当前：系统默认"）。拼串归 C++ —— QML 零逻辑铁律。 */
    QString statusText() const;
    /** 列表模型（含"系统默认"一项）：每项 {id, name, supporting}，supporting 由本类判定。 */
    QVariantList options() const;

    /** 切换：-1 = 系统默认。采集在跑则当场生效，否则只落盘（下次采集生效）。 */
    Q_INVOKABLE void select(int deviceId);

    /** NoiseMonitor 构造/析构时自报，本类不反向依赖它的生命周期。 */
    static void setLiveMonitor(NoiseMonitor *m);

    /** QML 类型注册（幂等；静态初始化即调用，早于任何引擎）。 */
    static void registerQmlType();

signals:
    void currentChanged();

private:
    explicit MicSelector(QObject *parent = nullptr);
    void persist(int deviceId) const;
};
