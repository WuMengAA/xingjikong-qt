// action_caps.h
// ───────────────────────────────────────────────────────────────────────────
// 本端真实实现的指令全集，**必须与 executeAction 的分派逐条对齐**。
// （2026-10-06 契合度改造：以前 register 只报 screen/input/power 三个能力组、
//  这条表一个都不报，于是云端和管端无从知道这台机器能不能拍照/定时/推文件，
//  管端只能把按钮对着所有机器开放，点下去才收到一句「未知指令」。）
// 放成常量数组而不是散在各个 if 里，是为了让"能做什么"只有一处真值来源：
// 以后 executeAction 加分支，这张表也得跟着加，否则设备会自称会做却做不了（或反之）。
//
// 2026-11-03 从 main.cpp 抽出：main.cpp 只做装配，能力名册属动作域知识
// （AGENTS.md §main.cpp 禁令：业务判断进 src/<domain>/）。
#pragma once
#include <cstddef>

namespace ActionCaps {
    extern const char* const kActionNames[];
    extern const int kActionCount;
}
