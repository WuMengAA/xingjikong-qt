# 加减归档 · 画面区放大 + UI 统一 + 自检开关（管理端 0.6.13）

日期：2026-10-07
版本：0.6.12 → 0.6.13
触发：用户 ——「留给画面的区域太少了」「UI 值得统一标准，找好 UI 库」

---

## 一、画面区太小（截图那个现象）

### 实测（先量再改）

我写了个只在 `build/` 里的临时探针（QML 是运行时加载的，不必重编译），量控制页三栏：

```
PROBE left=160 mid=671 right=293 win=1180 rightImpl=293
```

**修前**（按用户截图换算，原图 1756×1173 → 逻辑宽 1180）：

| 栏 | 修前 | 修后 |
|---|---|---|
| 左（设备） | 168 | **160**（+ 上限 184） |
| **中（画面）** | **~299** | **671** |
| 右（动作） | ~657 | **293**（上限 300） |

画面 **299 → 671，翻了 2.24 倍**。

### 根因

`RowLayout` 里给右栏写的是 `Layout.preferredWidth: 196`，但 **preferredWidth 不是硬约束**：
右栏内容的自然宽度是 **293**，而某项把右栏实际顶到了 657，多出来的宽度是从"靠 `fillWidth` 吃剩余的
画面区"身上抢的 —— 画面就被压成了一条竖缝。

### 改法（确定性修复，不依赖"猜是谁顶的"）

- 左栏：`preferredWidth: 160` + `Layout.maximumWidth: 184`
- 画面：`Layout.fillWidth: true` + 显式 `preferredWidth: 640`（声明"剩余优先给它"）
- 右栏：`preferredWidth: 280` + **`Layout.maximumWidth: 300`**（2 列 120 按钮＝250，280 够、300 封顶）

`Layout.maximumWidth` 是硬上限，谁再想撑都撑不动。

画面本体（`Rectangle` + `Image { fillMode: PreserveAspectFit }`）本身是 `fillWidth+fillHeight`，
所以**容器一变宽，画面就跟着变大**，不需要另外改比例逻辑。

---

## 二、UI 统一（用户第二条）

### 盘点（实测计数，不是印象）

| 项 | 修前 | 修后 |
|---|---|---|
| 裸 `Button { }`（Qt 自带，走系统调色板） | **5** | **0** |
| `InputField` | 7 | 7 |
| `Btn` | 10 | **15** |
| `Card` / `EmptyState` | **0 引用** | 0（列进下一步） |
| 手写 `MouseArea` | 22 | 22 |

5 处裸 Button 分别：发送到选中设备 / 设定 / 广播 / 开始考试 / 结束考试。
**裸 Button 是黑白界面里唯一会"自己变白"的控件** —— 与 0.6.11 那个"输入框白底白字"同源（都走系统调色板）。
全部换成 `Btn`（补了显式 `width`，因为 Btn 是 Rectangle、没有 implicitWidth）。

### 调研结论：**不引第三方 UI 库**

写进 `docs/UI规范-2026-10-07.md`，要点：

- Qt 官方样式（Fusion / Material / Universal / **FluentWinUI3**，本机 Qt 6.8.1 已带实现）——
  能用，但会**整套推翻黑白设计稿**，要换就是全站换，不是局部换；
- Kirigami（拖 KF6 运行时）/ Cutie（Linux 触摸端）/ qml-material（Qt5 停更）—— 对本项目都不划算；
- **Fluent 2 / Material 3 / Ant Design / Apple HIG 该抄的是"标准维度"（间距阶、字号阶、状态机），不是皮**。

**我们缺的不是库，是唯一真源。** 证据就是上面那四条（输入框白底白字、裸 Button 会变白、
`theme: th` 绑成自己、Card/EmptyState 建了没人用）。

规范里同时定死了：令牌表（`darkTh`/`lightTh` 全部键与用途）、圆角三档、字号四档、
组件白名单、以及 **6 条禁止写法**（每条都对应一个真实事故）。

---

## 三、新增：离屏自检不再被"单实例"挡住

**这是这次能一次改对的底气。**

`src/singleinstance.cpp` 加开发开关（`.h/.cpp` 均已按规矩**原样同步**到 `Stelarith-control-qt/src/`，
两端 md5 一致：`4c900fa927e5525a` / `6a60234831c4b7e4`）：

```cpp
if (qEnvironmentVariableIsSet("STE_ALLOW_MULTI")) {   // 默认不设 ⇒ 行为与以前逐字一致
    fprintf(stderr, "[stelarith] STE_ALLOW_MULTI 已设 → 跳过单实例守卫（仅调试）\n");
    return true;
}
```

**为什么值得加**：0.6.9 / 0.6.10 / 0.6.11 连续三轮都因为用户开着管理端（互斥体互斥）而**没法自检**，
于是 0.6.11 那个 `InputField: Binding loop` 一路带到用户眼前才被发现。现在跑着正式版也能起离屏实例。

⚠️ 默认行为不变，生产照旧单实例；教室机上别设这个。

---

## 四、验证

| 环节 | 结果 |
|---|---|
| 探针实测 | `left=160 mid=671 right=293 win=1180`（画面 299 → 671） |
| qmllint | Main.qml 无 Error |
| 编译 | viewer `[exit] 0`；被控端 `[build-agent] OK`（含同步的 singleinstance.cpp） |
| 离屏自检 | **报错行 0**（`STE_ALLOW_MULTI=1` 下跑通，未打扰正在运行的管理端） |
| 出包 | 见下（make-portable 18/18） |
| 线上 | ota 指向 0.6.13 |

---

## 五、没做 / 没验证

- **`Card` / `EmptyState` 仍 0 引用**：页面里的卡片壳与空态还是手写，换成组件属于结构性改动，
  留到下一轮（规范文档里列成第 2 步）。
- **Theme 单例没做**：它牵动全部 8 个组件和所有调用点（根除 `theme: th` 传参），
  必须独立一轮做完整验证，不能跟布局改动混在一起。列在规范文档"下一步"第 1 条。
- **真机观感未验**：画面放大后的实际观感、5 个换过的按钮（宽度 60/90/160/整行）在真机上的排版，
  离屏量得到几何、量不出好看。请装上 0.6.13 看一眼。
- 被控端的 `singleinstance.cpp` 同步**未提交**（那边 main.cpp 仍压着别人的在途工作）。
