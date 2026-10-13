# 加减归档 · UI 转 Material 3（唯一模板落地两端）· 2026-10-13

**背景**：用户看 MaterialLab 测试页后裁决「都可以，被控端和管理端都用这个」——MD3 令牌 + 组件库定为全项目唯一视觉模板。本批把模板落地：两端建令牌/组件库、管理端手机端全量迁移、桌面端换令牌源、被控端灵动岛换令牌（QML/C++ 双绘 parity）。

**版本**：0.6.30 → 0.6.31（两端）

---

## 一、新增文件

### 1. 管理端（Stelarith-viewer-qt）

| 路径 | 行数 | 职责 |
|---|---|---|
| `qml/components/Md3.qml` | 125 | **MD3 令牌单一真源**：baseline 十色 + surface 五级容器 + 字阶 13 级 + 形状 6 档 + 动效 3 时长/2 缓动 + 状态层。深浅两实例（`dark` 布尔切换），照 Theme.qml「共享组件+两实例」模式（纯 Singleton 无法深浅并存） |
| `qml/components/md3/Md3Btn.qml` | 73 | 按钮六变体（filled/tonal/elevated/outlined/text/error），高 40 全圆，状态层 8%/10% |
| `qml/components/md3/Md3Fab.qml` | 54 | FAB regular 56 / mini 40，label 非空自动 extended |
| `qml/components/md3/Md3TextField.qml` | 66 | filled 浮动标签 + 聚焦 2dp 底线 + 错误态。**不设 background**（#94 暗色白底白字事故教训） |
| `qml/components/md3/Md3SwitchRow.qml` | 66 | 开关行：52×32 轨道，thumb 16→24 emphasized 缓动 |
| `qml/components/md3/Md3CheckBox.qml` | 34 | 18×18 圆角 2 |
| `qml/components/md3/Md3Radio.qml` | 32 | 20 圆 + 10 实心点 |
| `qml/components/md3/Md3Chip.qml` | 55 | filter chip：选中 secondaryContainer + 前导 ✓ |
| `qml/components/md3/Md3Card.qml` | 25 | 卡片三态（elevated/filled/outlined），圆角 12 |
| `qml/components/md3/Md3ListItem.qml` | 121 | 列表项：leading 容器 + headline/supporting + trailing 文字/开关 + 分隔线 |
| `qml/components/md3/Md3NavBar.qml` | 70 | 底部导航：80 高 + pill 64×32 + badge |
| `qml/components/md3/Md3Dialog.qml` | 69 | 对话框：scrim 32% + 卡片 28 圆角 + 三角色动作 |
| `qml/components/md3/Md3Snackbar.qml` | 62 | Snackbar：inverse 底 + action + 4s 自动消失 |
| `scripts/_md3_probe.cpp`（重写） | ~250 | 截图探针：**新增 StubBackend**（假 devices/events/recordings + 信号），Md3 实例注入 root `md3` 属性，`--page/--dialog/--snack` 按 objectName 驱动 |
| `scripts/_md3_probe.py`（重写） | ~140 | 编译配方加 moc 步（StubBackend 带 Q_OBJECT）；截图目标改手机端四页 + 触发态 |

### 2. 被控端（Stelarith-control-qt）

| 路径 | 行数 | 职责 |
|---|---|---|
| `qml/components/Md3.qml` | 125 | 与管理端**逐字相同**（跨端一致是模板的意义） |
| `qml/components/md3/*.qml`（12 个） | 748 | 同上，逐字相同。被控端后续 OOBE/设置窗/考试模式浮层直接用 |

## 二、修改的既有文件

### 管理端

| 文件 | 改动 | 为什么 |
|---|---|---|
| `qml/mobile/MobileShell.qml` | 全量重写（265→248 行）：Theme→Md3 实例、顶部条 52→64（MD3 top app bar）、BottomNav→Md3NavBar、Dialog→Md3Dialog、toast→Md3Snackbar、四页改传 `md3:` | 手机端迁 MD3。保留全部 backend 接线/定时器/汇总函数原样 |
| `qml/mobile/Main.qml` | 同上（267→235 行，ApplicationWindow 独立壳，Android 入口） | 同上 |
| `qml/mobile/OverviewPage.qml` | 重写（86→98）：灰面板卡→MD3 filled 卡（containerHigh），数字 22px→displayS 36 | 字阶/色板归 MD3 |
| `qml/mobile/DeviceListPage.qml` | 重写（83→86）：行高 68→76，ping/关机改 MD3 text 按钮（primary/error + 状态层） | 同上 |
| `qml/mobile/DeviceDetailDrawer.qml` | 重写（96→130）：抽屉背景改 containerLow + **顶部 28 圆角**（整圆角+同色盖下半的经典做法）+ 拖拽手柄；操作行→Md3ListItem；关机→filled error 按钮 | 同上 |
| `qml/mobile/MonitorPage.qml` | 重写（83→77）：设备横条 44→60，选设备按钮→Md3Chip（filter 态）；停止→mini FAB | 同上。RtcVideoSurface 收 `theme: page.md3`（legacy 别名桥） |
| `qml/mobile/SettingsPage.qml` | 重写（194→201）：身份/外观的圆角矩形按钮→Md3Chip；登录→Md3Btn filled；分组卡→Md3Card filled；告警 severity 点/色归 MD3 令牌 | 同上 |
| `qml/Main.qml`（桌面 3802 行） | **仅 2 行**：`Theme { dark: … }` ×2 → `Md3 { dark: … }`（含注释更新） | 桌面端本批只换令牌源——Md3 的 legacy 别名（win/panel/fg/acc/rCtrl…）让 3802 行与全部旧组件（Btn/InputField/Card/ToggleRow/RtcVideoSurface/各 Dialog）**零改动**换 MD3 肤。组件逐个迁 md3/ 是下批，完成后删别名块 |
| `CMakeLists.txt` / `src/main.cpp` | 仅版本号 0.6.30→0.6.31（bump 脚本 4 副本） | 版本纪律 |

### 被控端

| 文件 | 改动 | 为什么 |
|---|---|---|
| `qml/island/IslandTheme.qml` | 2 个令牌：`severityUrgent` (180,20,20,210)→(179,38,30,210)（MD3 error #B3261E）；`accent` #0A84FF/#2997FF → #6750A4/#D0BCFF（MD3 primary）。inform/remind 保留（协议语义档，MD3 baseline 无定义） | 灵动岛是「动态岛」身份组件，形状（36/56/168）与半透明底**不动**（换了就不是岛）；只把色彩族归 MD3 |
| `src/island.cpp:924` | urgent 全屏底色 (180,20,20,210)→(179,38,30,210) | **QML/C++ 双绘 parity 铁律**（IslandTheme 头注要求逐字一致） |
| `CMakeLists.txt` / `src/main.cpp` | 版本号 0.6.30→0.6.31 | 版本纪律 |

## 三、删除的文件

| 文件 | 能否恢复 | 说明 |
|---|---|---|
| `Stelarith-viewer-qt/qml/mobile/BottomNav.qml` | 能（git 历史） | 功能被 Md3NavBar 完全取代（旧实现 54 高灰底 TabBar，MD3 80 高 pill 指示器）。两处实例化点（MobileShell/Main）本批已改 |

## 四、接口/签名迁移对照表

| 旧 | 新 | 影响面 |
|---|---|---|
| 页面收 `th: Theme实例` | 页面收 `md3: Md3实例`，读 `md3.cXxx` | 仅 mobile/ 8 文件（本批全改） |
| 桌面/旧组件读 `th.win/panel/fg/acc/rCtrl…` | **不变**——Md3 提供同名 legacy 别名 | 桌面 Main.qml + 全部旧组件零改动 |
| `import "../components"`（Theme） | `import "../components"`（Md3）+ `import "../components/md3"`（组件） | 壳文件 2 处 |
| BottomNav 组件 | Md3NavBar | mobile 2 处 |
| 探针 `--dialog/--snack`（MaterialLab 专属函数） | 同参数改走 objectName findChild（confirmDialog/snack） | 仅探针 |

## 五、验证证据

| 项 | 命令 | 结果 |
|---|---|---|
| 静态检查 | `qmllint Md3.qml md3/*.qml mobile/*.qml` | **零 Error 零 Warning** |
| 被控端编译 | `build-agent.sh` | exit 0，island.cpp/main.cpp 重编，qml/→build 拷贝（含 components/md3/） |
| 管理端编译 | `JOBS=1 build-qt-viewer.bat` | exit 0（见 build-viewer-0631.log） |
| 手机端截图 | `python scripts/_md3_probe.py` | 7/7 OK：mob-light-0..3 / mob-dark-0 / dialog / snack |
| 灵动岛渲染 | `python scripts/_island_qml_probe.py --expand [--light]` | 深浅两张渲染正常，令牌变更无回归 |
| 截图目检 | Read 全部 9 张 | 卡片色阶/字阶/pill 指示器/chip 选中态/对话框 28 圆角+scrim/Snackbar inverse+撤销/深色五级容器全部符合 MD3 规范 |

截图位置：`D:/Stelarith/_probe/shots/mob-*.png`、`island-md3-*.png`。

## 六、未验证部分（明确声明）

1. **桌面端 Main.qml 只换了令牌源，未逐页迁组件**——3802 行的 Btn/InputField/Card 等仍是旧形状（旧圆角/高度），只是颜色变成 MD3。完整桌面端迁移是下批。
2. **真机手感未测**：截图是离屏定格，点击/滚动/动效（ emphasized 缓动的手感）要真机摸。
3. **被控端 OOBE 配置窗 / 设置窗 / 考试模式全屏 / 广播全屏未迁**——它们是 C++ QWidget 或独立 QML 窗，属需求 #2/#3/#4 的范围，本批只备好了组件库。
4. **DeviceDetailDrawer 抽屉未单独截图**（需先有 dev 注入 + open，探针未驱动）；抽屉内组件与列表页同源，目检由 dialog/snack 覆盖组件正确性。
5. **viewer ota.json 仍未发 0.6.31**：viewer OTA 通道吃 zip（updater.cpp 解压+robocopy），与用户「出 zip 没有任何意义」口径冲突，**待用户裁定**（走 NSIS 全量则 OTA 通道要改，或桌面端只发全量链接）。
6. **管理端 offscreen 冒烟未跑**（QML 界面程序，无 backend 起不来）；本次以 stub backend 探针覆盖手机端页面渲染。
7. agent 0.6.31 未发布 ota.json（出包后 publish，见当轮收尾）。

---

# 附批（2026-10-14 00:3x）：布局模板演示 LayoutLab 五屏

用户点单「接下来看布局——不同界面的模板和简易演示 demo」。交付 `qml/LayoutLab.qml` + `qml/layout/` 五屏：

| 模板 | 尺寸 | 内容 | 对应需求 |
|---|---|---|---|
| DesktopConsole | 1280×800 | 桌面集控台：导航轨（pill 激活）+ 顶栏（作用域切换器/搜索/账户）+ 统计四卡 + 设备网格（画面占位/状态点/名称/ping/关机） | 桌面端迁移底稿 |
| OobeConfig | 560×640 | 配置教室：**只露一个接入码输入**（URL/令牌收「高级」折叠）+ 状态行 + 保存并连接 | 需求 #2 界面预演 |
| ExamMode | 1280×800 | 考试模式全屏：标题+倒计时大数字 + 统计 + 座位网格（求助红框高亮）+ 退出需监考密码 | #88 延伸 |
| LocalSettings | 560×680 | 本机设置：五个本地开关（语音/摄像头/广播/通知/监控角标）+ 「云端可否强制打开」策略待定声明 | 需求 #3 界面预演 |
| NotifyFull | 960×600 | 通知/广播全屏：severity 着色 + 左色条 + 来源 chip + 倒计时进度条 + 单按钮 | 需求 #4 之前的全屏态 |

## 附批修改/新增

- **新增**：`qml/LayoutLab.qml`（演示宿主：左轨切五屏）、`qml/layout/*.qml` 五屏、`scripts/_md3_probe.cpp` 加 `--screen/--width/--height`
- **修复**：`Md3SwitchRow.qml` 两行态（带 supporting）标签与说明文字重叠——标签 `y:12`、说明 `y:38`（agent 端同步修 `a1d5070`）
- **提交**：viewer `b4774f9`、agent `a1d5070`

## 附批踩坑（均已写进代码注释）

1. **宿主晚赋值替换 md3 实例 = 渲染事故**：Loader 链路下探针/宿主在 `setSource` 之后才注入 Md3 实例，屏幕创建期 `md3={}`（空）⇒ Behavior 动画时长 `md3.durShort` 变 NaN **永久卡死**（开关轨道填充色停默认白）、空实例子树把宿主内容盖住。**修法**：各屏内部自建 `Md3 { dark: root.dark }`，宿主只传 `dark` 布尔（与手机端 mobile/ 页面同一模式——它们本来就自建所以没炸）。
2. **GridView 委托直接拿 Md3Card 当委托 ⇒ 内容不渲染**（Row/Repeater 里的 Md3Card 没事）。改 plain Item 自绘卡片。
3. **Row 在 Column 里 implicitHeight 实测算成 0** ⇒ 子项全不画（信息行整行消失）。信息行显式 `height: 48`。
4. `-h` 撞 Qt 内置 help 选项 ⇒ 探针窗口尺寸用长选项 `--width/--height`。

## 附批验证

qmllint 零错；`python scripts/_md3_probe.py` **14/14 OK**；五屏 + 深浅变体逐张目检符合 MD3（色阶/字阶/圆角/状态层/severity 着色）。截图：`D:/Stelarith/_probe/shots/layout-*.png`。

## 附批未验证

- 五屏是**布局骨架 + 假数据**，未接真实 backend（DesktopConsole 的设备网格、ExamMode 的座位名单都是静态假数据）
- 真机手感未测；桌面端 Main.qml 仍未逐页迁移（本批只是底稿）
- OOBE 的真实链路（接入码→云端兑换 url+token）未实现，本批只做界面
