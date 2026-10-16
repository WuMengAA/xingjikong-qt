import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui"
import "../qml/ActionDefs.js" as Actions
import "../qml/UiPlugins.js" as Plugins

// 设备动作 / 命令的声明清单运行时探针（U4 · 2026-10-16）
//
// 分工：
//   · 与改造前清单的**逐字对照**（38 条 name/keys/tip、右栏 5 组、快捷行 7 条）
//     由 node 脚本完成 —— 需要一个"改造前原件"当基线，无法在 QML 里做。
//   · 这里验的是 QML 侧那部分：两个 .js 真能被解析（.pragma library 正常）、
//     注册表合并两份清单不冲突、四个投影（命令面板 / 右栏分组 / 快捷行 / 批量条）
//     真能派生出来。
//
// 跑法（无需构建，直接吃源码树）：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -platform offscreen scripts/_ui_actions_probe.qml
// ⚠️ Windows 上 Qt 日志走 OutputDebugString ⇒ 加 QT_ASSUME_STDERR_HAS_CONSOLE=1 才看得见文字；
//    判定不看日志，看**退出码**：10 = 全通过 / 20 = 有失败（"没输出"≠"没跑"）。
Window {
    id: win
    width: 480
    height: 320
    visible: true
    color: "#101010"

    Md3 { id: md3; dark: true }

    UiRegistry {
        id: reg
        scope: "viewer"
        permissions: []
        capabilities: []
        // Main.qml 里就是这一行（两份清单合并成一个注册表）
        manifest: Plugins.manifest().concat(Actions.manifest())
    }

    property int bad: 0
    property int total: 0

    function ck(name, cond) {
        total++
        if (cond) console.log("  ok   " + name)
        else { bad++; console.log("  FAIL " + name) }
    }
    function eq(name, got, want) {
        const g = JSON.stringify(got), w = JSON.stringify(want)
        ck(name + "  (" + g + ")", g === w)
    }

    Component.onCompleted: {
        // ── 一、两份清单合并后无冲突（id 跨清单也必须唯一）──
        eq("合并后条目数 = 58", reg.items.length, 58)
        eq("无 validate 问题", reg.problems, [])

        // ── 二、命令面板：38 条、按 palOrder 排 ──
        const cmds = reg.commandModel()
        eq("命令面板 38 条", cmds.length, 38)
        eq("前三条 = 锁屏 / 重启 / 关机教室机",
           cmds.slice(0, 3).map(function (c) { return c.name }),
           ["锁屏教室机", "重启教室机", "关机教室机"])
        eq("末条 = 打开官网下载页", cmds[37].name, "打开官网下载页")
        ck("每条都带 pluginId（点它才回得到声明）",
           cmds.every(function (c) { return !!c.pluginId }))
        ck("检索别名非空（打首字母要能搜到）",
           cmds.every(function (c) { return c.keys.length > 0 }))

        // ── 三、右栏：5 组、组内顺序、宽窄判定 ──
        eq("分组 = 通知/电源/看看/任务/批量",
           reg.actionGroups(), ["通知", "电源", "看看", "任务", "批量"])
        eq("电源组 3 条 = 锁屏/重启/关机",
           reg.actionsForGroup("电源").map(function (p) { return p.title }),
           ["锁屏", "重启", "关机"])
        eq("看看组 5 条",
           reg.actionsForGroup("看看").map(function (p) { return p.title }),
           ["截图", "软件", "监控", "音量", "文件"])
        ck("电源组全 wide（竖排）",
           reg.actionsForGroup("电源").every(function (p) { return p.wide }))
        ck("看看组无 wide（2 列网格）",
           reg.actionsForGroup("看看").every(function (p) { return !p.wide }))

        // ── 四、快捷行：7 条、按 quickOrder 排、手机端 5 条 ──
        const q = reg.quickActions()
        eq("快捷行 7 条且顺序正确",
           q.map(function (p) { return p.id }),
           ["lock", "notify-send", "broadcast-open", "voice-toggle",
            "screen-toggle", "exam-toggle", "multiband-open"])
        eq("快捷行文案",
           q.map(function (p) { return p.quickTitle || p.title }),
           ["锁屏", "发通知", "广播", "语音对讲", "屏幕广播", "考试模式", "多班墙"])
        eq("手机端 5 条（滤掉屏幕广播/考试模式）",
           q.filter(function (p) { return p.phone !== false }).length, 5)

        // ── 五、批量条 ──
        const b = reg.items.filter(function (p) { return p.bulk })
        eq("批量条 2 条", b.map(function (p) { return p.title }),
           ["锁屏选中的", "关机选中的"])

        // ── 六、门控口径（与改造前一致）──
        eq("lock 的能力需求 = [lock]（action 兜底）", reg.capNeeds(reg.byId("lock")), ["lock"])
        eq("software-list 的能力需求两条",
           reg.capNeeds(reg.byId("software-list")),
           ["list_shortcut_candidates", "process_list"])
        eq("terminal 的能力需求 = [terminal_open]", reg.capNeeds(reg.byId("terminal")), ["terminal_open"])
        ck("lock 的 role = admin、perm 空（人机两套门控不混）",
           reg.byId("lock").role === "admin" && reg.byId("lock").perm === "")
        eq("quickGuard 只有 lock 有", reg.items.filter(function (p) { return p.quickGuard })
           .map(function (p) { return p.id }), ["lock"])

        // ── 七、kind 分派 ──
        eq("kind=panel 都指向真实面板 id",
           reg.items.filter(function (p) {
               return p.kind === "panel" && !reg.byId(p.panel) && p.panel !== "broadcast"
           }).map(function (p) { return p.id }), [])
        eq("kind=app 的子类型共 5 种",
           (function () {
               const s = {}
               reg.items.forEach(function (p) { if (p.kind === "app") s[p.app] = 1 })
               return Object.keys(s).sort()
           })(), ["external", "login", "page", "role", "theme"])

        console.log("UI-ACTIONS-PROBE total=" + total + " bad=" + bad
                    + " 声明=" + reg.items.length
                    + " 命令=" + cmds.length + " 右栏组=" + reg.actionGroups().length
                    + " 快捷=" + q.length + " 批量=" + b.length)
        Qt.exit(bad === 0 ? 10 : 20)
    }
}
