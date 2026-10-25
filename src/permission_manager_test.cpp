// PermissionManager 独立单测（不依赖 Qt 事件循环；纯逻辑）。
// 编译/运行见 scripts/_build_perm_test.py；退出码 0 = 全过。
#include "permission_manager.h"

#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
static int g_total = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_total;                                                           \
        if (!(cond)) {                                                       \
            std::printf("FAIL: %s\n", msg);                                  \
            ++g_fail;                                                        \
        } else {                                                             \
            std::printf("ok:   %s\n", msg);                                  \
        }                                                                    \
    } while (0)

static QJsonArray makeScope(const char *type, const char *id) {
    QJsonArray a;
    QJsonObject o;
    o.insert(QStringLiteral("type"), QString::fromUtf8(type));
    o.insert(QStringLiteral("id"), QString::fromUtf8(id));
    a.append(o);
    return a;
}

static QJsonObject devWith(const char *classCode, const char *ip, const char *mac) {
    QJsonObject d;
    d.insert(QStringLiteral("uid"), QStringLiteral("N7"));
    d.insert(QStringLiteral("name"), QStringLiteral("机房1"));
    if (classCode) d.insert(QStringLiteral("classCode"), QString::fromUtf8(classCode));
    if (ip) d.insert(QStringLiteral("ip"), QString::fromUtf8(ip));
    if (mac) d.insert(QStringLiteral("mac"), QString::fromUtf8(mac));
    return d;
}

int main() {
    // 1) 未鉴权 ⇒ 一切拒 + 防御性脱敏（最低权限，不暴露敏感字段）
    {
        PermissionManager pm;
        pm.reset();
        CHECK(!pm.canAccess(QStringLiteral("class_xlzx_2028_08")), "reset: canAccess 拒");
        CHECK(!pm.isPlatformAdmin(), "reset: 非平台超管");
        CHECK(!pm.can(QStringLiteral("device:ip-view")), "reset: can() 拒");
        CHECK(!pm.maskedDevice(devWith("c", "10.0.0.1", "aa:bb")).contains(QStringLiteral("ip")),
              "reset: 防御性脱敏（未鉴权不暴露 ip）");
    }
    // 2) 全权（静态令牌）⇒ 放行 + 不脱敏
    {
        PermissionManager pm;
        pm.setFullAccess();
        CHECK(pm.fullAccess(), "full: fullAccess 真");
        CHECK(pm.canAccess(QStringLiteral("class_x")), "full: canAccess 过");
        CHECK(pm.can(QStringLiteral("device:ip-view")), "full: can() 过（含敏感）");
        CHECK(pm.maskedDevice(devWith("c", "10.0.0.1", "aa:bb")).contains(QStringLiteral("ip")),
              "full: 不脱敏 ip");
    }
    // 3) 平台超管 ⇒ 放行 + 不脱敏
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u1"), QString(), {QStringLiteral("platform_admin")}, QJsonArray());
        CHECK(pm.isPlatformAdmin(), "platform: isPlatformAdmin 真");
        CHECK(pm.canAccess(QStringLiteral("class_x")), "platform: canAccess 过");
        CHECK(pm.maskedDevice(devWith("c", "10.0.0.1", "aa:bb")).contains(QStringLiteral("ip")),
              "platform: 不脱敏");
    }
    // 4) 老师 scope=class 精确
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u2"), QStringLiteral("schoolA"),
                       {QStringLiteral("teacher")}, makeScope("class", "class_xlzx_2028_08"));
        CHECK(pm.canAccess(QStringLiteral("class_xlzx_2028_08")), "teacher: 本班过");
        CHECK(!pm.canAccess(QStringLiteral("class_xlzx_2028_09")), "teacher: 他班拒");
        CHECK(!pm.canAccess(QStringLiteral("")), "teacher: 无归属拒");
        CHECK(pm.can(QStringLiteral("device:control")), "teacher: 有 control");
        CHECK(!pm.can(QStringLiteral("device:ip-view")), "teacher: 无 ip-view");
        CHECK(!pm.can(QStringLiteral("system:settings")), "teacher: 无 settings");
        CHECK(pm.hasRole(QStringLiteral("teacher")), "teacher: hasRole 真");
    }
    // 5) 老师 scope=grade 前缀覆盖
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u3"), QStringLiteral("schoolA"),
                       {QStringLiteral("teacher")}, makeScope("grade", "class_xlzx_2028"));
        CHECK(pm.canAccess(QStringLiteral("class_xlzx_2028_08")), "teacher grade: 届内过");
        CHECK(pm.canAccess(QStringLiteral("class_xlzx_2028_07")), "teacher grade: 届内过2");
        CHECK(!pm.canAccess(QStringLiteral("class_xlzx_2029_01")), "teacher grade: 跨届拒");
    }
    // 6) 老师 scope=school 覆盖全校
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u4"), QStringLiteral("schoolA"),
                       {QStringLiteral("teacher")}, makeScope("school", "class_xlzx"));
        CHECK(pm.canAccess(QStringLiteral("class_xlzx_2028_08")), "teacher school: 全校过");
        CHECK(pm.canAccess(QStringLiteral("class_xlzx_2029_01")), "teacher school: 全校过2");
        CHECK(!pm.canAccess(QStringLiteral("class_other_2028_01")), "teacher school: 外校拒");
    }
    // 7) 脱敏：老师移除 ip/mac，保留其余
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u5"), QStringLiteral("schoolA"),
                       {QStringLiteral("teacher")}, makeScope("class", "class_xlzx_2028_08"));
        QJsonObject m = pm.maskedDevice(devWith("class_xlzx_2028_08", "10.0.0.1", "aa:bb:cc"));
        CHECK(!m.contains(QStringLiteral("ip")), "teacher mask: 删 ip");
        CHECK(!m.contains(QStringLiteral("mac")), "teacher mask: 删 mac");
        CHECK(m.contains(QStringLiteral("uid")), "teacher mask: 留 uid");
        CHECK(m.contains(QStringLiteral("name")), "teacher mask: 留 name");
        QJsonArray arr;
        arr.append(devWith("class_xlzx_2028_08", "10.0.0.1", "aa:bb"));
        QJsonArray out = pm.maskDevices(arr);
        CHECK(out.size() == 1 && !out.at(0).toObject().contains(QStringLiteral("ip")),
              "teacher mask: 批量删 ip");
    }
    // 8) 助教最小化
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u6"), QStringLiteral("schoolA"),
                       {QStringLiteral("assistant")}, makeScope("class", "class_xlzx_2028_08"));
        CHECK(pm.can(QStringLiteral("device:monitor")), "assistant: 有 monitor");
        CHECK(!pm.can(QStringLiteral("device:control")), "assistant: 无 control");
        CHECK(!pm.can(QStringLiteral("device:ip-view")), "assistant: 无 ip-view");
        CHECK(pm.maskedDevice(devWith("c", "10.0.0.1", "aa:bb")).contains(QStringLiteral("ip")) == false,
              "assistant: 脱敏");
    }
    // 9) 未知角色 ⇒ 默认拒
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u7"), QStringLiteral("schoolA"),
                       {QStringLiteral("ghost")}, makeScope("class", "class_xlzx_2028_08"));
        CHECK(!pm.can(QStringLiteral("device:monitor")), "ghost: 默认拒");
        CHECK(!pm.isPlatformAdmin(), "ghost: 非平台超管");
    }
    // 10) setIdentity 后覆盖为 fullAccess（模拟重连换票）
    {
        PermissionManager pm;
        pm.setIdentity(QStringLiteral("u8"), QStringLiteral("schoolA"),
                       {QStringLiteral("teacher")}, makeScope("class", "class_xlzx_2028_08"));
        CHECK(!pm.can(QStringLiteral("system:settings")), "rebind前: teacher 无 settings");
        pm.setFullAccess();
        CHECK(pm.fullAccess(), "rebind: 切全权");
        CHECK(pm.can(QStringLiteral("system:settings")), "rebind: 全权有 settings");
        CHECK(pm.maskedDevice(devWith("c", "10.0.0.1", "aa:bb")).contains(QStringLiteral("ip")),
              "rebind: 全权不脱敏");
    }

    // 11) 集合裁剪 scopedDevices（2026-10-25：设备表在 C++ 单一入口按 scope 裁）
    {
        QJsonArray arr;
        arr.append(devWith("class_xlzx_2028_08", "10.0.0.1", "aa:bb"));
        arr.append(devWith("class_xlzx_2029_01", "10.0.0.2", "aa:cc"));
        arr.append(devWith(nullptr, "10.0.0.3", "aa:dd"));   // 无班级归属

        // 全权：一台都不许少（含无归属的）
        PermissionManager full;
        full.setFullAccess();
        CHECK(full.scopedDevices(arr).size() == 3, "scoped full: 原样 3 台");

        // 老师 scope=class：只留本班，他班与无归属都裁掉
        PermissionManager t;
        t.setIdentity(QStringLiteral("u9"), QStringLiteral("schoolA"),
                      {QStringLiteral("teacher")}, makeScope("class", "class_xlzx_2028_08"));
        QJsonArray tOut = t.scopedDevices(arr);
        CHECK(tOut.size() == 1, "scoped teacher: 只剩本班 1 台");
        CHECK(tOut.at(0).toObject().value(QStringLiteral("classCode")).toString()
                  == QStringLiteral("class_xlzx_2028_08"),
              "scoped teacher: 留下的是本班那台");

        // 平台超管：不裁
        PermissionManager p;
        p.setIdentity(QStringLiteral("u10"), QString(), {QStringLiteral("platform_admin")}, QJsonArray());
        CHECK(p.scopedDevices(arr).size() == 3, "scoped platform: 不裁");

        // 未鉴权（reset）：全部拒（最低权限，与 canAccess 同口径）
        PermissionManager none;
        none.reset();
        CHECK(none.scopedDevices(arr).size() == 0, "scoped reset: 全拒");

        // 空表不炸
        CHECK(none.scopedDevices(QJsonArray()).size() == 0, "scoped: 空表安全");
    }

    if (g_fail == 0) {
        std::printf("\nALL PASS (%d checks)\n", g_total);
        return 0;
    }
    std::printf("\n%d/%d FAILED\n", g_fail, g_total);
    return 1;
}
