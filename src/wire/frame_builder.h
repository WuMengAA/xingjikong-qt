#pragma once

#include <QByteArray>
#include <QString>

/* 协议 v1 信封封装（2026-10-18 统一方案 A · main 拆分 #12）
 *
 * 三个打包函数与 kProtocolVersion、上行序号计数器一起从 main.cpp 匿名域迁出
 * （main 拆分 #12）。依赖：g_uid（main.cpp 文件作用域外部链接，extern 复用，
 * 同 upload 模块模式）。
 *
 * 文本：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
 * 帧  ：[1B 版本=1][2B 大端 headerLen][header JSON][JPEG/RFB/OPUS]
 *
 * 为什么要这套：以前是"想到一条加一条"的扁平 {type,...}，没版本号、帧没序号，
 * 加任何新机制都要在两端各猜一遍。现在加功能 = 加一个带类型的消息，骨架不动。
 */

QByteArray makeFrameBytes(const QString &uid, int seq, const QByteArray &jpeg);
QByteArray makeVncFrameBytes(const QByteArray &rfb);
QByteArray makeAudioFrameBytes(const QByteArray &payload, bool opus);
// 远控 H264 上行帧（A 档第②步）：被控端 MF 编码出的 annex-B NALU → 云端中继到管理端解码。
QByteArray makeH264FrameBytes(const QByteArray &nalu);
