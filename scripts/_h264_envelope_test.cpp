// A 档第⑤步 · 本机控制面证伪：用真实 makeH264FrameBytes 产出 h264-data 信封字节，
// 以 base64 打到 stdout，交由 Node 侧 parseAnyFrame 验证解析（跨组件链路证伪）。
// 合成 NALU 占位（本地 MF 0 产出，真机 NALU 同格式），只验信封/中继这一跳。
#include "wire/frame_builder.h"
#include <QCoreApplication>

// frame_builder.cpp 引用 extern QString g_uid; 这里提供定义。
QString g_uid = QStringLiteral("test-uid-001");

int main(int argc, char **argv)
{
    QCoreApplication a(argc, argv);
    // 合成 annex-B NALU 占位：SPS/PPS/IDR 起始码 + 伪字节（真机同格式，仅内容不同）。
    QByteArray syntheticNalu;
    // ⚠️ 不能用 append(const char*)（遇 \x00 截断）；逐字节构造 annex-B NALU 占位。
    const char sps[] = {0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f};
    const char pps[] = {0x00,0x00,0x00,0x01,0x68,0xce,0x3c,0x80};
    const char idr[] = {0x00,0x00,0x00,0x01,0x65};
    syntheticNalu.append(sps, sizeof(sps));
    syntheticNalu.append(pps, sizeof(pps));
    syntheticNalu.append(idr, sizeof(idr));
    syntheticNalu.append(QByteArray(40, char(0x88)));

    QByteArray env = makeH264FrameBytes(syntheticNalu);
    // 以 base64 输出，便于 Node 侧精确还原字节（避免 stdout 二进制截断）。
    QByteArray b64 = env.toBase64();
    printf("%s\n", b64.constData());
    return 0;
}
