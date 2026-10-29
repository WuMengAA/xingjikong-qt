// 首次启动引导 · 二维码画布（2026-10-20）
// 只负责「把 C++ 给的矩阵画出来」：matrix 是每行一个 "0101…" 字符串的数组。
// ⚠️ 本机不生成二维码：没有编码器就不该画 —— 后端给矩阵才画，给不了就走输入绑定码。
//
// ── 关于这里的两处硬编码 Hex（2026-10-30 改回并说明）──────────────────────
// 「页面/组件禁硬编码 Hex」这条铁律**不适用于本文件**：二维码是**功能图像**，
// 扫码器按"白底黑点"的极性识别，不随主题变。
//   2026-10-30 本轮一度改成 Theme.color.background / textPrimary，
//   结果在深色主题下变成"深底白点"——极性反了，多数扫码器读不出（功能性回归）。
// 同批还留了个更严重的手尾：删掉 `property var tk` 却没删 `onTkChanged`，
//   而 QrCanvas 是被 StepScan 实例化的 ⇒ 整条链报
//   `Cannot assign to non-existent property "onTkChanged"`
//   → StepScan unavailable → **整个 OOBE 向导加载失败**。
//   探针只加载 AgentShell，抓不到这个断层，是 OOBE 探针（STE_OOBE_SHOT=1）暴露的。
import QtQuick

Canvas {
    id: root
    property var matrix: []
    property int cellPad: 16          // 静区（QR 规范要求 4 模块，这里按像素留更宽松）

    onMatrixChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        // 底：白（二维码必须高对比，深色主题下也一样 —— 扫码器按黑白读）
        ctx.fillStyle = "#FFFFFF";
        ctx.fillRect(0, 0, width, height);

        var n = matrix ? matrix.length : 0;
        if (n <= 0) return;

        var side = Math.min(width, height) - cellPad * 2;
        if (side <= 0) return;
        var cell = side / n;
        ctx.fillStyle = "#000000";                 // 码点（纯黑，别用 textPrimary：深色主题下它是白）
        for (var r = 0; r < n; r++) {
            var row = matrix[r];
            for (var c = 0; c < row.length; c++) {
                if (row.charAt(c) !== "1") continue;
                // +0.5 抹掉亚像素缝隙（否则缩放到非整数格会出现浅灰竖线，影响识别）
                ctx.fillRect(cellPad + c * cell, cellPad + r * cell, cell + 0.5, cell + 0.5);
            }
        }
    }
}
