// 星集控被控端 · PPT 域 COM 底层（2026-10-27 从 ppt_domain.cpp 拆出）
// 纪律见 ppt_domain.h 文件头：只用 GetActiveObject（绝不 CoCreateInstance，
// 否则查一次状态就在老师机器上凭空弹 PowerPoint）、每个 IDispatch 都要 Release、
// 失败必须带人话 + HRESULT。

#include "ppt_com.h"
#include "ppt_domain.h"      // Status 的完整定义（readShow 要写它）

#include <QDebug>
#include <QHash>

namespace PptDomain {
namespace detail {

// ── ComScope 的构造函数要写在这里（头里只留声明，避免 windows.h 之外的依赖）──
ComScope::ComScope()
{
    hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    mine = SUCCEEDED(hr);
}

ComScope::~ComScope()
{
    if (mine) ::CoUninitialize();
}

bool ComScope::usable() const
{
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}



// ── COM 套间：本域可能被指令分发（已初始化）或独立探针（未初始化）调用 ──────
// 只做"若我没有初始化，就别去 Uninitialize"的平衡；RPC_E_CHANGED_MODE 说明
// 本线程已被别人按别的模型初始化过（MTA），此时 COM 仍可用，照常继续。

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


// 按偏好顺序探测两个应用，并挑出本次要控的那个。
// Pick 的结构定义在 ppt_com.h（拆文件时别在这里再写一份 —— 会 C2011 重定义）。
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


}   // namespace detail
}   // namespace PptDomain
