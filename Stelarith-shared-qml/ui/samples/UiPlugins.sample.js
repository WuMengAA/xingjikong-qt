// 示例界面声明清单（U2 / U5 参考实现）—— 纯数据，照 UiContract.js 的形状写。
//
// 六条各自演示一种门控或一种扩展面，方便宿主探针一次性验完：
//   sample           panel + command       常规（两端都可见）
//   notify-demo      command + perm        权限门控
//   agent-only-demo  command + scope:agent 适用端过滤
//   lock-demo        action  + caps        被控端能力门控
//   sample-page-a/b  page                  「整屏页」面（U5 新增）—— 两条指向**同一个 entry**，
//                                          用于验证"同一 entry 被多条声明引用 ⇒ 建出多个独立实例"
.pragma library

function manifest() {
    return [
        {
            id: "sample",
            title: "示例界面插件",
            icon: "circle",
            scope: ["both"],
            order: 10,
            surfaces: ["panel", "command"],
            entry: "samples/SamplePanel.qml",
            keys: ["sample", "shili", "示例"],
            tip: "U2 的参考实现：由宿主按声明实例化"
        },
        {
            id: "notify-demo",
            title: "演示：需 notify 权限",
            icon: "bell",
            scope: ["both"],
            order: 20,
            surfaces: ["command"],
            perm: "notify",
            keys: ["notifydemo", "quanxian"],
            tip: "环境未上报权限时放行；上报了但缺 notify 则隐藏"
        },
        {
            id: "agent-only-demo",
            title: "演示：仅被控端",
            icon: "monitor",
            scope: ["agent"],
            order: 30,
            surfaces: ["command"],
            keys: ["agentonly"],
            tip: "管理端环境下应被 scope 过滤掉"
        },
        {
            id: "lock-demo",
            title: "演示：锁屏（需被控端能力）",
            icon: "lock",
            scope: ["both"],
            order: 40,
            surfaces: ["action"],
            action: "lock",
            caps: ["lock"],
            keys: ["lockdemo"],
            tip: "被控端未上报能力时放行；上报了但不含 lock 则隐藏"
        },
        {
            id: "sample-page-a",
            title: "示例页 A",
            icon: "circle",
            scope: ["both"],
            order: 100,
            surfaces: ["page"],
            entry: "samples/SamplePage.qml",
            tip: "U5 参考实现：整屏页由 UiPageHost 按声明实例化"
        },
        {
            id: "sample-page-b",
            title: "示例页 B",
            icon: "monitor",
            scope: ["both"],
            order: 110,
            surfaces: ["page"],
            entry: "samples/SamplePage.qml",
            tip: "与 A 指向同一 entry ⇒ 宿主应建出两个独立实例（按声明 id 缓存）"
        }
    ];
}
