// 课表下发消息的落地接线（2026-11-02）
//
// 为什么单独一个域文件：这段逻辑本身属于「课表同步」，不是装配。写在 main.cpp 里
// 会让 main 涨行（main 有只降不升的门禁），而这项功能恰恰是必须要有的业务。
//
// 边界（与 §10.9 一致）：
//   · 本文件只做「云端信封 → 落盘 → 热重载 → 回执」这一条流水线，不碰 UI、不发通知；
//   · 三个外部依赖（课表时钟、班级码、回执通道）**全部由 main 注入**，
//     这里不 include main、也不假设任何全局变量存在。
#include "timetable_apply.h"

#include <QJsonObject>
#include <QString>

class ScheduleClock;

namespace TimetableApplyWire {

// 注入点。main 在启动装配阶段调一次（此时时钟可能还没构造，所以用取值函数而非裸指针）。
struct Deps {
    ScheduleClock *(*clock)();                              // 当前课表时钟（可返回 nullptr）
    QString (*classCode)();                                 // 本机班级码（用于回执与日志）
    void (*reply)(const QString &type, const QJsonObject &); // 回云端（>0 台才需要，允许为 nullptr）
};

void init(const Deps &deps);

// 处理一条 `timetable-apply` 消息。返回 true 表示这条消息归本域处理
// （调用方据此决定是否继续往下走）。
bool handle(const QJsonObject &payload);

}  // namespace TimetableApplyWire
