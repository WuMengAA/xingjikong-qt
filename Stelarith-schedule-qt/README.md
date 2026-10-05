# 星集控 · 课表编辑器（Qt）

Qt 6 + QML + C++ 课表编辑器，数据模型对齐 ClassIsland 官方 Profile 档案，支持 CSES JSON 导入导出。

## 工程结构

```
Stelarith-schedule-qt/
├── CMakeLists.txt
├── build.ps1                 # 一键构建脚本（配置 + 编译 + windeployqt）
├── README.md
├── src/
│   ├── profile.h                 # 数据模型（Profile/TimeSlot/Subject/ClassPlan/ClassPlanGroup）
│   ├── profile_repository.h/.cpp # 档案仓库（读写 Default.json）
│   ├── schedule_model.h/.cpp     # 课表网格模型（QAbstractTableModel）
│   ├── timeslot_model.h/.cpp     # 时间点模型（QAbstractListModel）
│   ├── subject_model.h/.cpp      # 科目模型（QAbstractListModel）
│   ├── cses_importer.h/.cpp      # CSES 格式导入导出
│   └── main.cpp                  # 入口（QML 注册、示例数据）
└── qml/
    ├── Main.qml                  # 主窗口 + 底部导航
    ├── ScheduleGrid.qml          # 课表网格（TableView + 调课）
    ├── TimeAxis.qml              # 时间轴甘特图（Flickable + Repeater）
    ├── TimeAxisBlock.qml         # 时间块（拖拽手柄，松手写回模型）
    └── SubjectManager.qml        # 科目管理（列表 + 添加/改名/删除）
```

## 构建（已验证）

前置：Qt 6.8.1 + MSVC 18 BuildTools + Ninja + CMake ≥ 3.21 + Windows SDK 10.0.26100.0。

**一键构建**（本机路径已写死在脚本里，换机器需改 `build.ps1` 顶部变量）：

```powershell
pwsh -File build.ps1        # 增量构建
pwsh -File build.ps1 -Clean # 全量重建
```

脚本做的事：
1. 设置 MSVC 编译环境（`INCLUDE` / `LIB` / `PATH`）—— CMake 的 VS 生成器认不到 VS 18 BuildTools，所以用 **Ninja 生成器 + 手工环境变量**
2. `cmake -S . -B build -G Ninja`（`CMAKE_C_COMPILER=cl` / `CMAKE_CXX_COMPILER=cl`）
3. `cmake --build build` → `build\stelarith-schedule-qt.exe`
4. `windeployqt` 部署 Qt 运行时 DLL

**⚠️ 部署注意（QML 应用特有）**：
- 我们的 QML 全部打包在 qrc 资源里（`qt_add_resources` 传统方式），windeployqt 扫描不到 QML import，
  **不会自动部署 QtQuick/QtQml 模块**。首次运行会报 `module "QtQuick" is not installed`。
  解决：手动把 `%QTDIR%\qml\QtQuick` 和 `%QTDIR%\qml\QtQml` 拷到 `build\qml\` 下。
- windeployqt 的 `--compiler-runtime` 可能漏 `msvcp140_2.dll`（Qt6Quick 依赖），
  缺失时进程秒退且退出码 `0xC0000142`。解决：从 `%MSVC%\bin\Hostx64\x64\` 拷贝 msvcp140*.dll + vcruntime140*.dll。

手动构建（按本机实际路径调整）：

```bat
set QTDIR=D:\Qt\6.8.1\msvc2022_64
set MSVC=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231
set SDK=C:\Program Files (x86)\Windows Kits\10
set INCLUDE=%QTDIR%\include;%MSVC%\include;%SDK%\Include\10.0.26100.0\ucrt;%SDK%\Include\10.0.26100.0\um;%SDK%\Include\10.0.26100.0\shared
set LIB=%QTDIR%\lib;%MSVC%\lib\x64;%SDK%\Lib\10.0.26100.0\ucrt\x64;%SDK%\Lib\10.0.26100.0\um\x64
set PATH=%QTDIR%\bin;%MSVC%\bin\Hostx64\x64;%SDK%\bin\10.0.26100.0\x64;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=%QTDIR% -DCMAKE_MAKE_PROGRAM="<ninja路径>" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release
windeployqt build\stelarith-schedule-qt.exe
```

## 运行（已验证）

```bat
build\stelarith-schedule-qt.exe
```

- 主窗口标题：`星集控 · 课表编辑器`，1024×768
- 底部 3 个导航页：课表（网格）/ 时间轴（甘特图）/ 科目（占位）
- 首次运行加载 `%LOCALAPPDATA%/ClassIsland/data/Profiles/Default.json`；文件不存在或为空则生成示例档案（3 节时间点 + 3 个科目 + 2 天课表：周一/周二）
- 命令行参数：`stelarith-schedule-qt.exe --profile <path>`

## 数据模型

对齐 ClassIsland 2.1.0.1 官方 Profile JSON 结构：

| 字段 | 说明 |
|------|------|
| `TimeLayouts` | 时间点字典（TimeSlotId → TimeSlot） |
| `ClassPlans` | 课表字典（ClassPlanId → ClassPlan） |
| `Subjects` | 科目字典（SubjectId → Subject） |
| `ClassPlanGroups` | 课表群字典 |
| `SelectedClassPlanGroupId` | 当前激活课表群 |

每个 `ClassPlan` 带 `WeekDay` / `WeekCountDiv` / `WeekCountDivTotal`，实现多周轮换：

```cpp
bool matchesWeek(int todayWeek) const {
    if (weekCountDivTotal <= 1) return true;
    return ((todayWeek - 1) % weekCountDivTotal) == (weekCountDiv - 1);
}
```

## CSES 导入导出

`CsesImporter` 支持 CSES JSON 格式（subjects / timetable / schedules）：

- `import(path)` / `importData(bytes)` / `importJson(obj)`
- `exportData(profile)` → CSES JSON bytes

## 验证记录（2026-10-06）

**✅ 已验证**：

1. **编译通过**：`cmake --build` exit=0，全部编译目标完成，产物 `stelarith-schedule-qt.exe`（约 134 KB）
2. **GUI 子系统正确**：PE subsystem=2（WIN32_EXECUTABLE），运行时无控制台黑窗
3. **Qt 运行时部署完整**：windeployqt 部署 Qt DLL + platforms/qwindows.dll + qml 目录（含手动拷贝的 QtQuick 全套模块 + MSVC 运行时 msvcp140_2）
4. **程序启动正常**：窗口标题 "星集控 · 课表编辑器"，进程保持运行，退出码 0
5. **QML 加载零错误**：5 个 QML 文件全部加载，无警告无 ReferenceError
6. **UI 渲染确认**：截图像素分析深色 78.7%、蓝色块 258px（#0078d4 课表/导航高亮）、白字 3.5%
7. **科目管理**：列表 + 添加 + 改名 + 删除（被课表引用禁止，isReferenced 已实现）
8. **调课**：ScheduleModel setData/swap 实现，ScheduleGrid 点击格子弹科目菜单（含清空）
9. **时间轴拖拽**：TimeAxisBlock 顶部/底部手柄拖拽，松手吸附 5 分钟写回模型
10. **保存到磁盘**：工具栏"保存"按钮，写回 profilePath 指定文件（自动建目录）
11. **多周轮换**：ScheduleModel 有 currentWeek 属性（默认按日期算，QML 周次下拉可切换），data() 按当前周过滤

**⚠️ 未验证 / 已知限制（如实记账）**：

1. **CSES 导入仅支持 JSON**：未集成 YAML 解析（CSES 官方格式是 YAML）。
2. **撤销/重做未实现**：无 `QUndoStack`。
3. **课表群切换 UI 未暴露**：QML 层没有课表群选择。
4. **Excel 导入导出未实现**。
5. **档案合并冲突**：`mergeClassPlan` 按 GUID 覆盖，未处理科目 ID 冲突。
6. **时间轴拖拽视觉细节**：拖拽中的吸附预览、手柄 hover 反馈等交互细节需要真人操作确认。
7. **周次计算简化**：`weekFromDate` 以 9 月 1 日为学期起点，实际学校学期起点需配置。

## 已知设计决策

- **数据模型独立于 QML**：`Profile` 是纯 C++ struct，通过 `ProfileRepository` 序列化。QML 不直接接触 Profile，只通过 3 个 Model（Schedule/TimeSlot/Subject）。
- **QML 通过 Q_INVOKABLE 访问模型**：C++ 的 `data()`/`headerData()` 虚函数 QML 无法直接调用，`ScheduleModel` 暴露了 `cellText(row, col)` / `rowHeader(row)` / `colHeader(col)` 三个 Q_INVOKABLE 方法。
- **时间点吸附 5 分钟**：`TimeSlotModel::snapTo5()` 在 C++ 层实现，不分散在 QML。
- **防重入标志**：`TimeSlotModel` 和 `SubjectModel` 都有 `m_isUpdating`，避免 QML 双向绑定死循环。
- **示例数据兜底**：`main.cpp` 的 `createSampleProfile()` 在档案为空时生成最小可用示例，方便首次运行看到效果。

## 与星集控的关系

本工程是**独立实验**，不属于 `Stelarith-xingjikong-qt` 的三条主生产线（cloud-ws / control-qt / viewer-qt）。
- 不依赖云端 8788
- 不依赖站点 8090
- 不修改任何现有子仓

数据模型对齐 ClassIsland 官方 Profile，可与 `gen_ci_profile.py` 生成的档案互通。
