// 学生名单 CSV 解析（2026-10-25）
//
// 为什么要单独一个 JS：老师手里的花名册多半是 Excel 另存的 CSV ——
// 逗号/制表符/全角逗号都可能出现，字段还常带引号与换行。把这段放进 QML 面板里
// 会把 200 行红线顶穿，也不便单测。这里只做「文本 → rows」，不碰网络、不碰界面。
//
// 列约定：默认 姓名,学号,座位,备注；**有表头就按表头认列**（姓名/学号/座位/备注，
// 也认 name/no/seat/note），所以老师把列顺序调换过也能导。
// 空姓名行直接跳过（不静默当"无名氏"塞进名单）。

function detectDelim(line) {
    const cands = [",", "\t", ";", "，"]
    let best = ",", bestN = -1
    for (let i = 0; i < cands.length; ++i) {
        const n = line.split(cands[i]).length - 1
        if (n > bestN) { bestN = n; best = cands[i] }
    }
    return best
}

/** 单行切列：支持 "带引号,含逗号" 与 "" 转义。 */
function splitLine(line, d) {
    const out = []
    let cur = ""
    let quoted = false
    for (let i = 0; i < line.length; ++i) {
        const ch = line[i]
        if (quoted) {
            if (ch === '"') {
                if (line[i + 1] === '"') { cur += '"'; ++i }
                else quoted = false
            } else cur += ch
        } else if (ch === '"') {
            quoted = true
        } else if (ch === d) {
            out.push(cur); cur = ""
        } else cur += ch
    }
    out.push(cur)
    return out
}

const HEAD_KEYS = [
    { k: "name", re: /^(姓名|名字|学生|name|student)$/i },
    { k: "no",   re: /^(学号|考号|编号|no|id|studentno|student_no)$/i },
    { k: "seat", re: /^(座位|座号|位置|seat)$/i },
    { k: "note", re: /^(备注|说明|note|remark|memo)$/i },
]

function mapFromHeader(cells) {
    const m = { name: 0, no: 1, seat: 2, note: 3 }
    let hit = 0
    for (let i = 0; i < cells.length; ++i) {
        const v = String(cells[i] || "").trim()
        for (let j = 0; j < HEAD_KEYS.length; ++j) {
            if (HEAD_KEYS[j].re.test(v)) { m[HEAD_KEYS[j].k] = i; ++hit; break }
        }
    }
    return { map: m, hit }
}

/**
 * @returns {{rows:Array<{name,studentNo,seat,note}>, total:number, skipped:number, header:boolean}}
 */
function parse(text) {
    const raw = String(text || "").replace(/\r\n/g, "\n").replace(/\r/g, "\n")
    const lines = raw.split("\n").filter(function (l) { return l.trim() !== "" })
    if (lines.length === 0) return { rows: [], total: 0, skipped: 0, header: false }

    const d = detectDelim(lines[0])
    let start = 0
    let map = { name: 0, no: 1, seat: 2, note: 3 }
    let header = false
    const first = splitLine(lines[0], d)
    const h = mapFromHeader(first)
    // 至少认出 2 个表头列才当表头（否则"姓名"这种单个词也可能是学生名）
    if (h.hit >= 2) { map = h.map; start = 1; header = true }

    const rows = []
    let skipped = 0
    for (let i = start; i < lines.length; ++i) {
        const c = splitLine(lines[i], d)
        const name = String(c[map.name] === undefined ? "" : c[map.name]).trim()
        if (name === "") { ++skipped; continue }
        rows.push({
            name: name,
            studentNo: String(c[map.no] === undefined ? "" : c[map.no]).trim(),
            seat: String(c[map.seat] === undefined ? "" : c[map.seat]).trim(),
            note: String(c[map.note] === undefined ? "" : c[map.note]).trim(),
        })
    }
    return { rows: rows, total: rows.length + skipped, skipped: skipped, header: header }
}
