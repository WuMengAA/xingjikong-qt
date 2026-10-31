#pragma once
// 远控 H264 画面 QML 图像提供器（A 档第②/④步，2026-10-31）。
// 渲染管线：被控端 MF 编码 NALU → 主 WS 的 h264-data 帧 → 本类喂 MfxH264Decoder
//           → 解出 QImage → 交给 QML 的 Image { source: "image://h264/live" }。
// 与 VncImageProvider 同约束：绝对禁止 QWidget 画图。
// 线程安全：h264-data 在 WS 回调线程到达，requestImage 在 GUI 线程，靠 m_mutex 保护。
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

class MfxH264Decoder;   // 前向声明，避免头里引入 MF/Windows 头（§10.4）

class H264ImageProvider : public QQuickImageProvider
{
public:
    H264ImageProvider();
    ~H264ImageProvider() override;

    // QML 侧 Image { source: "image://h264/live?t=<tick>" } 取图时调用
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // 主线程/WS 线程调用：推入一个 H264 访问单元（Annex-B NALU），解出帧则更新缓存
    void feedNal(const QByteArray &nal);
    void clear();

    // 解码器初始化（首帧到达时按需调用，按帧尺寸初始化一次）
    bool ensureDecoder(int w, int h);

private:
    QMutex m_mutex;
    QImage m_img;
    MfxH264Decoder *m_dec = nullptr;   // 拥有，析构释放
    int m_decW = 0, m_decH = 0;
    bool m_decReady = false;
};
