// 远控 VNC 链路（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）
//
// // 搬出理由：远控是本文件里最独立的一条链 —— 建立连接、鉴权、帧注入、断开，
// // 全程只碰 m_vnc* / m_vncState 这组成员，不参与设备表、不参与下发指令。

#include "viewerbackend.h"
#include "viewerbackend_shared.h"
#include <QWebSocket>
#include <QJsonArray>
#include "vnc_client.h"
#include "vnc_image_provider.h"
#include "H264ImageProvider.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QUrl>

void ViewerBackend::setVncImageProvider(VncImageProvider *p)
{
    m_vncProvider = p;
}
void ViewerBackend::setH264ImageProvider(H264ImageProvider *p)
{
    m_h264Provider = p;
}
void ViewerBackend::setVncState(const QString &s)
{
    if (m_vncState == s) return;
    m_vncState = s;
    emit vncStateChanged();
}
// ─────────────────────────────────────────────────────────────────────────────
// 被控端访问密码鉴权（2026-10-26 新增）
//   接管一台启用了访问密码的设备前，必须先向云端 vnc-auth 鉴权；云端在本连接记
//   ws.__vncAuthed[uid]，之后 remote_control_start / vnc-data 双向中继才放行。
//   未设密码的设备行为与以往完全一致（不走鉴权，直接接管）。
// ─────────────────────────────────────────────────────────────────────────────
void ViewerBackend::beginTakeover(const QString &uid)
{
    if (uid.isEmpty()) return;
    // 选中即接管（需求#7，2026-10-11）：进了远控界面、选好机器，就不需要再点开关。
    // 教师角色不发（权限不足，发了也是被拒）；设备没上报 remote_control_start 能力就不发
    // （老版本被控端，别让回执流水里全是失败）。
    if (role() != QStringLiteral("admin")
        || !m_capActions.contains(QStringLiteral("remote_control_start")))
        return;
    QJsonObject rc;
    // 帧率随单下发（2026-10-18 晚修复）：原先 rc 是空对象，被控端取默认 20
    // 且 VncServer 抓屏节奏写死 100ms=10fps——客户端要多少都只有 10fps。
    // 现在一路带到 VncServer::start(0, fps)，抓屏间隔按 1000/fps 算（clamp 33ms）。
    rc.insert(QStringLiteral("fps"), 24);
    sendAction(QStringLiteral("remote_control_start"), rc);
    m_remoteControlling = true;
    m_remoteControlUid = uid;
    m_pendingVncUid = uid;   // 等回执/兜底再拉 VNC
    logf("[viewer] 选中即远控：已对 %s 下发 remote_control_start", uid.toUtf8().constData());
    // ⚠️ 2026-10-24 修复（VNC 从未通过的根因）：原来「发指令」和「startVnc」在同一毫秒执行——
    // 被控端要等指令到达后才起抓屏线程、绑 127.0.0.1:5900、接桥。管理端立刻连过去等不到
    // "RFB 003.008"，rfbInitClient 直接失败（日志"连接或握手未完成"），整场远控退回 JPEG 轮询。
    // 现在改为等被控端回执确认 VNC 已起再拉（见 instruction-result 分支），并留 2s 兜底定时器。
    QTimer::singleShot(2000, this, [this, uid]() {
        if (m_pendingVncUid != uid || m_vncActive) return;
        logf("[viewer] 2s 兜底：仍未见回执，直接拉起 VNC（uid=%s）", uid.toUtf8().constData());
        startVnc(uid);
    });
}
void ViewerBackend::requestVncAccessAuth(const QString &uid)
{
    if (uid.isEmpty()) return;
    // 本会话已记过这台设备的访问密码 → 直接重试，不弹窗（避免每次重连都问一次）。
    const QString cached = m_vncAccessPassword.value(uid);
    if (!cached.isEmpty()) {
        sendVncAuth(uid, cached, /*autoSubmitted=*/true);
    } else {
        emit vncAuthRequired(uid);   // QML 弹密码框
    }
}
void ViewerBackend::sendVncAuth(const QString &uid, const QString &password, bool autoSubmitted)
{
    if (uid.isEmpty()) return;
    QJsonObject p;
    p.insert(QStringLiteral("uid"), uid);
    p.insert(QStringLiteral("password"), password);
    sendEnvelope(QStringLiteral("vnc-auth"), p);
    m_vncAuthAuto = autoSubmitted;
    m_vncAuthUid = uid;
    m_vncAuthPasswordSubmitted = password;
    logf("[viewer] 已发 vnc-auth（uid=%s，%s）", uid.toUtf8().constData(),
         autoSubmitted ? "自动重试" : "用户提交");
}
void ViewerBackend::submitVncPassword(const QString &uid, const QString &password)
{
    // 由 QML 密码框调用：用户手动提交，失败要弹错误提示（m_vncAuthAuto=false）。
    m_vncAuthAuto = false;
    sendVncAuth(uid, password, /*autoSubmitted=*/false);
}
void ViewerBackend::startVnc(const QString &uid)
{
    if (uid.isEmpty()) return;
    // 幂等：同一台已经在连/已连上，就不要再重启会话（重启必然打断正在进行的握手）。
    if (m_vncActive && m_vncUid == uid) {
        logf("[viewer] VNC 会话已在 %s 上，忽略重复拉起", uid.toUtf8().constData());
        return;
    }
    m_vncUid = uid;
    if (!m_vnc) {
        m_vnc = new VncClient(this);
        connect(m_vnc, &VncClient::frameReady, this, &ViewerBackend::onVncFrame);
        // 出站：libvncclient 想发的 RFB 字节（协议版本/认证/更新请求/键鼠）→ 主 WS
        connect(m_vnc, &VncClient::vncOut, this, [this](const QByteArray &data) {
            if (data.isEmpty() || !m_ws) return;
            const qint64 n = m_ws->sendBinaryMessage(viewerMakeVncFrame(m_currentUid, data));
            // 诊断（2026-10-18 深夜排障）：down 方向此前完全黑盒 —— 打头几帧看字节去哪了
            static int dbgSent = 0;
            if (dbgSent < 4)
                logf("[viewer-vnc] down 已发 #%d：%d 字节（uid=%s）", ++dbgSent,
                     int(n), m_currentUid.toUtf8().constData());
        });
        connect(m_vnc, &VncClient::stateChanged, this, [this](int st) {
            switch (st) {
            case VncClient::Connecting: setVncState(QStringLiteral("连接中")); break;
            case VncClient::Connected:  setVncState(QStringLiteral("已连接")); break;
            case VncClient::Error:      setVncState(QStringLiteral("连接失败")); break;
            default:                    setVncState(QStringLiteral("已断开")); break;
            }
            emit vncStateChanged();   // vncActive 与状态同信号，一起刷新
        });
    }
    // VNC 密码：环境变量 STE_VNC_PASSWORD 优先（被控端 libvncserver 侧的密码）。
    if (m_vncPassword.isEmpty())
        m_vncPassword = qEnvironmentVariable("STE_VNC_PASSWORD", QString()).trimmed();

    m_vncActive = true;
    m_vncBtnMask = 0;
    if (m_frameSource != QStringLiteral("vnc")) {
        m_frameSource = QStringLiteral("vnc");
        emit frameSourceChanged();
    }
    setVncState(QStringLiteral("连接中"));
    emit vncStateChanged();
    logf("[viewer] 拉起 VNC 会话：uid=%s（RFB 字节走主 WS 的 vnc-data 帧）",
         uid.toUtf8().constData());
    m_vnc->start(m_vncPassword);
    m_pendingVncUid.clear();   // 已拉起，兜底定时器不再需要动手

    // ⚠️ 2026-10-18 深夜实证修复：这里**不能退订**！云端 vncDataToViewer 只投给
    //    「订阅中」的管理端（谁订阅投谁），退订之后被控端的 up 帧会被云端静默丢弃
    //    —— RFB 握手第一帧（ProtocolVersion 12B）就到不了 viewer，画面永远黑。
    //    （此前退订的动机是"两套抓屏抢桌面"，实际上 agent 侧抓屏不受订阅影响，
    //     真正的互斥靠 m_vncActive 让 applyFrameBytes 让行，见 onBinaryMessage。）
    //    stopVnc 里的重新订阅保留（幂等，双保险）。
}
void ViewerBackend::stopVnc()
{
    if (!m_vncActive && !m_vnc) return;
    m_vncActive = false;
    m_vncUid.clear();
    m_pendingVncUid.clear();
    m_vncBtnMask = 0;
    if (m_vnc) m_vnc->stop();
    if (m_vncProvider) m_vncProvider->clear();
    if (m_frameSource == QStringLiteral("vnc")) {
        m_frameSource = QStringLiteral("none");
        emit frameSourceChanged();
    }
    setVncState(QStringLiteral("已断开"));
    logf("[viewer] VNC 会话已停止");
    // 恢复常规画面通道：重新订阅当前设备。
    if (!m_currentUid.isEmpty() && m_screenActive) {
        QJsonObject sp;
        sp.insert(QStringLiteral("uid"), m_currentUid);
        sendEnvelope(QStringLiteral("subscribe"), sp);
    }
}
void ViewerBackend::onVncData(const QByteArray &data)
{
    if (data.isEmpty()) return;
    // 2026-10-24 诊断：vnc-data 是否真到达管理端（此前只能看被控端"已发"，到达与否是黑盒）
    static int dbgVncIn = 0;
    if (dbgVncIn < 8)
        logf("[viewer] vnc-data 到达 #%d：%d 字节", ++dbgVncIn, data.size());
    if (!m_vnc) {
        logf("[viewer] WARN 收到 vnc-data 但 VNC 客户端未建立（丢弃 %d 字节）", data.size());
        return;
    }
    m_vnc->feedIncoming(data);
}
void ViewerBackend::onVncFrame(const QImage &img)
{
    if (img.isNull() || !m_vncActive) return;
    if (m_frameSource != QStringLiteral("vnc")) {
        m_frameSource = QStringLiteral("vnc");
        emit frameSourceChanged();
    }
    m_frameCount++;
    m_frame = img;
    m_frameUid = m_currentUid;              // libvncclient 帧不带 uid，按当前这台记账
    m_lastFrameBytes = (int)img.sizeInBytes();
    const quint64 fp = viewerFrameFingerprint(img);
    m_sameFrameStreak = (fp == m_frameHash) ? (m_sameFrameStreak + 1) : 0;
    m_frameHash = fp;
    setStatic(m_sameFrameStreak >= kStaticStreak);
    if (m_vncProvider) m_vncProvider->update(img);   // image://vnc/live
    if (!m_screenStatic) emit frameChanged();
    emit vncFrameReady(img);
    emit statsChanged();
    if (m_frameCount % 30 == 1) {
        logf("[viewer] 远控画面在动[VNC]：第 %d 帧（%dx%d）",
             m_frameCount, img.width(), img.height());
    }
    // 取证开关（2026-10-24）：设 STE_VNC_CAPTURE=<png 路径> 时，每 60 帧把**实际解出并送屏**
    // 的画面落盘一张。排障时"到底有没有画面"不该靠猜或靠人盯着看 —— 直接看图。
    static const QString capPath = qEnvironmentVariable("STE_VNC_CAPTURE", QString()).trimmed();
    if (!capPath.isEmpty() && m_frameCount % 60 == 1) {
        if (img.save(capPath, "PNG"))
            logf("[viewer] 取证帧已落盘：%s（第 %d 帧 %dx%d）",
                 capPath.toUtf8().constData(), m_frameCount, img.width(), img.height());
    }
}
