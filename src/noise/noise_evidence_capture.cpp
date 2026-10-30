#include "noise_evidence_capture.h"

#include <QDateTime>

NoiseEvidenceCapture::NoiseEvidenceCapture(IFrameSnapshot* desktop, IFrameSnapshot* camera)
    : m_desktop(desktop), m_camera(camera)
{
}

NoiseEvidence NoiseEvidenceCapture::capture()
{
    NoiseEvidence ev;
    ev.capturedAt = QDateTime::currentDateTime();

    // 桌面：先抓。它是"发生了什么"的主要证据，摄像头是次要补充。
    if (m_desktop) {
        QString path, why;
        if (m_desktop->capture(&path, &why)) {
            ev.desktopPath = path;
        } else {
            ev.desktopError = why.isEmpty() ? QStringLiteral("桌面抓帧失败（原因未提供）") : why;
        }
    } else {
        ev.desktopError = QStringLiteral("未配置桌面抓帧源");
    }

    // 摄像头：失败不阻断 —— 一张都没有才算"没抓到"（由 any() 表达）。
    if (m_camera) {
        QString path, why;
        if (m_camera->capture(&path, &why)) {
            ev.cameraPath = path;
        } else {
            ev.cameraError = why.isEmpty() ? QStringLiteral("摄像头抓帧失败（原因未提供）") : why;
        }
    } else {
        // 本机没摄像头是常态（讲台机可能只接 HDMI），不该当成"故障"上报到管理端；
        // 但也不能留空让人误以为是"抓了但没抓到"，所以给一句明确的"无此设备"。
        ev.cameraError = QStringLiteral("本机未配置摄像头");
    }

    return ev;
}
