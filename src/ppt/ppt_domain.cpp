// 星集控被控端 · PPT 放映控制域（PowerPoint / WPS 演示）
// 2026-10-27 · I 批次（第七刀）。设计与纪律见 ppt_domain.h 文件头。
//
// ⚠️ 本文件是全工程**第二处**直接写 COM 的地方（第一处是 screen_capture_dxgi 的
//    D3D11 设备创建，第三处是 main.cpp 的 SAPI）。三条纪律：
//      ① 所有 COM 调用只在**主线程**做（executeAction 就在主线程）；
//      ② 每个 IDispatch 都要有明确的 Release（下面用 releaseDisp 单点回收）；
//      ③ 任何一步失败都要带上**人话 + HRESULT**，不写"失败了"了事 ——
//         老师看到的是"没开 PowerPoint"而不是"错误 0x80004005"。
//
// ⚠️ 为什么不用 CoCreateInstance（与需求原文的差异，此处记账）：
//    需求写的是 `CoCreateInstance(L"PowerPoint.Application")`。实测该调用的语义是
//    **没运行就启动一个**。`ppt_status` 是查状态指令，在老师机器上凭空弹 PowerPoint
//    属于事故级副作用。⇒ 本域改用 `GetActiveObject`（只连已在跑的实例），
//    要启动 PPT 走既有 `launch_app`。行为差异已写进加减归档。

#include "ppt_domain.h"
#include "ppt_com.h"        // COM 底层（2026-10-27 拆出，见该文件头）

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

QString  g_file;                            // ppt.json（偏好持久化）
QString  g_pref = QStringLiteral("auto");   // auto / powerpoint / wps

void loadPref() {
    if (g_file.isEmpty()) return;
    QFile f(g_file);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QString a = o.value(QStringLiteral("app")).toString().trimmed().toLower();
    if (a == QStringLiteral("powerpoint") || a == QStringLiteral("wps") || a == QStringLiteral("auto"))
        g_pref = a;
}

bool savePref() {
    if (g_file.isEmpty()) return false;
    QJsonObject o;
    o.insert(QStringLiteral("app"), g_pref);
    QSaveFile f(g_file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

}   // namespace

namespace PptDomain {

// COM 辅助全在 detail 里（ppt_com.h）；这里 using 一下，调用点不用逐处加前缀。
using namespace detail;


void init(const QString &settingsFile) {
    g_file = settingsFile;
    loadPref();
    qInfo("[agent-qt] PPT 域就绪：偏好=%s（只连已运行的实例，不主动启动）",
          qUtf8Printable(g_pref));
}

QString preferredApp() { return g_pref; }

bool setPreferredApp(const QString &app, QString *error, bool *saved) {
    const QString a = app.trimmed().toLower();
    if (a != QStringLiteral("auto") && a != QStringLiteral("powerpoint") && a != QStringLiteral("wps")) {
        if (error) *error = QStringLiteral("app 只能是 auto / powerpoint / wps（收到「%1」）").arg(app);
        if (saved) *saved = false;
        return false;
    }
    g_pref = a;
    // 落盘失败不影响本次运行（偏好已在内存生效），但如实报 false 让上层能提示
    const bool wrote = savePref();
    if (saved) *saved = wrote;
    if (!wrote && error) *error = QStringLiteral("偏好已生效，但写不进 ppt.json（%1）").arg(g_file);
    return true;
}

Status query() {
    Status s;
    ComScope com;
    if (!com.usable()) {
        s.error = QStringLiteral("COM 初始化失败（%1）").arg(hresultText(com.hr));
        return s;
    }

    Pick p = pickApp(g_pref);
    s.available = p.available;

    if (!p.app) {
        s.error = p.reasons.isEmpty() ? QStringLiteral("没有可用的演示软件")
                                      : p.reasons.join(QStringLiteral("；"));
        return s;
    }
    s.app = p.id;
    s.running = true;

    // 指定了应用但它没运行（另一个可达）⇒ 如实 failed，并说清"要控那个就切偏好"
    if (g_pref != QStringLiteral("auto") && !p.available.contains(g_pref)) {
        s.running = false;
        s.error = QStringLiteral("指定控 %1，但它没在运行；当前可达的是 %2 —— 要控它请把偏好切到 %3")
                      .arg(appLabel(g_pref),
                           p.available.isEmpty() ? QStringLiteral("无") : appLabel(p.available.first()),
                           p.available.isEmpty() ? QStringLiteral("auto") : p.available.first());
        releasePick(&p);
        return s;
    }

    QString err;
    const bool readOk = readShow(p.app, &s, &err);
    releasePick(&p);

    s.ok = readOk;
    if (!readOk) {
        s.error = err.isEmpty() ? QStringLiteral("读不到放映状态") : err;
    } else if (!s.presenting) {
        // 连上了、但没在放映 —— 上层按"如实 failed"口径回报，但状态本身是有效的
        s.error = QStringLiteral("已连上 %1，但当前没有在放映").arg(appLabel(s.app));
    }
    return s;
}

bool step(int delta, Status *out, QString *error) {
    if (delta == 0) {
        if (error) *error = QStringLiteral("step 的 delta 不能为 0");
        return false;
    }
    ComScope com;
    if (!com.usable()) {
        if (error) *error = QStringLiteral("COM 初始化失败（%1）").arg(hresultText(com.hr));
        return false;
    }

    Pick p = pickApp(g_pref);
    if (!p.app) {
        if (error) {
            *error = p.reasons.isEmpty() ? QStringLiteral("没有可用的演示软件")
                                         : p.reasons.join(QStringLiteral("；"));
        }
        return false;
    }
    const QString appId = p.id;

    // 取放映窗口的 View
    IDispatch *wins = nullptr;
    IDispatch *view = nullptr;
    QString err;
    int nWin = 0;
    if (!asDispatch(p.app, L"SlideShowWindows", &wins, &err)) {
        releasePick(&p);
        if (error) *error = err;
        return false;
    }
    if (!asInt(wins, L"Count", &nWin, &err) || nWin <= 0) {
        releaseDisp(wins);
        releasePick(&p);
        if (error) *error = QStringLiteral("%1 没有在放映（先开始放映再翻页）").arg(appLabel(appId));
        return false;
    }
    IDispatch *win = nullptr;
    const bool gotWin = itemAt(wins, 1, &win, &err);
    releaseDisp(wins);
    if (!gotWin || !asDispatch(win, L"View", &view, &err)) {
        releaseDisp(win);
        releasePick(&p);
        if (error) *error = err;
        return false;
    }
    releaseDisp(win);

    VARIANT v;
    const bool called = invoke(view, delta > 0 ? L"Next" : L"Previous",
                               DISPATCH_METHOD, &v, 0, nullptr, &err);
    ::VariantClear(&v);
    releaseDisp(view);
    releasePick(&p);

    if (!called) {
        if (error) *error = err;
        return false;
    }
    // 翻完**重新读一次**真实页码，绝不把请求值回显（翻到最后一页会结束放映）
    const Status after = query();
    if (out) *out = after;
    return true;
}

bool gotoSlide(int index, Status *out, QString *error) {
    if (index < 1) {
        if (error) *error = QStringLiteral("页码从 1 开始（收到 %1）").arg(index);
        return false;
    }
    ComScope com;
    if (!com.usable()) {
        if (error) *error = QStringLiteral("COM 初始化失败（%1）").arg(hresultText(com.hr));
        return false;
    }

    Pick p = pickApp(g_pref);
    if (!p.app) {
        if (error) {
            *error = p.reasons.isEmpty() ? QStringLiteral("没有可用的演示软件")
                                         : p.reasons.join(QStringLiteral("；"));
        }
        return false;
    }
    const QString appId = p.id;

    IDispatch *wins = nullptr;
    IDispatch *view = nullptr;
    QString err;
    int nWin = 0;
    if (!asDispatch(p.app, L"SlideShowWindows", &wins, &err)) {
        releasePick(&p);
        if (error) *error = err;
        return false;
    }
    if (!asInt(wins, L"Count", &nWin, &err) || nWin <= 0) {
        releaseDisp(wins);
        releasePick(&p);
        if (error) *error = QStringLiteral("%1 没有在放映（先开始放映再跳页）").arg(appLabel(appId));
        return false;
    }
    IDispatch *win = nullptr;
    const bool gotWin = itemAt(wins, 1, &win, &err);
    releaseDisp(wins);
    if (!gotWin || !asDispatch(win, L"View", &view, &err)) {
        releaseDisp(win);
        releasePick(&p);
        if (error) *error = err;
        return false;
    }
    releaseDisp(win);

    VARIANT arg;
    ::VariantInit(&arg);
    arg.vt = VT_I4;
    arg.lVal = index;
    VARIANT v;
    const bool called = invoke(view, L"GotoSlide", DISPATCH_METHOD, &v, 1, &arg, &err);
    ::VariantClear(&v);
    releaseDisp(view);
    releasePick(&p);

    if (!called) {
        if (error) *error = err;
        return false;
    }
    const Status after = query();
    if (out) *out = after;
    return true;
}

/* ── 2026-10-27（用户点单「PPT自动打开」）────────────────────────
 * 让**已在运行**的演示软件进入放映。三条限制都是为了不制造事故：
 *   ① 不 CoCreateInstance、不启动进程 —— 见文件头红线。老师没开 PPT 就什么都不做；
 *   ② 不打开文件 —— 本域不知道课件在哪，"自动打开某个 pptx"不属这里的能力；
 *   ③ 已经在放映 → 直接成功返回，**不重放**（重放会跳回第一页，
 *      把老师讲到一半的位置清掉，是不可挽回的现场事故）。
 */
bool autoStartShow(Status *out, QString *error) {
    ComScope com;
    if (!com.usable()) {
        if (error) *error = QStringLiteral("COM 初始化失败（%1）").arg(hresultText(com.hr));
        return false;
    }

    Pick p = pickApp(g_pref);
    if (!p.app) {
        if (error) {
            *error = p.reasons.isEmpty() ? QStringLiteral("没有演示软件在运行")
                                         : p.reasons.join(QStringLiteral("；"));
        }
        return false;
    }

    Status s;
    s.app = p.id;
    s.running = true;
    QString err;
    if (!readShow(p.app, &s, &err)) {
        releasePick(&p);
        if (error) *error = err;
        return false;
    }
    if (s.presenting) {                 // ③ 已在放映：什么也不动
        releasePick(&p);
        if (out) *out = s;
        return true;
    }

    // 已运行但没放映 ⇒ ActivePresentation.SlideShowSettings.Run()
    IDispatch *pres = nullptr;
    if (!asDispatch(p.app, L"ActivePresentation", &pres, &err)) {
        const QString label = appLabel(s.app);
        releasePick(&p);
        if (error) {
            *error = QStringLiteral("%1 里没有打开的课件（先打开课件，再点批注）").arg(label);
        }
        return false;
    }

    bool ok = false;
    IDispatch *ss = nullptr;
    if (!asDispatch(pres, L"SlideShowSettings", &ss, &err)) {
        if (error) *error = err;
    } else {
        VARIANT v;
        ok = invoke(ss, L"Run", DISPATCH_METHOD, &v, 0, nullptr, &err);
        ::VariantClear(&v);
        releaseDisp(ss);
        if (!ok && error) *error = err;
    }
    releaseDisp(pres);
    releasePick(&p);

    if (!ok) return false;

    // Run 之后放映窗口要几百毫秒才建起来，这里**不**同步等待：
    // 回报"已发起放映"，真实页码由下一次 query() 拉准（step 那边也是这个口径）。
    s.presenting = true;
    s.slide = 1;
    if (out) *out = s;
    return true;
}

}   // namespace PptDomain
