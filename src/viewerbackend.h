// 星集控 · 管理端后端（ViewerBackend）
//
// 为什么单独抽出来：management 端原来把「网络/协议/数据」和「界面」揉在同一个 main.cpp 里（约 750 行），
// 改样式要碰逻辑、改逻辑怕碰 UI。界面要换 Qt Quick / QML（见《星集控-电脑端UI定稿方案-2026-10-03.md》），
// 所以先把逻辑抽成**与界面无关**的 QObject —— QML 与 QWidget 都能直接用同一份。
//
// 职责边界（只做这些，别往里塞界面的事）：
//   连接 / 鉴权 / 重连退避 / 设备表 / 画面帧 / 指令下发 / 回执 / 错误
// 界面层的：弹窗、布局、状态栏文案拼装、存现场图 —— 都留在界面侧。

#pragma once

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QTimer;
class QWebSocket;
class QWebEngineView;

/** 全局日志（定义在 main.cpp；backend 与界面共用同一份落盘日志）。 */
void logf(const char *fmt, ...);

class ViewerBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool authed READ isAuthed NOTIFY authedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool statusWarn READ statusWarn NOTIFY statusTextChanged)
    Q_PROPERTY(QJsonArray devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString currentUid READ currentUid WRITE setCurrentUid NOTIFY currentUidChanged)
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY statsChanged)
    Q_PROPERTY(int lastFrameBytes READ lastFrameBytes NOTIFY statsChanged)
    Q_PROPERTY(QString cloudUrl READ cloudUrl NOTIFY cloudUrlChanged)
    // ── 文件推送（file_push / file_chunk / file_done 三步走）──
    // 状态：idle（没在推）→ pushing（等被控端收下会话）→ sending（一片一片推）
    //       → done（推完，fileTarget 是被控端落盘路径）／ failed（中断，fileError 有原因）
    Q_PROPERTY(QString fileState READ fileState NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileProgressChanged)
    Q_PROPERTY(qint64 fileBytes READ fileBytes NOTIFY fileProgressChanged)
    Q_PROPERTY(qint64 fileTotal READ fileTotal NOTIFY fileProgressChanged)
    Q_PROPERTY(int filePercent READ filePercent NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileError READ fileError NOTIFY fileProgressChanged)
    Q_PROPERTY(QString fileTarget READ fileTarget NOTIFY fileProgressChanged)

    // ── WebRTC 收流（T-3，2026-10-04）──
    // 管理端做 answer 侧：被控端 captureStream 推 video track → 云端中继 →
    // 这里 QWebEngineView 离屏跑 RTCPeerConnection.setRemoteDescription(offer) →
    // createAnswer 回云端 → ontrack 拿到 track 塞进 <video> → 抽帧走现有 frame 通道。
    // 只收、不发——发是 agent 侧的事。
    Q_PROPERTY(bool rtcReady READ rtcReady NOTIFY rtcStateChanged)
    Q_PROPERTY(QString rtcState READ rtcState NOTIFY rtcStateChanged)

public:
    explicit ViewerBackend(QObject *parent = nullptr);
    /** 析构：QWebEngineView 必须在 event loop 停止之后销毁，否则 Chromium 直接崩。 */
    ~ViewerBackend() override;

    /** 读环境变量（STE_VIEWER_URL / STE_VIEWER_TOKEN）并开始连接。 */
    void start();

    bool isConnected() const { return m_connected; }
    bool isAuthed() const { return m_authed; }
    QString statusText() const { return m_statusText; }
    bool statusWarn() const { return m_statusWarn; }
    QJsonArray devices() const { return m_devices; }
    QString currentUid() const { return m_currentUid; }
    QImage frame() const { return m_frame; }
    double fps() const { return m_fps; }
    int frameCount() const { return m_frameCount; }
    int lastFrameBytes() const { return m_lastFrameBytes; }
    /** 云端地址（设置页要显示；它是启动时从环境变量读的，不是用户填的）。 */
    QString cloudUrl() const { return m_url; }

    // 文件推送的 7 个读数：Q_PROPERTY 的 READ 就是这些，少了任何一个，
    // moc 生成的 _t->xxx() 和本文件里的 `ViewerBackend::xxx() const` 两侧同时报错
    QString fileState() const { return m_fileState; }
    QString fileName() const { return m_fileName; }
    qint64 fileBytes() const { return m_fileBytes; }
    qint64 fileTotal() const { return m_fileTotal; }
    int filePercent() const { return m_filePercent; }
    QString fileError() const { return m_fileError; }
    QString fileTarget() const { return m_fileTarget; }

    // RTC 读数
    bool rtcReady() const { return m_rtcReady; }
    QString rtcState() const { return m_rtcState; }

    /** 切到某台设备 → 向云端订阅它的画面。 */
    void setCurrentUid(const QString &uid);

    Q_INVOKABLE void requestDevices();
    Q_INVOKABLE void sendAction(const QString &action, const QJsonObject &params = QJsonObject());
    Q_INVOKABLE void sendPing();
    Q_INVOKABLE void sendPointer(const QString &kind, double nx, double ny);
    Q_INVOKABLE void sendType(const QString &text);

    /**
     * 推一个本机文件给当前选中的教室机。
     *
     * 链路：file_push（开会话）→ 一片一片 file_chunk（data 走 base64）→ file_done（收口并校总字节）。
     * **一片一片等回执再发下一片**（回执 done 才继续），不是一口气全发出去 ——
     * 被控端的 file_chunk 只认"下一片"的 seq，乱序直接拒；而且 base64 后每条近 90KB，
     * 攒一堆同时发会把自己和云端都淹了。
     * @param urlText 文件的 file:// URL（界面上 FileDialog 给的）
     */
    /**
     * 弹本机"选文件"框，返回选中文件的 file:// URL（没选则返回空串）。
     *
     * 走 C++ 原生 QFileDialog 而不是 QML 的 QtQuick.Dialogs：后者是一套独立的 QML 插件，
     * 部署时漏了就是"exe 起不来 / Main.qml 整个挂掉"，排查起来比多链接一个库贵得多。
     */
    Q_INVOKABLE QString pickFile();
    Q_INVOKABLE void pushFile(const QString &urlText);
    /** 中断当前推送：让被控端把半截文件关掉、清会话（不留下半个坏文件在那台机器上）。 */
    Q_INVOKABLE void cancelPush();

    // ── QWebChannel 回调（JS → C++，收流页里的 __qt.* 就是调这四个）──
    /** JS 抽到的一帧 JPEG（base64，可能带 data: 前缀）；复用 frame 通道交给 QML。 */
    Q_INVOKABLE void setRtcFrame(const QString &b64);
    /** 远端 answer 的 SDP 字符串（正常流里管理端不产出，仅日志记录）。 */
    Q_INVOKABLE void rtcGotAnswer(const QString &sdp);
    /** 远端 ICE candidate 的 JSON 字符串。 */
    Q_INVOKABLE void rtcGotIce(const QString &candidateJson);
    /** JS 侧的诊断输出（setOffer/ontrack/ICE 全程）。 */
    Q_INVOKABLE void rtcDiag(const QString &s);
    /** JS 侧 ontrack 触发（真正拿到远端视频轨）。 */
    Q_INVOKABLE void rtcGotTrack();

signals:
    void connectedChanged();
    void authedChanged();
    void statusTextChanged();
    void devicesChanged();
    void currentUidChanged();
    void frameChanged();
    void statsChanged();
    void cloudUrlChanged();
    /** 文件推送进度：状态/文件名/已推字节/百分比/失败原因/落盘路径，全走这一个信号。 */
    void fileProgressChanged();
    /** WebRTC 收流链路状态变化（ready / negotiating / failed / idle）。 */
    void rtcStateChanged();
    /** 帧没画成（解码失败等）——界面可以往屏幕上说明一句，别让画面无声无息不动。 */
    void frameDropped(const QString &reason);
    /** 鉴权被拒：界面应停止重连并把原因显示出来。 */
    void authFailed(const QString &reason);
    /**
     * 指令结果。state: "sent"（云端已写进连接）/ "executed"（机器真做了）/ 其它=没发成。
     * result: "done" / "failed"（仅 executed 时有意义）。data: 动作附加数据（可选）。
     */
    void resultReceived(const QString &uid, const QString &action, const QString &state,
                        const QString &result, const QString &error, const QString &detail,
                        const QJsonObject &data);

private:
    void setStatus(const QString &s, bool warn);
    void connectToCloud();
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString &text);
    void onBinaryMessage(const QByteArray &buf);
    void refreshDevices(const QJsonArray &arr);
    void applyFrameBytes(const QByteArray &jpeg, const QJsonObject &header);
    void tickFps();
    void sendEnvelope(const QString &type, const QJsonObject &payload);
    void sendNextChunk();
    void setFileFail(const QString &why);
    void clearFilePush();

    // ── WebRTC 收流内部 ──
    void initRtcView();          // 懒建 QWebEngineView + 注入 answer 侧 JS
    void setRtcState(const QString &s);

    QWebSocket *m_ws = nullptr;
    QTimer *m_fpsTimer = nullptr;

    QString m_url;
    QString m_token;

    QString m_currentUid;
    bool m_connected = false;
    bool m_authed = false;
    bool m_authFailed = false;      // 鉴权被拒后别再一遍遍重连
    QString m_authFailReason;

    QString m_statusText;
    bool m_statusWarn = false;

    // ── 文件推送现场 ──
    QString m_fileState = QStringLiteral("idle");
    QString m_fileName;
    qint64 m_fileBytes = 0;
    qint64 m_fileTotal = 0;
    int m_filePercent = 0;
    QString m_fileError;
    QString m_fileTarget;      // 推完后被控端的落盘路径
    QString m_pushPath;        // 本机文件路径（便于报错时把路径直接说出来）
    QFile m_pushFile;          // 本机文件句柄（推完/取消就关，不留着）
    qint64 m_pushNext = 0;     // 下一片的 seq（被控端只认严格递增）

    // ── WebRTC 收流现场 ──
    QWebEngineView *m_rtcView = nullptr;
    bool m_rtcReady = false;
    QString m_rtcState = QStringLiteral("idle");

    QJsonArray m_devices;
    QImage m_frame;
    int m_frameCount = 0;
    int m_framesSinceCheck = 0;
    int m_lastFrameBytes = 0;
    double m_fps = 0.0;
    qint64 m_lastFpsCheckMs = 0;
    qint64 m_pingSentMs = 0;
};
