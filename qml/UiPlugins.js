// 管理端界面声明清单（U3）—— 一条声明 = 一个界面单元。
//
// 这就是「插件化」的入口：以前加一个面板要动 Main.qml（3858 行里改 openOnly 的
// if-else、加一个 Popup 声明、再加进命令面板），现在**只加一条声明**。
//
// 形状与门控口径见 ../../Stelarith-shared-qml/ui/UiContract.js（纯函数，node 可回归）。
// entry 相对 **viewer 的 qml 根**解析（宿主 entryBase = Qt.resolvedUrl(".") 于 Main.qml）。
//
// ⚠️ 迁移来历（2026-10-16 U3）：
//   · 8 条 entry 本来就在 qml/ 根下（独立文件，自带 theme 属性）
//   · 6 条 entry 在 qml/panels/ 下 —— 它们**原来内联在 Main.qml 里**（Popup 块），
//     U3 才抽成文件；抽出时把 `root.` 改成了宿主注入的 ctx.window，
//     把靠组件作用域隐式解析的裸 `th` 显式化（详见各文件头部注释）。
.pragma library

// 面板 id 与原 openOnly(which) 的取值**逐一对应**，故 openOnly 可退化成一行转发。
function manifest() {
    return [
        // ── 单机视图 / 观测类 ────────────────────────────────────────────
        { id: "log",       title: "日志",       icon: "file-text",  order: 10,
          surfaces: ["panel"], entry: "LogDialog.qml",
          keys: ["log", "rizhi", "rz"], tip: "这台设备的运行日志" },
        { id: "term",      title: "终端",       icon: "terminal",   order: 20,
          surfaces: ["panel"], entry: "TerminalDialog.qml",
          keys: ["terminal", "cmd", "zhongduan"], tip: "命令行窗口（要管理员身份）" },
        { id: "media",     title: "媒体",       icon: "music",      order: 30,
          surfaces: ["panel"], entry: "MediaDialog.qml",
          keys: ["media", "meiti"], tip: "设备上的音频 / 点歌" },
        { id: "sched",     title: "定时任务",   icon: "clock",      order: 40,
          surfaces: ["panel"], entry: "ScheduleDialog.qml",
          keys: ["schedule", "dingshi", "ds"], tip: "排一个定时关机 / 重启" },
        { id: "alarm",     title: "闹钟",       icon: "clock",      order: 41,
          surfaces: ["panel"], entry: "AlarmDialog.qml",
          keys: ["alarm", "naozhong", "nz"], tip: "定时响铃 / 播报 / 锁屏（2026-10-26 P1）" },
        { id: "camera",    title: "监控",       icon: "camera",     order: 50,
          surfaces: ["panel"], entry: "panels/CameraCenterDialog.qml",
          keys: ["camera", "shexiangtou", "sxt"], tip: "摄像头监控 + 绑定" },

        // ── 下发 / 交互类 ───────────────────────────────────────────────
        { id: "notify",    title: "通知",       icon: "bell",       order: 60,
          surfaces: ["panel"], entry: "NotifyDialog.qml",
          keys: ["notify", "tongzhi", "tz"], tip: "向所选设备广播通知" },
        { id: "file",      title: "分发文件",   icon: "upload",     order: 70,
          surfaces: ["panel"], entry: "FilePushDialog.qml",
          keys: ["file", "wenjian", "wj"], tip: "向所选设备分发文件" },
        { id: "software",  title: "软件",       icon: "grid",       order: 80,
          surfaces: ["panel"], entry: "SoftwareDialog.qml",
          keys: ["software", "ruanjian", "rj"], tip: "设备已安装应用与运行中进程" },
        { id: "speech",    title: "语音对讲",   icon: "mic",        order: 90,
          surfaces: ["panel"], entry: "panels/SpeechDialog.qml",
          keys: ["voice", "yuyin", "yy"], tip: "按住说话，全班可听" },
        { id: "interact",  title: "互动审批",   icon: "check-check", order: 100,
          surfaces: ["panel"], entry: "InteractDialog.qml",
          keys: ["interact", "shenpi", "sp", "diange", "zhufu"],
          tip: "审核学生点歌与祝福，右侧预览设备端显示效果" },

        // ── 管控 / 多机类 ───────────────────────────────────────────────
        { id: "control",   title: "管控",       icon: "sliders",    order: 110,
          surfaces: ["panel"], entry: "panels/ControlDialog.qml",
          keys: ["control", "guanKong", "gk", "broadcast", "guangbo", "gb", "exam", "kaoshi"],
          tip: "广播 / 考试模式 / 语音 / 屏幕广播" },
        { id: "multiband", title: "多班墙",     icon: "layout-grid", order: 120,
          surfaces: ["panel"], entry: "panels/MultiThumbDialog.qml",
          keys: ["multiband", "duoban", "db"], tip: "多班缩略图墙（最多 9 台在线）" },

        // ── 系统信息类 ─────────────────────────────────────────────────
        { id: "changelog", title: "更新日志",   icon: "history",    order: 130,
          surfaces: ["panel"], entry: "panels/ChangelogDialog.qml",
          keys: ["changelog", "gengxin", "gx"], tip: "本版改了什么" },
        { id: "help",      title: "帮助",       icon: "help-circle", order: 140,
          surfaces: ["panel"], entry: "panels/HelpDialog.qml",
          keys: ["help", "bangzhu", "bz"], tip: "快捷键与常见问题" }
    ];
}
