// 星集控 · Material 3 设计令牌单一真源（2026-10-13 · 用户批准为全项目唯一模板）
//
// 来源：qml/MaterialLab.qml 测试页。用户裁决 2026-10-13：「都可以，被控端和
// 管理端都用这个」——本文件 + qml/components/md3/ 组件库即两端的唯一视觉模板。
// 规范值：Material Design 3 baseline 色板 / 字阶 / 形状 / 状态层 / 动效。
//
// 规范（2026-10-22 用户点单「全局统一」）：本文件即全局设计系统（Design System），
// 与 AGENTS.md §9「全局 UI 构建红线」配套生效 —— 颜色/形状/间距/字阶/动效的唯一真源。
// 外部规范里的 Theme.qml（Singleton）在本项目的落地**就是本文件**：不新建第二个令牌文件，
// 避免两份真源漂移（历史：旧 Theme.qml 已退役并入此处，见下方迁移桥注释）。
//
// 用法（2026-10-25 起 = pragma Singleton，用户点单「MD3 太难控制」）：
//   任意 `import "components"`（或 `import "../components"`）的文件直接写：
//     color: Md3.cSurface   font.pixelSize: Md3.titleM   radius: Md3.rM
//   深浅：各端主外壳**唯一写入口** `Md3.dark = darkMode`（viewer Main.qml /
//   control AgentShell.qml）。需要临时固定浅色的模态窗口（如 control OOBE）
//   进入前保存原值、关闭时恢复，不要长期覆盖。
//
// 为什么改 singleton（2026-10-25）：
//   旧实现是「共享组件 + 每页自建实例」⇒ 全端 278 处 `md3:` 手工透传、
//   48 处 `property var md3` 声明、605 处 `md3.xxx` 引用；忘传即回落到组件内
//   硬编码色（如 th ? th.fg : "#1C1B1F"），出现「局部颜色不对」且不跟换肤。
//   旧理由「单例只能持一份值，需要深浅两套并存」经核查**不成立**：
//   Main.qml 的 darkTh/lightTh 仅因 th 声明成 property var 需预构造对象，
//   th 只取其一（darkMode ? darkTh : lightTh），从未同时显示两套。
//   singleton 后 dark 变化会触发全部令牌 binding 重算 —— 正是所需的全局随动。
//
// ⚠️ 兼容期约定（阶段 1，2026-10-25）：存量 `md3: Md3` / `md3.xxx` 写法继续可用
//   （单例对象可赋给 property var），阶段 2/3 再删透传、把 `md3.` 改 `Md3.`。
//   禁止再新增 `Md3 { ... }` 实例化写法（singleton 不可实例化，直接报错）。
//
// ── 迁移桥（legacy 别名，2026-10-13）─────────────────────────────
// 存量界面（桌面 Main.qml 3802 行 + Btn/InputField/Card/ToggleRow/RtcVideoSurface
// 等既有组件）读的是旧 Theme 令牌名（win/panel/fg/acc/rCtrl…）。本文件提供
// **同名别名**映射到 MD3 值：把 `Theme {}` 换成 `Md3 {}` 即整域换肤，无需逐页
// 重写。⚠️ 这是迁移桥不是长期 API——全端组件迁移完成后删除别名块。
pragma Singleton

import QtQuick 2.15

QtObject {
    id: root
    property bool dark: false

    // ═══ 色彩 · MD3 baseline ═══
    property color cPrimary:      dark ? "#D0BCFF" : "#6750A4"
    property color cOnPrimary:    dark ? "#381E72" : "#FFFFFF"
    property color cPrimaryCont:  dark ? "#4F378B" : "#EADDFF"
    property color cOnPrimaryCont:dark ? "#EADDFF" : "#21005D"
    property color cSecondary:      dark ? "#CCC2DC" : "#625B71"
    property color cSecondaryCont:  dark ? "#4A4458" : "#E8DEF8"
    property color cOnSecondaryCont:dark ? "#E8DEF8" : "#1D192B"
    property color cTertiary:      dark ? "#EFB8C8" : "#7D5260"
    property color cTertiaryCont:  dark ? "#633B48" : "#FFD8E4"
    property color cOnTertiaryCont:dark ? "#FFD8E4" : "#31111D"
    property color cError:         dark ? "#F2B8B5" : "#B3261E"
    property color cOnError:       dark ? "#601410" : "#FFFFFF"
    property color cErrorCont:     dark ? "#8C1D18" : "#F9DEDC"
    property color cOnErrorCont:   dark ? "#F9DEDC" : "#410E0B"
    property color cSurface:       dark ? "#1C1B1F" : "#FFFBFE"
    property color cOnSurface:     dark ? "#E6E1E5" : "#1C1B1F"
    property color cSurfaceVar:    dark ? "#49454F" : "#E7E0EC"
    property color cOnSurfaceVar:  dark ? "#CAC4D0" : "#49454F"
    property color cOutline:       dark ? "#938F99" : "#79747E"
    property color cOutlineVar:    dark ? "#49454F" : "#CAC4D0"
    // surface 容器五级（MD3 elevation 以色阶表达，不用投影）
    property color cContLowest:    dark ? "#0F0D13" : "#FFFFFF"
    property color cContLow:       dark ? "#1D1B20" : "#F7F2FA"
    property color cCont:          dark ? "#211F26" : "#F3EDF7"
    property color cContHigh:      dark ? "#2B2930" : "#ECE6F0"
    property color cContHighest:   dark ? "#36343B" : "#E6E0E9"
    property color cInverseSurf:   dark ? "#E6E1E5" : "#313033"
    property color cInverseOn:     dark ? "#313033" : "#F4EFF4"
    // 状态色（MD3 baseline 未定义语义状态，沿用项目 WCAG 校准值，err 归 MD3 error）
    property color cOk:            dark ? "#4CC38A" : "#1B7A43"
    // 成功容器（2026-10-28 蓝图还原补）：选中胶囊 / 在线标记的**浅底**，
    // 配 cOk 边框用。深浅两套都给了值 —— 原来各处硬编码 "#DCFCE7" 在深色主题上会刺眼。
    property color cOkCont:        dark ? "#123B2A" : "#DCFCE7"
    property color cWarn:          dark ? "#D4A017" : "#A87C0A"
    property color cInfo:          dark ? "#7FB3E0" : "#2A6394"
    // 品牌点缀色「终末地工业青」（2026-10-22 收编为令牌）：在线设备的"数据在流"左缘细规、
    // 侧栏选中项细规等冷调点缀，不抢紫色主色。此前在两处硬编码 #2FB8C6（深浅同值），
    // 按「零硬编码」纪律收编到此处；保持既有像素不变（深浅同值）。
    property color cAccent:        "#2FB8C6"

    // ═══ 清新极简 token 组（fresh · 2026-10-24 班级工作台预览点选） ═══
    // 来源：管理端班级工作台 HTML 预览（方案 A 横屏三栏，用户点选）：
    //   #F7F8FA 浅灰底 + 白色大圆角卡 + 绿色 accent + pill 按钮 + 微投影。
    // 边界纪律：**新增组、不动既有令牌** —— 被控端与管理端其余页面继续用
    //   MD3 baseline（两套并存，谁都不覆盖谁，避免一次性全端换肤的回归面）；
    //   班级工作台页族（viewer qml/pages/class/*）+ 外壳左栏 ClassNav 切到本组。
    // 深色同构：深灰底 + 深卡 + 同一支绿（绿在两档都够亮，不随 dark 变值）；
    //   字阶不新造 —— 沿用上方 MD3 字阶（项目纪律 12/13/14/16）。
    property color frBg:        dark ? "#0F1218" : "#F7F8FA"
    property color frCard:      dark ? "#161B22" : "#FFFFFF"
    property color frInk:       dark ? "#E5E7EB" : "#111827"
    property color frInk2:      dark ? "#9CA3AF" : "#6B7280"
    property color frLine:      dark ? "#232B36" : "#EEF0F3"   // 分隔线 / 输入控件底
    property color frGreen:     "#22C55E"                      // accent 主值（深浅同值）
    property color frGreenD:    "#16A34A"                      // accent 按下 / 深一档
    property color frGreenSoft: dark ? "#12281B" : "#E9FBF0"   // accent 容器（选中 pill 底）
    property color frOnGreen:   "#FFFFFF"                      // accent 上的文字
    property color frDanger:    dark ? "#F87171" : "#EF4444"   // 危险动作（删除）
    property color frDangerSoft:dark ? "#2A1517" : "#FEF2F2"   // 危险容器（删除确认条）
    property color frOffDot:    dark ? "#4B5563" : "#D1D5DB"   // 离线灰点
    // 形状：大圆角卡 + 全圆 pill（对齐预览 18/24 语言）
    property int frRCard: 18    // 卡片
    property int frRBig:  24    // 大卡 / 横幅
    property int frRIn:   12    // 卡内小块（输入框 / 徽标底）
    property int frRPill: 999   // 按钮 / 状态 pill（取值大于半高即全圆）
    // 投影：QML 无 box-shadow，消费方用两层偏移矩形近似；色/偏移/透明度集中此处
    property color frShadow:    dark ? "#000000" : "#101828"
    property int   frShadowY:   6
    property real  frShadowA:   dark ? 0.30 : 0.04

    // ═══ 形状（MD3 shape scale） ═══
    property int rXS: 4      // 文本框 / Snackbar
    property int rS: 8       // Chip / 小卡片
    property int rM: 12      // 卡片 / FAB / 菜单
    property int rL: 16      // FAB / 大面板
    property int rXL: 28     // 对话框 / 抽屉
    property int rFull: 999  // 按钮 / 开关 / pill（取值大于半高即全圆）

    // ═══ 间距（布局网格 · 2026-10-19 补，原令牌集缺间距刻度） ═══
    property int spXS: 4     // 紧凑组内（图标↔文字、分段之间）
    property int spS: 8      // 组内元素
    property int spM: 16     // 组件间距（默认）
    property int spL: 24     // 面板左右外边距 / 大区块间距
    property int spXL: 32    // 页面级留白 / 骨架大间距（2026-10-22 按规范补）

    // ═══ 动效（MD3 motion） ═══
    property int durShort: 200    // 颜色 / 开关 / 指示器
    property int durMedium: 350   // 进入 / 退出
    property int durLong: 500     // 几何形变（大元素）
    // 2026-10-28 蓝图还原：原型（手写稿附的 Tailwind 原型）里三处动效的时长
    // 与 MD3 默认刻度不一致，且果冻曲线带 overshoot。这三条是**还原参考**用的，
    // 不是替代上面三条 —— 新代码优先用语义名（durJelly/durDrawer/durModal），
    // 老代码里的 durShort/durMedium 保持不动。
    //   果冻：transition: transform 400ms cubic-bezier(.34,1.7,.64,1) ⇒ Easing.OutBack
    //   抽屉：transition: transform 400ms cubic-bezier(.2,.8,.2,1)  ⇒ Easing.OutCubic
    //   模态：transition: opacity 300ms ease                       ⇒ Easing.OutQuad
    property int durJelly: 400    // 果冻曲线（hover 1.05 / press 0.90 回弹）
    property int durDrawer: 400   // 抽屉滑入滑出
    property int durModal: 300    // 模态/遮罩淡入淡出
    // 果冻缩放刻度（原型 .jelly-effect:hover / :active）
    property real jellyHover: 1.05
    property real jellyPress: 0.90
    // emphasized decelerate（进入）/ standard（通用）
    property var easeEmph: [0.05, 0.7, 0.1, 1.0]
    property var easeStd:  [0.2, 0.0, 0.0, 1.0]
    // 状态层不透明度（MD3 state layers）
    property real stHover: 0.08
    property real stPress: 0.10

    // ═══ 字阶（MD3 type scale，px） ═══
    property int displayL: 57
    property int displayM: 45
    property int displayS: 36
    property int headlineL: 32
    property int headlineM: 28
    property int headlineS: 24
    property int titleL: 22
    property int titleM: 16
    property int titleS: 14
    property int bodyL: 16
    property int bodyM: 14
    property int bodyS: 12
    // 项目「正」字号：Main.qml 根 font.pixelSize: 13 兜底档（2026-10-22 收编为令牌，
    // 对齐规范字阶 bodyMedium=13）。页面里 13px 一律取此令牌，不再散落字面量。
    property int body13: 13
    property int labelL: 14
    property int labelM: 12
    property int labelS: 11

    // ═══ legacy 别名（迁移桥 · 全端迁移完成后删） ═══
    // 旧 Theme 令牌名 → MD3 值。存量组件/页面零改动换肤。
    property color win:    cSurface
    property color body:   cSurface
    property color panel:  cCont
    property color cream:  cContHigh
    property color hover:  cContHighest
    property color seg:    cContHigh
    property color hover2: cContLow
    property color line:   cOutlineVar
    property color canvas: cContHighest
    property color stroke: cOutlineVar
    property color stroke2:cOutline
    property color todo:   cContHigh
    property color card:   cContHighest
    property color sepline:cOutlineVar
    property color fg:     cOnSurface
    property color op:     cOnSurfaceVar
    property color fg3:    cOnSurfaceVar
    property color fg4:    cOutline
    property color inv:    cInverseSurf
    property color ph:     cOutline
    property color acc:    cPrimary
    property color ok:     cOk
    property color warn:   cWarn
    property color err:    cError
    property color info:   cInfo
    property int rWin:  rXL   // 16 → 28（MD3 对话框圆角）
    property int rCard: rM    // 12 → 12（MD3 卡片圆角）
    property int rCtrl: rFull // 8  → 20（MD3 按钮全圆）
}
