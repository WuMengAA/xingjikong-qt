.pragma library
// 班级显示排序（ClassNav 左栏 + TopBar「全部班级」下拉共用）
//
// 为什么需要：云端 `/api/classes` 按 canonical 码（`cls-<hash>`）排序 —— 那是**身份**
// 不是**顺序**，一个年级 12 个班时会排成 3班/6班/12班/1班… 老师找班要扫一遍。
// 这里只改显示顺序，**不动任何班的码**（改码 = 已绑设备归属当场跳变）。
//
// 规则：名字以数字开头就按数值比（1班 < 2班 < … < 10班 < 12班），
//       非数字名（如「测试班」）排在数字之后，同组内按中文拼音序。
//       没有 name 时退回 code，避免整条消失。

function _key(c) {
    const t = String((c && (c.name || c.code)) || "")
    const m = /^(\d+)/.exec(t)
    return { n: m ? Number(m[1]) : 100000, s: t }
}

/** @param {Array} list backend.classes 或宿主注入的班表（不修改入参） */
function byName(list) {
    const arr = (list || []).slice()
    arr.sort(function (a, b) {
        const ka = _key(a), kb = _key(b)
        return (ka.n - kb.n) || ka.s.localeCompare(kb.s, "zh")
    })
    return arr
}
