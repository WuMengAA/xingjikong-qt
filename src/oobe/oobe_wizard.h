// 星集控被控端 · 首次启动引导（OOBE Wizard）数据面（2026-10-20）
//
// 为什么单独一个类（而不是塞进 agent_ui.h / main.cpp）：
//   AUTOMOC 会把「被 main.cpp include 的头」单独编成一个 TU（见 agent_ui.h 头部三连坑），
//   那个 TU 里**看不到 main.cpp 的任何 static 符号**。所以本类只做「绑定这件事」，
//   真正落盘（班级码/班级名）与重连由 main.cpp 接 boundSucceeded 信号完成 —— 单向依赖，
//   本文件不引用 main.cpp 一个符号。
//
// 三个对外契约（QML 只认这三个）：
//   ① bindingState：Idle → WaitingForScan → Success / Failed
//   ② submitBindingCode(code)：输入绑定码提交（大写字母+数字，6 位）
//   ③ skipWizard()：「稍后配置」——关掉向导、进未绑定/离线主界面，不阻断启动
//
// 后端接口（站点侧，均带 uid）：
//   GET  /api/device/bind-qr?uid=…        → {ok:true, code:"A7X9Q2", matrix:["0101…", …]}
//   GET  /api/device/bind-status?code=…&uid=… → {bound:true, classCode:"…", className:"…"}
//   POST /api/device/activate?code=…&uid=…    → {ok:true, classCode, className}（既有端点，复用）
//
// ⚠️ 二维码渲染口径：本机**不内置 QR 编码器**（Qt 没有；手写 RS 纠错容易生成扫不出来的码，
//    比不显示更糟）。矩阵由后端给，QML 只负责画。后端没给矩阵 ⇒ 友好降级到「输入绑定码」，
//    绝不留白屏、绝不画假码。
//
// ⚠️ 文案纪律：本文件所有对外字符串只出现「班级 / 绑定码 / 扫码」，不出现 wss、token、接口名。

#ifndef STELARITH_OOBE_WIZARD_H
#define STELARITH_OOBE_WIZARD_H

#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QTimer;

class OobeWizard : public QObject
{
    Q_OBJECT
public:
    /** 绑定状态机。QML 只读这个枚举决定界面（WaitingForScan 时显示二维码页）。 */
    enum BindingState {
        Idle = 0,             // 什么都没做（欢迎页 / 选择方式）
        WaitingForScan = 1,   // 二维码已出，等手机扫
        Success = 2,          // 绑定成功（班级信息已拿到）
        Failed = 3            // 失败（errorText 里有能照着做的原因）
    };
    Q_ENUM(BindingState)

    explicit OobeWizard(QObject *parent = nullptr);
    ~OobeWizard() override;

    // ── 只读给 QML 的状态 ────────────────────────────────
    Q_PROPERTY(int bindingState READ bindingState NOTIFY bindingStateChanged)
    Q_PROPERTY(QString machineCode READ machineCode NOTIFY infoChanged)
    Q_PROPERTY(QString className READ className NOTIFY infoChanged)
    Q_PROPERTY(QString classCode READ classCode NOTIFY infoChanged)
    // 2026-10-25：设备用途（classroom / broadcast）。OOBE **第一步**就问它——
    // 因为用途决定后面绑的是什么（班 or 广播站）、以及这台机器开哪套外壳。
    // 它比绑定更早生效：选完立刻由 main.cpp 落盘并补发 register。
    Q_PROPERTY(QString deviceRole READ deviceRole NOTIFY roleChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QVariantList qrMatrix READ qrMatrix NOTIFY qrChanged)
    Q_PROPERTY(QString qrCodeText READ qrCodeText NOTIFY qrChanged)
    Q_PROPERTY(bool qrBusy READ qrBusy NOTIFY qrChanged)

    int bindingState() const { return m_state; }
    QString machineCode() const { return m_uid; }
    QString className() const { return m_className; }
    QString classCode() const { return m_classCode; }
    QString deviceRole() const { return m_role; }
    QString errorText() const { return m_error; }
    QVariantList qrMatrix() const { return m_qrMatrix; }
    QString qrCodeText() const { return m_qrCode; }
    bool qrBusy() const { return m_qrBusy; }

    /** 机器码（register 用的 uid）+ 站点基址：由 main.cpp 在显示向导前注入。 */
    void setMachineCode(const QString &uid);
    void setSiteBase(const QString &siteBase);
    /** 注入本机已保存的用途（重开向导时默认选中上次的选择）。 */
    void setInitialRole(const QString &role);

    // ── QML 可调 ─────────────────────────────────────────
    /** 进入扫码页：拉一次二维码并开始每 2 秒查一次绑定结果。 */
    Q_INVOKABLE void startScan();
    /** 换一张（旧码随即作废；后端每次给新 code）。 */
    Q_INVOKABLE void refreshQrCode();
    /** 输入绑定码提交。code 只接受 6 位大写字母/数字（QML 已限，C++ 再兜一次）。 */
    Q_INVOKABLE void submitBindingCode(const QString &code);
    /** 「暂时跳过，稍后配置」：关向导、进未绑定/离线主界面。 */
    Q_INVOKABLE void skipWizard();
    /** 成功页「完成配置，进入系统」。 */
    Q_INVOKABLE void finishWizard();
    /** 关掉错误提示（错误文案保留，供页面内提示行复用）。 */
    Q_INVOKABLE void dismissError();
    /** 选用途（'classroom' / 'broadcast'）。选完立即发 roleChosen —— 用途不等绑定完成。 */
    Q_INVOKABLE void chooseRole(const QString &role);
    /** 6 位绑定码是否合法（QML 用来控制「确认绑定」按钮亮灭）。 */
    Q_INVOKABLE bool isCodeFormatValid(const QString &code) const;

signals:
    void bindingStateChanged();
    void infoChanged();
    void errorTextChanged();
    void qrChanged();
    void roleChanged();
    /** 用途已选（main.cpp 落盘 + 补发 register）。与 wizardFinished 独立：
     *  用户选了广播机却中途跳过绑定，用途也必须已经生效。 */
    void roleChosen(const QString &role);
    /** 向导结束（bound=true 表示已绑定；false 表示用户跳过）。main.cpp 据此落盘标记。 */
    void wizardFinished(bool bound);
    /** 绑定成功：classCode 可能为空（老站点不回），className 可能为空（同上）。 */
    void boundSucceeded(const QString &classCode, const QString &className);

private:
    void setState(int s);
    void fail(const QString &friendly);
    void succeed(const QString &classCode, const QString &className);
    void stopScanning();
    void pollOnce();

    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_poll = nullptr;
    QString m_uid;
    QString m_siteBase;
    int m_state = Idle;
    QString m_className;
    QString m_role = QStringLiteral("classroom");
    QString m_classCode;
    QString m_error;
    QVariantList m_qrMatrix;
    QString m_qrCode;        // 后端给的一次性扫码码
    bool m_qrBusy = false;
    int m_pollCount = 0;
    bool m_done = false;     // 防空转：向导已结束就不再发起请求
};

#endif // STELARITH_OOBE_WIZARD_H
