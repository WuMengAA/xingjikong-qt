// action_caps.cpp
// 见 action_caps.h。本端 action 名册唯一真源（与 executeAction 分支对齐）。
#include "action_caps.h"

namespace ActionCaps {

const char* const kActionNames[] = {
    "camera_bind", "camera_list", "camera_record_start", "camera_record_stop", "camera_snapshot", "camera_test",
    "cancel_schedule", "file_chunk", "file_done", "file_push",
    "input", "launch_app", "list_schedules", "list_shortcut_candidates",
    "lock", "log_tail", "media_delete", "media_list",
    "media_session_start", "media_session_stop", "notify", "open_app",
    "ping", "process_list", "process_stop", "reboot",
    "remote_control_start", "remote_control_stop", "schedule_reboot", "schedule_shutdown",
    "screenshot", "self_update", "set_volume", "shutdown",
    // 远程终端三条（2026-10-06）。必须报上去：管理端按 caps 决定「终端」按钮亮不亮，
    // 不报 = 管理端永远置灰，功能做完了看得见摸不着。
    // 顺序照旧按字母表末段排，方便以后和别的动作一起校对。
    "terminal_close", "terminal_input", "terminal_open",
    // 考试模式（2026-10-06，设计文档 3.7 第一版）：全屏拦截 + 黑名单轮询 + 计时。
    "exam_mode", "exam_mode_stop",
    // 噪音 / 自习（2026-10-13）：noise_monitor_* 是底层采集开关（只测不录）；
    // study_mode_* 是上层模式（周期测噪 + 连续超阈自动录一段摄像头并上传）。
    // ⚠️ noise_monitor_* 之前一直漏登记 —— 设备实际能做却自称不会，本轮补上。
    "noise_monitor_start", "noise_monitor_stop",
    "study_mode_start", "study_mode_stop",
    // 定时自动化规则引擎（2026-10-27 Phase 3）：管理端经此下发 / 拉取规则。
    // 仅做"转发到 AutomationEngine"的薄壳，规则执行仍走 executeAction 既有词表（零新攻击面）。
    "automation_list", "automation_upsert", "automation_remove",
    "automation_toggle", "automation_run",
    // 倒计时固定目标（2026-11-03）：配置常驻倒计时面盯死的时刻；
    // 管理端按 caps 决定「固定倒计时」入口显不显。
    "set_countdown_target",
};

const int kActionCount = (int)(sizeof(kActionNames) / sizeof(kActionNames[0]));

} // namespace ActionCaps
