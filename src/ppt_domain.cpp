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

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include "ppt_domain.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

// ── COM 套间：本域可能被指令分发（已初始化）或独立探针（未初始化）调用 ──────
// 只做"若我没有初始化，就别去 Uninitialize"的平衡；RPC_E_CHANGED_MODE 说明
// 本线程已被别人按别的模型初始化过（MTA），此时 COM 仍可用，照常继续。
struct ComScope {
    HRESULT hr = E_FAIL;
    bool    mine = false;       // CoInitializeEx 成功（S_OK / S_FALSE）⇒ 由我平衡
    ComScope() {
        hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        mine = SUCCEEDED(hr);
    }
    ~ComScope() { if (mine) ::CoUninitialize(); }
    bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

void releaseDisp(IDispatch *&p) {
    if (p) { p->Release(); p = nullptr; }
}

QString hresultText(HRESULT hr) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

QString bstrText(const BSTR b) {
    if (!b) return QString();
    const int n = static_cast<int>(::SysStringLen(b));
    return QString::fromWCharArray(b, n);
}

// 统一的 Invoke：名字 → DISPID → Invoke，异常时把 EXCEPINFO 的描述捞出来。
// out 由调用方 VariantClear（本函数只负责 VariantInit）。
bool invoke(IDispatch *obj, const wchar_t *name, WORD flags, VARIANT *out,
            int nArgs, VARIANT *args, QString *err) {
    ::VariantInit(out);
    if (!obj) {
        if (err) *err = QStringLiteral("COM 对象为空");
        return false;
    }
    LPOLESTR names[1] = { const_cast<LPOLESTR>(name) };
    DISPID dispid = DISPID_UNKNOWN;
    HRESULT hr = obj->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) {
        if (err) {
            *err = QStringLiteral("找不到成员 %1（%2）")
                       .arg(QString::fromWCharArray(name), hresultText(hr));
        }
        return false;
    }

    DISPPARAMS dp = {};
    dp.cArgs = static_cast<UINT>(nArgs);
    dp.rgvarg = args;              // ⚠️ DISPPARAMS 实参是逆序；本域最多传 1 个，无歧义

    EXCEPINFO ei;
    ::ZeroMemory(&ei, sizeof(ei));
    UINT argErr = 0;
    hr = obj->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, out, &ei, &argErr);
    if (hr == DISP_E_EXCEPTION) {
        QString src = bstrText(ei.bstrSource);
        QString desc = bstrText(ei.bstrDescription);
        if (err) {
            *err = QStringLiteral("%1 抛异常：%2%3")
                       .arg(QString::fromWCharArray(name),
                            src.isEmpty() ? QString() : src + QStringLiteral(" / "),
                            desc.isEmpty() ? QStringLiteral("（无描述）") : desc);
        }
        if (ei.bstrDescription) ::SysFreeString(ei.bstrDescription);
        if (ei.bstrSource)      ::SysFreeString(ei.bstrSource);
        if (ei.bstrHelpFile)    ::SysFreeString(ei.bstrHelpFile);
        return false;
    }
    if (FAILED(hr)) {
        if (err) {
            *err = QStringLiteral("%1 调用失败（%2）")
                       .arg(QString::fromWCharArray(name), hresultText(hr));
        }
        return false;
    }
    return true;
}

// 取子对象（属性返回 IDispatch）。成功时 *out 由调用方 releaseDisp。
bool asDispatch(IDispatch *obj, const wchar_t *name, IDispatch **out, QString *err) {
    *out = nullptr;
    VARIANT v;
    QString why;
    if (!invoke(obj, name, DISPATCH_PROPERTYGET, &v, 0, nullptr, &why)) {
        if (err) *err = why;
        return false;
    }
    bool ok = false;
    if (v.vt == VT_DISPATCH && v.pdispVal) {
        v.pdispVal->AddRef();
        *out = v.pdispVal;
        ok = true;
    } else if (v.vt == VT_UNKNOWN && v.punkVal) {
        IDispatch *d = nullptr;
        if (SUCCEEDED(v.punkVal->QueryInterface(IID_IDispatch, reinterpret_cast<void **>(&d)))) {
            *out = d;
            ok = true;
        }
    }
    ::VariantClear(&v);
    if (!ok && err) {
        *err = QStringLiteral("%1 不是 COM 对象（vt=%2）")
                   .arg(QString::fromWCharArray(name)).arg(static_cast<int>(v.vt));
    }
    return ok;
}

bool asInt(IDispatch *obj, const wchar_t *name, int *out, QString *err) {
    VARIANT v;
    QString why;
    if (!invoke(obj, name, DISPATCH_PROPERTYGET, &v, 0, nullptr, &why)) {
        if (err) *err = why;
        return false;
    }
    bool ok = true;
    switch (v.vt) {
    case VT_I4:   *out = static_cast<int>(v.lVal);  break;
    case VT_I2:   *out = static_cast<int>(v.iVal);  break;
    case VT_UI4:  *out = static_cast<int>(v.ulVal); break;
    case VT_R8:   *out = static_cast<int>(v.dblVal); break;
    case VT_BOOL: *out = (v.boolVal != VARIANT_FALSE) ? 1 : 0; break;
    case VT_BSTR: *out = bstrText(v.bstrVal).toInt(); break;
    default:      ok = false; break;
    }
    ::VariantClear(&v);
    if (!ok && err) {
        *err = QStringLiteral("%1 不是整数（vt=%2）")
                   .arg(QString::fromWCharArray(name)).arg(static_cast<int>(v.vt));
    }
    return ok;
}

bool asText(IDispatch *obj, const wchar_t *name, QString *out, QString *err) {
    VARIANT v;
    QString why;
    if (!invoke(obj, name, DISPATCH_PROPERTYGET, &v, 0, nullptr, &why)) {
        if (err) *err = why;
        return false;
    }
    bool ok = (v.vt == VT_BSTR);
    if (ok) *out = bstrText(v.bstrVal);
    ::VariantClear(&v);
    if (!ok && err) *err = QStringLiteral("%1 不是字符串").arg(QString::fromWCharArray(name));
    return ok;
}

// 集合取元素：`Item(index)`。PowerPoint 里它是 property 也是 method，
// 两个 flag 一起传，避免不同版本/不同实现（WPS）只认其中一种。
bool itemAt(IDispatch *coll, int index, IDispatch **out, QString *err) {
    *out = nullptr;
    VARIANT arg;
    ::VariantInit(&arg);
    arg.vt = VT_I4;
    arg.lVal = index;

    VARIANT v;
    QString why;
    if (!invoke(coll, L"Item", DISPATCH_METHOD | DISPATCH_PROPERTYGET, &v, 1, &arg, &why)) {
        if (err) *err = why;
        return false;
    }
    bool ok = false;
    if (v.vt == VT_DISPATCH && v.pdispVal) {
        v.pdispVal->AddRef();
        *out = v.pdispVal;
        ok = true;
    }
    ::VariantClear(&v);
    if (!ok && err) *err = QStringLiteral("Item(%1) 没拿到对象").arg(index);
    return ok;
}

QString appLabel(const QString &id) {
    if (id == QStringLiteral("powerpoint")) return QStringLiteral("PowerPoint");
    if (id == QStringLiteral("wps"))        return QStringLiteral("WPS 演示");
    return id;
}

const wchar_t *progIdOf(const QString &id) {
    return (id == QStringLiteral("wps")) ? L"KWPP.Application" : L"PowerPoint.Application";
}

// 连**已在运行**的实例。why 说明为什么连不上（未安装 / 未运行），供上层如实回报。
bool attachOne(const QString &id, IDispatch **out, QString *why) {
    *out = nullptr;
    CLSID clsid;
    HRESULT hr = ::CLSIDFromProgID(progIdOf(id), &clsid);
    if (FAILED(hr)) {
        if (why) *why = QStringLiteral("未安装（ProgID 未注册，%1）").arg(hresultText(hr));
        return false;
    }
    IUnknown *unk = nullptr;
    hr = ::GetActiveObject(clsid, nullptr, &unk);   // ⚠️ 不是 CoCreateInstance：不拉起程序
    if (FAILED(hr) || !unk) {
        if (why) {
            // 实测（2026-10-27）：只开 PowerPoint 的"开始屏幕"、不打开任何文稿时，
            // 它**不进** COM 运行对象表（GetActiveObject → 0x800401e3 MK_E_UNAVAILABLE）。
            // 所以这里的措辞要把"没打开文稿"说出来，否则老师会以为 PPT 没装/没开。
            *why = QStringLiteral("未运行或没打开演示文稿（PowerPoint 只在有文稿打开时才登记到 "
                                  "COM 运行对象表，%1）").arg(hresultText(hr));
        }
        return false;
    }
    IDispatch *disp = nullptr;
    hr = unk->QueryInterface(IID_IDispatch, reinterpret_cast<void **>(&disp));
    unk->Release();
    if (FAILED(hr) || !disp) {
        if (why) *why = QStringLiteral("COM 对象不支持 IDispatch（%1）").arg(hresultText(hr));
        return false;
    }
    *out = disp;
    return true;
}

// 读放映实况。返回 false 只代表**读了但读不全**（如放映窗口在却取不到页码）。
bool readShow(IDispatch *app, PptDomain::Status *s, QString *err) {
    IDispatch *wins = nullptr;
    if (!asDispatch(app, L"SlideShowWindows", &wins, err)) return false;

    int nWin = 0;
    if (!asInt(wins, L"Count", &nWin, err)) {
        releaseDisp(wins);
        return false;
    }
    if (nWin <= 0) {                 // 没在放映（这是**有效状态**，不是错误）
        releaseDisp(wins);
        s->presenting = false;
        s->slide = 0;
        s->total = 0;
        return true;
    }

    IDispatch *win = nullptr;
    if (!itemAt(wins, 1, &win, err)) {   // 多放映窗口不处理：真实课堂只有一块屏
        releaseDisp(wins);
        return false;
    }
    releaseDisp(wins);

    IDispatch *view = nullptr;
    if (!asDispatch(win, L"View", &view, err)) {
        releaseDisp(win);
        return false;
    }
    releaseDisp(win);

    IDispatch *slide = nullptr;
    if (!asDispatch(view, L"Slide", &slide, err)) {
        releaseDisp(view);
        return false;
    }

    int idx = 0;
    if (asInt(slide, L"SlideIndex", &idx, nullptr)) s->slide = idx;

    // 总页数 / 文稿名要从 Slide → Parent(Presentation) 拿。
    // 拿不到不算致命（页码才是老师要的），故这里**不写 err**。
    IDispatch *pres = nullptr;
    if (asDispatch(slide, L"Parent", &pres, nullptr)) {
        IDispatch *slides = nullptr;
        if (asDispatch(pres, L"Slides", &slides, nullptr)) {
            int total = 0;
            if (asInt(slides, L"Count", &total, nullptr)) s->total = total;
            releaseDisp(slides);
        }
        QString name;
        if (asText(pres, L"Name", &name, nullptr)) s->presentation = name;
        releaseDisp(pres);
    }

    releaseDisp(slide);
    releaseDisp(view);

    s->presenting = true;
    if (s->slide <= 0) {             // 放映中却读不到页码 ⇒ 如实报，不拿 0 当"第 1 页"
        if (err) *err = QStringLiteral("取不到当前页码（放映窗口在，但 View.Slide.SlideIndex 读不出来）");
        return false;
    }
    return true;
}

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

// 按偏好顺序探测两个应用，并挑出本次要控的那个。
// 返回选中的应用 id（空 = 一个都没连上），why 收集两边的失败原因。
struct Pick {
    QString id;
    IDispatch *app = nullptr;
    QStringList available;
    QStringList reasons;
};

Pick pickApp(const QString &pref) {
    Pick p;
    const QStringList both = { QStringLiteral("powerpoint"), QStringLiteral("wps") };
    QHash<QString, IDispatch *> attached;
    for (const QString &id : both) {
        IDispatch *d = nullptr;
        QString why;
        if (attachOne(id, &d, &why)) {
            attached.insert(id, d);
            p.available << id;
        } else {
            p.reasons << appLabel(id) + QStringLiteral("：") + why;
        }
    }

    QStringList order;
    if (pref == QStringLiteral("powerpoint") || pref == QStringLiteral("wps"))
        order << pref;                      // 指定了就不看另一个（绝不悄悄切换）
    else
        order << QStringLiteral("powerpoint") << QStringLiteral("wps");

    for (const QString &id : order) {
        if (attached.contains(id)) {
            p.id = id;
            p.app = attached.take(id);
            break;
        }
    }
    // 没被选中的那个：释放，别泄漏
    for (auto it = attached.begin(); it != attached.end(); ++it) releaseDisp(it.value());
    return p;
}

void releasePick(Pick *p) {
    if (p) releaseDisp(p->app);
}

}   // namespace

namespace PptDomain {

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
