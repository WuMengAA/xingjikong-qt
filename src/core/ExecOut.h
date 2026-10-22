// 星集控被控端 · 指令执行结果（跨 TU 共享）
// 2026-10-23 · AGENTS.md §10 第 3 刀：从 main.cpp 提到共享头文件。
//
// ⚠️ 为什么必须提出来：它原定义在 main.cpp **匿名 namespace 内部**，
//    匿名域符号是内部链接，**跨不了 TU** —— 相机分支一抽到 src/media/
//    CameraActions.cpp，那边就拿不到 ExecOut 类型。
//    （第二刀的 LNK2019 是同一个坑，见 main.cpp 251 行上方注记。）

#pragma once

#include <QString>
#include <QJsonObject>

// 2026-10-03 批次 1：成功/失败统一成 "done"/"failed"，动作的语义化返回值放进 data
// （协议规范第十节：加**可选字段**，老端忽略不报错）—— 这样三端"绿=done"的判据不用动。
struct ExecOut { QString result; QString error; QJsonObject data; };
