// 星集控被控端 · 通用进程工具（2026-10-22 · AGENTS.md §10 第二刀）
//
// 从 main.cpp 搬出。它不属于任何业务域（录像、OTA、远控、终端都在用），
// 所以单独放 src/core/，好过塞进 media 模块让录依赖反常。

#pragma once

class QProcess;

namespace ProcessUtil {

/** 同步等到进程退出，但等待期间**不卡主线程**：心跳照发、指令照收、定时器照跑。
 *  重入安全：等待期间云端再派同一动作时各用独立 QProcess / 独立临时文件名（毫秒戳），
 *  无共享态。超时返回 false（进程可能还活着，由调用方决定 kill 还是放过）。 */
bool waitFinished(QProcess &p, int timeoutMs);

} // namespace ProcessUtil
