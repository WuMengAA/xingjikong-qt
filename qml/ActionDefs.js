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
// 同一动作在三处的位置与宽度不同：按钮要短（"锁定桌面"）、命令面板要能自解释、
// 快捷行要一眼看懂。**统一的是清单与术语，不是长度** —— 所以 title / palName /
// quickTitle 各留一个，空则回落到 title。
//
// ⚠️ 2026-10-20 文案规范（用户点单）：全部改成「教育行业 B 端管理系统」标准术语 ——
//    对象统一叫「设备」，动作用「广播通知 / 文件分发 / 锁定桌面 / 屏幕监视 /
//    屏幕广播 / 远程关机 / 语音对讲」这套。规范与禁用词表在
//    Stelarith-shared-qml/ui/UiTerms.js，门禁 test-ui-terms.mjs 会把旧词（教室机 /
//    机器 / 黑屏 / 发通知 …）直接判红。**改这里之前先看那份表**，别再写口语。
//    keys 里的拼音别名保持原样：它们是"老师打首字母也能搜到"的兼容层，不是界面文案。
.pragma library

function manifest() {
    return [
        // ══ 设备动作（命令面板 38 条的主干）══════════════════════════════
        { id: "lock", palOrder: 1, title: "锁定桌面", palName: "锁定桌面", quickTitle: "锁定桌面",
          kind: "send", action: "lock", role: "admin", danger: true, quickGuard: "lock",
          group: "电源", wide: true, heavy: true, quick: true, quickOrder: 1, order: 20,
          surfaces: ["action", "command"],
          keys: ["lock", "suo", "sjb", "suoding", "zhuomian"], tip: "立即锁定所选设备桌面（需管理员身份）" },

        { id: "reboot", palOrder: 2, title: "远程重启", palName: "远程重启",
          kind: "send", action: "reboot", role: "admin", danger: true,
          group: "电源", wide: true, heavy: true, order: 21,
          surfaces: ["action", "command"],
          keys: ["reboot", "chongqi", "cq"], tip: "远程重启所选设备（需管理员身份）" },

        { id: "shutdown", palOrder: 3, title: "远程关机", palName: "远程关机",
          kind: "send", action: "shutdown", role: "admin", danger: true,
          group: "电源", wide: true, heavy: true, order: 22,
          surfaces: ["action", "command"],
          keys: ["shutdown", "guanji", "gj"], tip: "远程关闭所选设备（需管理员身份）" },

        { id: "notify-send", palOrder: 4, title: "广播通知", palName: "广播通知", quickTitle: "广播通知",
          kind: "panel", panel: "notify", caps: ["notify"],
          group: "通知", wide: true, heavy: true, quick: true, quickOrder: 2, order: 10,
          surfaces: ["action", "command"],
          keys: ["notify", "tongzhi", "tz"], tip: "向所选设备广播文本通知（需填写内容）" },

        { id: "screenshot", palOrder: 8, title: "屏幕截图", kind: "send", action: "screenshot",
          group: "监视", order: 30, surfaces: ["action", "command"],
          keys: ["screenshot", "jieku", "jk"], tip: "截取所选设备当前画面" },

        // 应用：要**同时**拿到候选列表与进程列表才算真能开，故 caps 显式点两条。
        // ⚠️ id 不能与 UiPlugins.js 的面板 id 同名（两者合并进同一注册表，
        //    契约的 validate 会报 "id 重复"）。面板 id 是 "software"，故这里加后缀。
        { id: "software-list", palOrder: 9, title: "应用管理", palName: "查看已安装应用",
          kind: "special", caps: ["list_shortcut_candidates", "process_list"],
          group: "监视", order: 31, surfaces: ["action", "command"],
          keys: ["software", "ruanjian", "rj"], tip: "查看设备已安装应用与运行中进程" },

        { id: "open-camera", palOrder: 10, title: "摄像头监视", palName: "摄像头监视",
          kind: "panel", panel: "camera", caps: ["camera_list"],
          group: "监视", order: 32, surfaces: ["action", "command"],
          keys: ["camera", "shexiangtou", "sxt"], tip: "查看设备摄像头画面" },

        { id: "volume", palOrder: 11, title: "音量调节", palName: "音量调节",
          kind: "special", caps: ["set_volume"],
          group: "监视", order: 33, surfaces: ["action", "command"],
          keys: ["volume", "yinliang", "yl"], tip: "调整设备音量" },

        { id: "open-file", palOrder: 12, title: "文件分发", palName: "文件分发",
          kind: "panel", panel: "file", caps: ["file_push"],
          group: "监视", order: 34, surfaces: ["action", "command"],
          keys: ["file", "wenjian", "wj"], tip: "向所选设备分发文件" },

        { id: "schedule", palOrder: 13, title: "定时任务", kind: "special", caps: ["list_schedules"],
          group: "任务", wide: true, order: 40, surfaces: ["action", "command"],
          keys: ["schedule", "dingshi", "ds"], tip: "设定定时关机或重启" },

        // 终端是开发者排障通道（能跑任意命令），不占老师日常界面 ⇒ 无 group，
        // 只活在命令面板（Ctrl+K 搜"终端"）。caps 让 0.6.3 之前那批设备入口不亮。
        { id: "terminal", palOrder: 5, title: "远程终端", palName: "远程终端",
          kind: "panel", panel: "term", role: "admin", caps: ["terminal_open"],
          order: 60, surfaces: ["command"],
          keys: ["terminal", "cmd", "zhongduan"], tip: "打开设备命令行（需管理员身份）" },

        { id: "remote-start", palOrder: 6, title: "开始远程控制", kind: "send",
          action: "remote_control_start", role: "admin",
          order: 61, surfaces: ["command"],
          keys: ["remote", "yuankong", "yk"], tip: "实时接管设备鼠标与键盘（需管理员身份）" },

        { id: "remote-stop", palOrder: 7, title: "停止远程控制", kind: "send",
          action: "remote_control_stop", role: "admin",
          order: 62, surfaces: ["command"],
          keys: ["remote", "yk"], tip: "结束当前远程控制会话" },

        // ══ 右栏批量组（只在右栏；命令面板有各自更细的入口）══════════════
        { id: "control-open", title: "批量管控…", kind: "panel", panel: "control",
          group: "批量", wide: true, order: 50, surfaces: ["action"] },

        { id: "speech-open", title: "语音对讲…", kind: "panel", panel: "speech",
          group: "批量", wide: true, order: 51, surfaces: ["action"] },

        // ══ 概览快捷行 + 面板入口（page 字段 = 执行前切到第 N 页）══════════
        // ⚠️ panel 写 "broadcast" 而非 "control"：openOnly 里 broadcast 是**别名**，
        //    指向 control。保持写别名 ⇒ 哪天别名规则改了，两处一起改（单一真源）。
        { id: "broadcast-open", palOrder: 22, title: "屏幕广播", palName: "向全部设备广播屏幕", quickTitle: "屏幕广播",
          kind: "panel", panel: "broadcast", page: 1,
          quick: true, quickOrder: 3, order: 63, surfaces: ["action", "command"],
          keys: ["broadcast", "guangbo", "gb"], tip: "将教师端画面同步至全部在线设备" },

        { id: "multiband-open", palOrder: 29, title: "多班监视墙", palName: "刷新多班监视墙", quickTitle: "多班监视墙",
          kind: "panel", panel: "multiband", page: 1,
          quick: true, quickOrder: 7, order: 64, surfaces: ["action", "command"],
          keys: ["multiband", "duoban", "db"], tip: "按班查看设备实时画面（最多 9 台在线）" },

        { id: "open-interact", palOrder: 30, title: "互动审批", kind: "panel", panel: "interact",
          order: 65, surfaces: ["command"],
          keys: ["interact", "shenpi", "sp", "diange", "zhufu"],
          tip: "审核学生点歌与祝福，右侧预览设备端显示效果" },

        // ══ 语音对讲（快捷行是**切换**语义；命令面板是分开的开/停两条）═════
        { id: "voice-toggle", title: "语音对讲", quickTitle: "语音对讲", kind: "special",
          quick: true, quickOrder: 4, order: 66, surfaces: ["action"] },

        { id: "voice-start", palOrder: 23, title: "开始语音对讲", kind: "special",
          order: 67, surfaces: ["command"],
          keys: ["voice", "yuyin", "yy"], tip: "向全部设备广播语音，再次点击停止" },

        { id: "voice-stop", palOrder: 24, title: "停止语音对讲", kind: "special",
          order: 68, surfaces: ["command"],
          keys: ["voice", "yuyin", "yy"], tip: "结束语音广播" },

        // ══ 屏幕广播（手机端没有：老师桌面推流是桌面专属）═════════════════
        { id: "screen-toggle", title: "屏幕广播", quickTitle: "屏幕广播", kind: "special",
          quick: true, quickOrder: 5, phone: false, order: 69, surfaces: ["action"] },

        { id: "screen-start", palOrder: 25, title: "开始屏幕广播", kind: "special",
          order: 70, surfaces: ["command"],
          keys: ["screen", "guangbo2", "gb2"], tip: "将教师端画面同步至全部在线设备" },

        { id: "screen-stop", palOrder: 26, title: "停止屏幕广播", kind: "special",
          order: 71, surfaces: ["command"],
          keys: ["screen", "tingbo"], tip: "结束屏幕广播" },

        // ══ 考试模式 ═════════════════════════════════════════════════════
        { id: "exam-toggle", title: "考试模式", quickTitle: "考试模式", kind: "special",
          quick: true, quickOrder: 6, phone: false, order: 72, surfaces: ["action"] },

        { id: "exam-start", palOrder: 27, title: "开始考试模式", kind: "special",
          order: 73, surfaces: ["command"],
          keys: ["exam", "kaoshi", "ks"], tip: "启用全屏锁定与应用黑名单，需手动结束" },

        { id: "exam-stop", palOrder: 28, title: "结束考试模式", kind: "special",
          order: 74, surfaces: ["command"],
          keys: ["exam", "kaoshi", "ks"], tip: "退出考试模式，恢复正常使用" },

        // ══ 选择 / 批量（走 runOnPicked，与多选按钮同一条路）══════════════
        // ⚠️ role 留空是**刻意的**：门控在 runOnPicked 内部按动作名判（permOk("lock")），
        //    这里再设一次 role 会与它重复 —— 且 pick-lock-all 传的是空动作名（不门控）。
        // ⚠️ title 与 palName 不同是**刻意的**：多选批量条上空间小（"锁定选中设备"），
        //    命令面板要能自解释（"锁定选中设备的桌面"）。以前这两处是两份独立硬编码。
        { id: "select-all", palOrder: 14, title: "选中全部设备", kind: "special",
          order: 80, surfaces: ["command"],
          keys: ["selectall", "quanxuan", "qx"], tip: "Ctrl+A：选中全部在线设备" },

        { id: "pick-clear", palOrder: 15, title: "取消选择", kind: "special",
          order: 81, surfaces: ["command"],
          keys: ["clearpick", "quxiaoxuan", "qxz"], tip: "Esc：清空当前选择" },

        { id: "pick-lock", palOrder: 16, title: "锁定选中设备", palName: "锁定选中设备的桌面", kind: "special",
          bulk: true, order: 82, surfaces: ["command"],
          keys: ["locksel", "suoxuan", "sx"], tip: "Ctrl+L：仅锁定已选中的设备" },

        { id: "pick-lock-all", palOrder: 17, title: "锁定全部设备", kind: "special",
          order: 83, surfaces: ["command"],
          keys: ["lockall", "quanbu", "qb"], tip: "Ctrl+Shift+L：锁定全部在线设备（需管理员）" },

        // 只在多选批量条上（不进右栏、不进命令面板）：surfaces 只给 action，
        // 且不带 group / quick / command ⇒ 三个投影各自都自然排除掉它。
        { id: "pick-shutdown", title: "关闭选中设备", kind: "special",
          bulk: true, order: 84, surfaces: ["action"] },

        // ══ 视图 / 系统（app 类：本地命令，不下发设备）════════════════════
        { id: "page-overview", palOrder: 18, title: "前往概览", kind: "app", app: "page", payload: "0",
          order: 90, surfaces: ["command"],
          keys: ["overview", "gailan", "gl"], tip: "查看设备在线状态、最近回执与今日课表" },

        // ══ 课堂工具（2026-10-25 · 教室小工具第一刀）═════════════════════
        // 落点**全部是被控端已有指令**，管理端此前只有单机音量滑块（volume 那条），
        // 班级场景缺"对全班一键"。这里补批量入口，被控端零改动、零新协议。
        // ⚠️ 音量三档用固定值而不是滑块：批量场景下"拖到一半点下发"没法对 40 台
        //    逐台确认，档位是老师真正会用的形状（静音 / 标准 / 大声）。
        // ⚠️ study_mode / noise_monitor 在被控端有「本地开关」门：学生机自己关了
        //    噪音监测，这两条会回 skipped（不是失败）⇒ 回执里看得见，不会假装成功。
        { id: "pick-volume-mute", title: "全班静音", palName: "将全班设备设为静音",
          kind: "special", action: "set_volume", params: { value: 0 },
          group: "课堂", wide: true, bulk: true, order: 70, surfaces: ["action"],
          keys: ["mute", "jingyin", "jy"], tip: "把全班在线设备的系统主音量设为 0" },

        { id: "pick-volume-low", title: "音量 30%", palName: "将全班设备音量设为 30%",
          kind: "special", action: "set_volume", params: { value: 30 },
          group: "课堂", bulk: true, order: 71, surfaces: ["action"],
          keys: ["volume30", "yl30"], tip: "把全班在线设备的系统主音量设为 30%" },

        { id: "pick-volume-high", title: "音量 80%", palName: "将全班设备音量设为 80%",
          kind: "special", action: "set_volume", params: { value: 80 },
          group: "课堂", bulk: true, order: 72, surfaces: ["action"],
          keys: ["volume80", "yl80"], tip: "把全班在线设备的系统主音量设为 80%" },

        { id: "pick-study-on", title: "开启专注模式", palName: "对全班开启专注模式",
          kind: "special", action: "study_mode_start",
          group: "课堂", bulk: true, order: 73, surfaces: ["action"],
          keys: ["study", "zhuanzhu", "zz"], tip: "对全班开启专注模式（自习/课堂静音纪律）" },

        { id: "pick-study-off", title: "结束专注模式", palName: "对全班结束专注模式",
          kind: "special", action: "study_mode_stop",
          group: "课堂", bulk: true, order: 74, surfaces: ["action"],
          keys: ["studyoff", "zzoff"], tip: "对全班结束专注模式，恢复正常提醒" },

        { id: "pick-noise-on", title: "开启噪音监测", palName: "对全班开启噪音监测",
          kind: "special", action: "noise_monitor_start",
          group: "课堂", bulk: true, order: 75, surfaces: ["action"],
          keys: ["noise", "zaoyin", "zy"], tip: "对全班开启噪音监测（超阈值留证）" },

        { id: "pick-noise-off", title: "关闭噪音监测", palName: "对全班关闭噪音监测",
          kind: "special", action: "noise_monitor_stop",
          group: "课堂", bulk: true, order: 76, surfaces: ["action"],
          keys: ["noiseoff", "zyoff"], tip: "对全班关闭噪音监测" },

        { id: "page-control", palOrder: 19, title: "前往屏幕监视", kind: "app", app: "page", payload: "1",
          order: 91, surfaces: ["command"],
          keys: ["control", "kongzhi", "kz"], tip: "查看设备实时画面并下发操作" },

        // 「前往集控」与「前往屏幕监视」是**同一个页**（历史遗留的两个入口），都保留：
        // 老师两套叫法都能搜到（"集控"是产品名，不是口语）。这是刻意的重复，不是漏删。
        { id: "page-jikong", palOrder: 20, title: "前往集控", kind: "app", app: "page", payload: "1",
          order: 92, surfaces: ["command"],
          keys: ["jikong", "jk"], tip: "查看设备实时画面并下发操作" },

        { id: "page-settings", palOrder: 21, title: "前往系统设置", kind: "app", app: "page", payload: "2",
          order: 93, surfaces: ["command"],
          keys: ["settings", "shezhi", "sz"], tip: "账户、连接、通知与外观" },

        // 2026-10-20 V1.0 P1：MD3 页面级骨架（Expanded 宽屏：240 侧栏 + 顶栏 + 操作区 + 4 列网格 + 侧滑面板）。
        // 只加导航声明，**不动现有 5 页** —— 骨架先并行验收，是否替换旧壳（底栏 4 标签）等点单。
        { id: "page-md3shell", palOrder: 22, title: "前往新版界面", kind: "app", app: "page", payload: "6",
          order: 95, surfaces: ["command"],
          keys: ["md3", "xinban", "xb"], tip: "MD3 页面级骨架（侧栏 + 顶栏 + 设备网格）" },

        { id: "refresh-devices", palOrder: 31, title: "刷新设备列表", kind: "special",
          order: 94, surfaces: ["command"],
          keys: ["refresh", "shuaxin", "sx"], tip: "重新获取设备状态（F5）" },

        { id: "login-site", palOrder: 32, title: "使用平台账号登录", kind: "app", app: "login",
          order: 95, surfaces: ["command"],
          keys: ["login", "denglu", "dl"], tip: "通过星璃账号授权登录" },

        { id: "role-admin", palOrder: 33, title: "切换为管理员", kind: "app", app: "role", payload: "admin",
          order: 96, surfaces: ["command"],
          keys: ["admin", "guanliyuan"], tip: "可执行电源与远程控制操作" },

        { id: "role-teacher", palOrder: 34, title: "切换为教师", kind: "app", app: "role", payload: "teacher",
          order: 97, surfaces: ["command"],
          keys: ["teacher", "jiaoshi", "js"], tip: "仅可查看画面、广播通知与文件分发" },

        { id: "theme-dark", palOrder: 35, title: "深色外观", kind: "app", app: "theme", payload: "1",
          order: 98, surfaces: ["command"],
          keys: ["dark", "heibai", "hb"], tip: "默认外观" },

        { id: "theme-light", palOrder: 36, title: "浅色外观", kind: "app", app: "theme", payload: "0",
          order: 99, surfaces: ["command"],
          keys: ["light", "qianse", "qs"], tip: "切换为浅色外观" },

        { id: "theme-auto", palOrder: 37, title: "跟随系统外观", kind: "app", app: "theme", payload: "2",
          order: 100, surfaces: ["command"],
          keys: ["auto", "xitong", "xt"], tip: "跟随操作系统配色" },

        { id: "open-download", palOrder: 38, title: "打开官网下载页", kind: "app", app: "external",
          payload: "https://www.245959623.xyz/download",
          order: 101, surfaces: ["command"],
          keys: ["download", "xiazai", "xz"], tip: "打开 www.245959623.xyz/download" }
    ];
}

/** 需要自定义分支的 id 清单（宿主 SPECIAL 表的键）—— 供回归断言核对两边不漏。 */
/** special 类的 id 清单（供门禁比对「宿主 runSpecial 的 case 是否齐全」）。
 *  2026-10-25：改成**从 manifest() 派生**。原来是手抄的 18 个 id —— 那正是本文件
 *  立件要消灭的「多份清单」病的残余：加了 7 个课堂工具条目忘了抄这里，门禁立刻报
 *  「宿主有、声明没有」。手抄清单 = 迟早漏，派生才是单一真源。 */
function specialIds() {
    return manifest().filter(function (p) { return p.kind === "special" })
                     .map(function (p) { return p.id });
}

/** app 类的 app 子类型（宿主按它分派到 5 个固定本地方法）。 */
function appKinds() {
    return { page: 1, theme: 1, role: 1, login: 1, external: 1 };
}
