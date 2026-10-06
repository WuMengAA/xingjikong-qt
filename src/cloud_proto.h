#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 协议 v1 的**唯一一处**封装修辞（被控端内部两个 TU 共用）。
//
// 为什么单独拎一个头：terminal_console.cpp 以前直接 `extern` 声明 main.cpp 里的
// makeEnvelope，链接时报 LNK2019（跨 TU 依赖这种写法太脆，且签名一改两边就错）。
// 现在两端都 include 这一份声明，定义在 main.cpp，改格式只需认这个地方。
//
// 信封：{"v":1,"type":...,"id":...,"ts":...,"payload":{...}}
// ─────────────────────────────────────────────────────────────────────────────

namespace CloudProto {
constexpr int kVersion = 1;
}

/// 包一个 v1 文本信封（ts 用当前毫秒）。id 一般留空 —— 只有需要配对回执时才带。
QString makeEnvelope(const QString &type, const QJsonObject &payload, const QString &id = QString());
