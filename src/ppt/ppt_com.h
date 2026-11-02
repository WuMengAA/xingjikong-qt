#pragma once
// PPT 域的 COM 底层（2026-10-27 从 ppt_domain.cpp 拆出）
//
// 为什么拆：加「批注自动放映」后 ppt_domain.cpp 到 665 行，撞「实现文件 ≤ 600 行」红线。
// 拆口选在 **COM 机制 / 业务语义** 之间 —— 这里只管"怎么跟 OLE 说话"，
// 不判断"该不该翻页""要不要自动放映"（那些留在 ppt_domain.cpp）。
//
// ⚠️ windows.h 只在这里出现：ppt_domain.cpp 因此不再直接被 COM 头污染。
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <QString>
#include <QStringList>

namespace PptDomain {

struct Status;      // 定义在 ppt_domain.h

namespace detail {

// ── COM 套间：本域可能被指令分发（已初始化）或独立探针（未初始化）调用 ──────
// 只做"若我没有初始化，就别去 Uninitialize"的平衡；RPC_E_CHANGED_MODE 说明
// 本线程已被别人按别的模型初始化过（MTA），此时 COM 仍可用，照常继续。
struct ComScope {
    HRESULT hr = E_FAIL;
    bool    mine = false;       // CoInitializeEx 成功（S_OK / S_FALSE）⇒ 由我平衡
    ComScope();
    ~ComScope();
    bool usable() const;
};

void    releaseDisp(IDispatch *&p);
QString hresultText(HRESULT hr);

// 统一的 Invoke：名字 → DISPID → Invoke，异常时把 EXCEPINFO 的描述捞出来。
// out 由调用方 VariantClear（本函数只负责 VariantInit）。
bool invoke(IDispatch *obj, const wchar_t *name, WORD flags, VARIANT *out,
            int nArgs, VARIANT *args, QString *err);

bool asDispatch(IDispatch *obj, const wchar_t *name, IDispatch **out, QString *err);
bool asInt(IDispatch *obj, const wchar_t *name, int *out, QString *err);
bool asText(IDispatch *obj, const wchar_t *name, QString *out, QString *err);
bool itemAt(IDispatch *coll, int index, IDispatch **out, QString *err);

QString appLabel(const QString &id);

// 把放映实况读进 Status（presenting / slide / total / presentation）
bool readShow(IDispatch *app, Status *s, QString *err);

// 按偏好顺序探测两个应用，并挑出本次要控的那个。
struct Pick {
    QString id;
    IDispatch *app = nullptr;
    QStringList available;
    QStringList reasons;
};
Pick pickApp(const QString &pref);
void releasePick(Pick *p);

}   // namespace detail
}   // namespace PptDomain
