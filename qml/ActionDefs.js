// 管理端设备动作 / 命令 的**单一真源**（U4）—— 一条声明 = 一个可执行单元。
//
// ── 病根（U4 要治的）────────────────────────────────────────────────────
// 同一批动作在 Main.qml 里有**三份互不相干的清单**：
//   ① 概览页快捷行（Repeater model，7 条，走 quickAction(a) 的 if-else 链）
//   ② 右栏按钮区（4 组 Repeater，12 条，走 runAction({l,a,f,p})）
//   ③ 命令面板（pal.cmds 字面数组，38 条，每条自带 run 闭包）
// 改一处忘另一处 = 静默不一致（历史上真发生过：命令面板「定时」写错键，
// 能力门控静默失效 —— 见 Main.qml capByLabel 的 2026-10-11 注释）。
// 现在：**清单只有这一份**，三处都从注册表派生。
//
// ── 形状 ──────────────────────────────────────────────────────────────
// 契约见 ../../Stelarith-shared-qml/ui/UiContract.js（纯函数，node 可回归）。
//   kind = send     发一条指令（action）
//          panel    开一个浮层（panel = UiPlugins.js 里的面板 id）
//          special  多步/条件分支 ⇒ 走宿主的 SPECIAL 表（键 = 本文件的 id）
//          app      本地应用命令（app 子类型 + payload）
// 执行**只在宿主一处**（Main.qml 的 runActionById）—— 右栏按钮与命令面板
// 因此走的是同一条路，不再各写一版。
//
// ── 文案为什么有三个 ────────────────────────────────────────────────────
// 同一动作在三处的叫法本来就不一样：按钮要短（"锁屏"）、命令面板要能自解释
// （"锁屏教室机"）、快捷行是口语（"发通知"）。**统一的是清单，不是文案** ——
// 所以 title / palName / quickTitle 各留一个，空则回落到 title。
.pragma library

function manifest() {
    return [
        // ══ 教室机动作（命令面板 38 条的主干）══════════════════════════════
        { id: "lock", palOrder: 1, title: "锁屏", palName: "锁屏教室机", quickTitle: "锁屏",
          kind: "send", action: "lock", role: "admin", danger: true, quickGuard: "lock",
          group: "电源", wide: true, heavy: true, quick: true, quickOrder: 1, order: 20,
          surfaces: ["action", "command"],
          keys: ["lock", "suo", "sjb"], tip: "教室机立刻锁屏（要管理员身份）" },

        { id: "reboot", palOrder: 2, title: "重启", palName: "重启教室机",
          kind: "send", action: "reboot", role: "admin", danger: true,
          group: "电源", wide: true, heavy: true, order: 21,
          surfaces: ["action", "command"],
          keys: ["reboot", "chongqi", "cq"], tip: "重启教室机（要管理员身份）" },

        { id: "shutdown", palOrder: 3, title: "关机", palName: "关机教室机",
          kind: "send", action: "shutdown", role: "admin", danger: true,
          group: "电源", wide: true, heavy: true, order: 22,
          surfaces: ["action", "command"],
          keys: ["shutdown", "guanji", "gj"], tip: "关掉教室机（要管理员身份）" },

        { id: "notify-send", palOrder: 4, title: "通知", palName: "给教室机发通知", quickTitle: "发通知",
          kind: "panel", panel: "notify", caps: ["notify"],
          group: "通知", wide: true, heavy: true, quick: true, quickOrder: 2, order: 10,
          surfaces: ["action", "command"],
          keys: ["notify", "tongzhi", "tz"], tip: "填标题和正文后下发（要填内容）" },

        { id: "screenshot", palOrder: 8, title: "截图", kind: "send", action: "screenshot",
          group: "看看", order: 30, surfaces: ["action", "command"],
          keys: ["screenshot", "jieku", "jk"], tip: "抓一帧画面回来" },

        // 软件：要**同时**拿到候选列表与进程列表才算真能开，故 caps 显式点两条。
        // ⚠️ id 不能与 UiPlugins.js 的面板 id 同名（两者合并进同一注册表，
        //    契约的 validate 会报 "id 重复"）。面板 id 是 "software"，故这里加后缀。
        { id: "software-list", palOrder: 9, title: "软件", palName: "看可打开的软件",
          kind: "special", caps: ["list_shortcut_candidates", "process_list"],
          group: "看看", order: 31, surfaces: ["action", "command"],
          keys: ["software", "ruanjian", "rj"], tip: "这台机器上有的程序 + 正在跑的" },

        { id: "open-camera", palOrder: 10, title: "监控", palName: "监控（摄像头）",
          kind: "panel", panel: "camera", caps: ["camera_list"],
          group: "看看", order: 32, surfaces: ["action", "command"],
          keys: ["camera", "shexiangtou", "sxt"], tip: "这台机器上有几个摄像头" },

        { id: "volume", palOrder: 11, title: "音量", palName: "调音量",
          kind: "special", caps: ["set_volume"],
          group: "看看", order: 33, surfaces: ["action", "command"],
          keys: ["volume", "yinliang", "yl"], tip: "打开音量浮层" },

        { id: "open-file", palOrder: 12, title: "文件", palName: "分发文件",
          kind: "panel", panel: "file", caps: ["file_push"],
          group: "看看", order: 34, surfaces: ["action", "command"],
          keys: ["file", "wenjian", "wj"], tip: "选一个文件推给教室机" },

        { id: "schedule", palOrder: 13, title: "定时任务", kind: "special", caps: ["list_schedules"],
          group: "任务", wide: true, order: 40, surfaces: ["action", "command"],
          keys: ["schedule", "dingshi", "ds"], tip: "排一个定时关机 / 重启" },

        // 终端是开发者排障通道（能跑任意命令），不占老师日常界面 ⇒ 无 group，
        // 只活在命令面板（Ctrl+K 搜"终端"）。caps 让 0.6.3 之前那批机器入口不亮。
        { id: "terminal", palOrder: 5, title: "终端", palName: "打开终端",
          kind: "panel", panel: "term", role: "admin", caps: ["terminal_open"],
          order: 60, surfaces: ["command"],
          keys: ["terminal", "cmd", "zhongduan"], tip: "命令行窗口（要管理员身份）" },

        { id: "remote-start", palOrder: 6, title: "开始远程控制", kind: "send",
          action: "remote_control_start", role: "admin",
          order: 61, surfaces: ["command"],
          keys: ["remote", "yuankong", "yk"], tip: "实时接管鼠标键盘（要管理员身份）" },

        { id: "remote-stop", palOrder: 7, title: "停止远程控制", kind: "send",
          action: "remote_control_stop", role: "admin",
          order: 62, surfaces: ["command"],
          keys: ["remote", "yk"], tip: "把上一路的远控收掉" },

        // ══ 右栏批量组（只在右栏；命令面板有各自更细的入口）══════════════
        { id: "control-open", title: "管控…", kind: "panel", panel: "control",
          group: "批量", wide: true, order: 50, surfaces: ["action"] },

        { id: "speech-open", title: "语音对讲…", kind: "panel", panel: "speech",
          group: "批量", wide: true, order: 51, surfaces: ["action"] },

        // ══ 概览快捷行 + 面板入口（page 字段 = 执行前切到第 N 页）══════════
        // ⚠️ panel 写 "broadcast" 而非 "control"：openOnly 里 broadcast 是**别名**，
        //    指向 control。保持写别名 ⇒ 哪天别名规则改了，两处一起改（单一真源）。
        { id: "broadcast-open", palOrder: 22, title: "广播", palName: "去广播给所有在线设备", quickTitle: "广播",
          kind: "panel", panel: "broadcast", page: 1,
          quick: true, quickOrder: 3, order: 63, surfaces: ["action", "command"],
          keys: ["broadcast", "guangbo", "gb"], tip: "广播要填内容，点开弹窗填再发" },

        { id: "multiband-open", palOrder: 29, title: "多班墙", palName: "刷新多班监控墙", quickTitle: "多班墙",
          kind: "panel", panel: "multiband", page: 1,
          quick: true, quickOrder: 7, order: 64, surfaces: ["action", "command"],
          keys: ["multiband", "duoban", "db"], tip: "重订阅多班缩略图（最多 9 台在线）" },

        { id: "open-interact", palOrder: 30, title: "互动审批", kind: "panel", panel: "interact",
          order: 65, surfaces: ["command"],
          keys: ["interact", "shenpi", "sp", "diange", "zhufu"],
          tip: "审学生点的歌 / 祝福；右侧直接看教室机上会显示什么" },

        // ══ 语音对讲（快捷行是**切换**语义；命令面板是分开的开/停两条）═════
        { id: "voice-toggle", title: "语音对讲", quickTitle: "语音对讲", kind: "special",
          quick: true, quickOrder: 4, order: 66, surfaces: ["action"] },

        { id: "voice-start", palOrder: 23, title: "开始语音对讲", kind: "special",
          order: 67, surfaces: ["command"],
          keys: ["voice", "yuyin", "yy"], tip: "全班可听，再点停" },

        { id: "voice-stop", palOrder: 24, title: "停止语音对讲", kind: "special",
          order: 68, surfaces: ["command"],
          keys: ["voice", "yuyin", "yy"], tip: "把语音广播收掉（静音）" },

        // ══ 屏幕广播（手机端没有：老师桌面推流是桌面专属）═════════════════
        { id: "screen-toggle", title: "屏幕广播", quickTitle: "屏幕广播", kind: "special",
          quick: true, quickOrder: 5, phone: false, order: 69, surfaces: ["action"] },

        { id: "screen-start", palOrder: 25, title: "开始屏幕广播", kind: "special",
          order: 70, surfaces: ["command"],
          keys: ["screen", "guangbo2", "gb2"], tip: "老师屏幕推给全部在线设备（再次点停）" },

        { id: "screen-stop", palOrder: 26, title: "停止屏幕广播", kind: "special",
          order: 71, surfaces: ["command"],
          keys: ["screen", "tingbo"], tip: "把屏幕广播收掉" },

        // ══ 考试模式 ═════════════════════════════════════════════════════
        { id: "exam-toggle", title: "考试模式", quickTitle: "考试模式", kind: "special",
          quick: true, quickOrder: 6, phone: false, order: 72, surfaces: ["action"] },

        { id: "exam-start", palOrder: 27, title: "开始考试模式", kind: "special",
          order: 73, surfaces: ["command"],
          keys: ["exam", "kaoshi", "ks"], tip: "全屏拦截 + 黑名单（无时长=手动结束）" },

        { id: "exam-stop", palOrder: 28, title: "结束考试模式", kind: "special",
          order: 74, surfaces: ["command"],
          keys: ["exam", "kaoshi", "ks"], tip: "恢复教室机（全屏拦截收掉）" },

        // ══ 选择 / 批量（走 runOnPicked，与多选按钮同一条路）══════════════
        // ⚠️ role 留空是**刻意的**：门控在 runOnPicked 内部按动作名判（permOk("lock")），
        //    这里再设一次 role 会与它重复 —— 且 pick-lock-all 传的是空动作名（不门控）。
        // ⚠️ title 与 palName 不同是**刻意的**：多选批量条上空间小（"锁屏选中的"），
        //    命令面板要能自解释（"锁屏选中的设备"）。以前这两处是两份独立硬编码。
        { id: "select-all", palOrder: 14, title: "选中全部在线设备", kind: "special",
          order: 80, surfaces: ["command"],
          keys: ["selectall", "quanxuan", "qx"], tip: "Ctrl+A：把在线设备全选上" },

        { id: "pick-clear", palOrder: 15, title: "取消选择", kind: "special",
          order: 81, surfaces: ["command"],
          keys: ["clearpick", "quxiaoxuan", "qxz"], tip: "Esc/点空白：把选中的机器清掉" },

        { id: "pick-lock", palOrder: 16, title: "锁屏选中的", palName: "锁屏选中的设备", kind: "special",
          bulk: true, order: 82, surfaces: ["command"],
          keys: ["locksel", "suoxuan", "sx"], tip: "Ctrl+L：只锁刚才选中的那几台" },

        { id: "pick-lock-all", palOrder: 17, title: "锁屏全部在线设备", kind: "special",
          order: 83, surfaces: ["command"],
          keys: ["lockall", "quanbu", "qb"], tip: "Ctrl+Shift+L：锁所有在线机器（需管理员）" },

        // 只在多选批量条上（不进右栏、不进命令面板）：surfaces 只给 action，
        // 且不带 group / quick / command ⇒ 三个投影各自都自然排除掉它。
        { id: "pick-shutdown", title: "关机选中的", kind: "special",
          bulk: true, order: 84, surfaces: ["action"] },

        // ══ 视图 / 系统（app 类：本地命令，不下发设备）════════════════════
        { id: "page-overview", palOrder: 18, title: "切到概览", kind: "app", app: "page", payload: "0",
          order: 90, surfaces: ["command"],
          keys: ["overview", "gailan", "gl"], tip: "在线设备 + 最近回执 + 今日课表" },

        { id: "page-control", palOrder: 19, title: "切到控制", kind: "app", app: "page", payload: "1",
          order: 91, surfaces: ["command"],
          keys: ["control", "kongzhi", "kz"], tip: "看教室机画面 + 下发动作" },

        // 「切到集控」与「切到控制」是**同一个页**（历史遗留的两个入口），都保留：
        // 老师两套叫法都能搜到。这是刻意的重复，不是漏删（见归档 §11.4）。
        { id: "page-jikong", palOrder: 20, title: "切到集控", kind: "app", app: "page", payload: "1",
          order: 92, surfaces: ["command"],
          keys: ["jikong", "jk"], tip: "看教室机画面 + 下发动作" },

        { id: "page-settings", palOrder: 21, title: "切到设置", kind: "app", app: "page", payload: "2",
          order: 93, surfaces: ["command"],
          keys: ["settings", "shezhi", "sz"], tip: "账户 / 连接 / 提醒 / 外观" },

        { id: "refresh-devices", palOrder: 31, title: "刷新设备列表", kind: "special",
          order: 94, surfaces: ["command"],
          keys: ["refresh", "shuaxin", "sx"], tip: "重新拉一遍云端下发（也可按 F5）" },

        { id: "login-site", palOrder: 32, title: "用网站账号登录", kind: "app", app: "login",
          order: 95, surfaces: ["command"],
          keys: ["login", "denglu", "dl"], tip: "走星璃账号授权，不用填密钥" },

        { id: "role-admin", palOrder: 33, title: "切成管理员身份", kind: "app", app: "role", payload: "admin",
          order: 96, surfaces: ["command"],
          keys: ["admin", "guanliyuan"], tip: "能操作教室机（电源 / 远控）" },

        { id: "role-teacher", palOrder: 34, title: "切成教师身份", kind: "app", app: "role", payload: "teacher",
          order: 97, surfaces: ["command"],
          keys: ["teacher", "jiaoshi", "js"], tip: "只留看画面 / 发通知 / 推文件" },

        { id: "theme-dark", palOrder: 35, title: "切到黑白外观", kind: "app", app: "theme", payload: "1",
          order: 98, surfaces: ["command"],
          keys: ["dark", "heibai", "hb"], tip: "默认外观" },

        { id: "theme-light", palOrder: 36, title: "切到浅色外观", kind: "app", app: "theme", payload: "0",
          order: 99, surfaces: ["command"],
          keys: ["light", "qianse", "qs"], tip: "换个亮堂的" },

        { id: "theme-auto", palOrder: 37, title: "跟随系统外观", kind: "app", app: "theme", payload: "2",
          order: 100, surfaces: ["command"],
          keys: ["auto", "xitong", "xt"], tip: "跟着系统配色走" },

        { id: "open-download", palOrder: 38, title: "打开官网下载页", kind: "app", app: "external",
          payload: "https://www.245959623.xyz/download",
          order: 101, surfaces: ["command"],
          keys: ["download", "xiazai", "xz"], tip: "打开 www.245959623.xyz/download" }
    ];
}

/** 需要自定义分支的 id 清单（宿主 SPECIAL 表的键）—— 供回归断言核对两边不漏。 */
function specialIds() {
    return ["software-list", "volume", "schedule",
            "voice-toggle", "voice-start", "voice-stop",
            "screen-toggle", "screen-start", "screen-stop",
            "exam-toggle", "exam-start", "exam-stop",
            "select-all", "pick-clear", "pick-lock", "pick-lock-all", "pick-shutdown",
            "refresh-devices"];
}

/** app 类的 app 子类型（宿主按它分派到 5 个固定本地方法）。 */
function appKinds() {
    return { page: 1, theme: 1, role: 1, login: 1, external: 1 };
}
