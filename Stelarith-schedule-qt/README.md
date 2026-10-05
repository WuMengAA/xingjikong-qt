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
    ├── ScheduleGrid.qml          # 课表网格（TableView）
    ├── TimeAxis.qml              # 时间轴甘特图（ListView）
    └── TimeAxisBlock.qml         # 时间块（拖拽手柄）
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

1. **编译通过**：`cmake --build` exit=0，11/11 编译目标全部完成，产物 `stelarith-schedule-qt.exe`（约 122 KB）
2. **GUI 子系统正确**：PE subsystem=2（WIN32_EXECUTABLE），运行时无控制台黑窗
3. **Qt 运行时部署完整**：windeployqt 部署 10 个 Qt DLL + platforms/qwindows.dll + qml 目录
4. **程序启动正常**：窗口标题 "星集控 · 课表编辑器" 正常显示，进程保持运行不崩溃
5. **QML 加载无错误**：4 个 QML 文件全部编译并加载成功

**⚠️ 未验证 / 已知限制（如实记账）**：

1. **窗口渲染内容未截图确认**：本会话无屏幕会话可交互截图，只确认了窗口创建成功、进程存活、无 QML 报错。UI 视觉细节（网格颜色、时间轴块布局）需要真人打开确认。
2. **拖拽保存未实现**：`TimeAxisBlock.qml` 的 `onReleased` 是空实现（TODO 标注），时间轴拖拽只改视觉不改数据。
3. **ScheduleModel::setData / swap 未实现**：调课/交换科目逻辑是空壳（TODO）。
4. **SubjectModel::isReferenced 未实现**：科目删除保护永远返回 false（TODO）。
5. **CSES 导入仅支持 JSON**：未集成 YAML 解析（CSES 官方格式是 YAML）。
6. **多周轮换未接当前周次**：`ScheduleModel::data()` 里 `matchesWeek(1)` 写死第 1 周。
7. **撤销/重做未实现**：无 `QUndoStack`。
8. **课表群切换 UI 未暴露**：QML 层没有课表群选择。
9. **Excel 导入导出未实现**。
10. **档案合并冲突**：`mergeClassPlan` 按 GUID 覆盖，未处理科目 ID 冲突。
11. **科目管理页是占位**：Main.qml 第 3 页显示"科目管理（待实现）"。

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
