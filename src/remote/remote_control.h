#pragma once
#include <QString>

// 远控会话生命周期管理（2026-10-30 main 拆分 #8）。
// 从 main.cpp 迁出：stopRemoteControlSession / reapRemoteControlIfStale /
// saveVncAccessLocally（含模块私有 vncAccessPath）。
// 状态全局（g_remoteControl / g_currentFps / g_vncSessionStartMs / g_lastVncDownMs）
// 仍驻 main（文件作用域外部链接），本模块 extern 复用（同 g_uid / agentEnvPath 模式）。
// ⚠️ 函数名与原 main static 一致 ⇒ 调用点不改名。

// 远控会话收口：remote_control_stop 动作与泄漏看门狗共用同一段清理
// （置位、解冻灵动岛、停桥、停服、恢复帧率）。幂等：无会话时早退。
void stopRemoteControlSession(const QString &reason);

// 远控会话泄漏看门狗：管理端 VncClient 每 5s 经 RFB 发 1x1 增量探活，
// 超过 25s（保活周期 5 倍）没收过任何 down 字节 ⇒ 判管理端已离开，自动收口。
// 搭心跳车执行，零新定时器。
void reapRemoteControlIfStale();

// 被控端访问密码本地持久化（云端统一设置/修改，明文经已鉴权设备 WS 下发到本机）。
// enabled=false → 清空文件（等于关闭）；enabled=true → 写明文。
void saveVncAccessLocally(bool enabled, const QString &password);
