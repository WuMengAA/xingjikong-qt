import QtQuick 2.15

// 灵动岛 · G2 圆角容器（2026-10-17）
//
// ── 为什么不用 Rectangle.radius ────────────────────────────────
//   `Rectangle.radius` 画的是**圆弧**：圆弧与直边的接缝处曲率从 0 突跳到 1/r，
//   人眼在浅色/大圆角/大面积上很容易看出"角上有一道折"。这就是用户说的
//   "不符合审美"——不是圆角大小的问题，是**曲率不连续**。
//   G2 = 曲率连续（continuous corner / squircle）：曲率沿边平滑地从 0 增到峰值，
//   视觉上"角是长出来的"，不是"被切掉的"。
//
// ── 为什么用 Canvas 而不是 QtQuick.Shapes ─────────────────────
//   被控端部署里 `QtQuick/Shapes` **只有 DesignHelpers、没有 qmlshapesplugin.dll**，
//   exe 旁也**没有 Qt6QuickShapes.dll**（与 QtQuick.Controls 缺 C++ 后端同一类问题）
//   ⇒ 教室机上 `import QtQuick.Shapes` 会直接失败。Canvas 属 QtQuick 基础类型，可用。
//
// ── 实现：直边 + 四角超椭圆 ───────────────────────────────────
//   超椭圆 |x/a|^n + |y/b|^n = 1 的四分之一段：参数 t∈[0,π/2]
//       X = r·(sin t)^p ,  Y = -r·(cos t)^p ,  p = 2/n
//   n=5 时观感与 iOS squircle 很接近（n→∞ 变直角，n=2 退化成椭圆）。
//   ⚠️ 只在四个角上走曲线，中间是**直线段** —— 不能整体做超椭圆：
//      1280×72 的横幅整体超椭圆会变成"橄榄形"，而不是"圆角矩形"。
//
// ── 兼容性 ────────────────────────────────────────────────────
//   `renderTarget: Canvas.Image`（不是默认的 FramebufferObject）：
//   教室机可能是软件渲染 / 老显卡，Image 模式最不容易出"黑块"。
//   代价是每次重绘要重新光栅化，但岛最大 1280×72~全屏，路径只有 4×steps 段，
//   实测远低于一帧预算。
Item {
    id: surface

    // ── 接口 ──────────────────────────────────────────────────
    property real radius: 18              // 圆角半径（逻辑像素）
    property color fillColor: "#1A1A1C"
    property color strokeColor: "transparent"
    property real strokeWidth: 0          // 0 = 不描边
    // 超椭圆指数：5 ≈ iOS squircle；调到 3 更"圆润"，调到 8 更接近直角
    property real superness: 5.0
    // 每个角的采样段数：10 段在 1x/1.5x 下已看不出多边形
    property int cornerSteps: 10

    default property alias content: host.data

    Behavior on fillColor { ColorAnimation { duration: 200 } }

    Canvas {
        id: cv
        anchors.fill: parent
        renderTarget: Canvas.Image
        antialiasing: true

        onPaint: surface.paintTo(getContext("2d"))
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // 内容宿主：放在 Canvas 之上，保证子项永远可交互（Canvas 不吃事件）
    Item {
        id: host
        anchors.fill: parent
    }

    onRadiusChanged: cv.requestPaint()
    onStrokeWidthChanged: cv.requestPaint()
    onStrokeColorChanged: cv.requestPaint()
    onSupernessChanged: cv.requestPaint()
    onCornerStepsChanged: cv.requestPaint()

    function paintTo(ctx) {
        const w = cv.width
        const h = cv.height
        if (w <= 0 || h <= 0)
            return

        ctx.reset()

        // 描边时路径要内缩半个线宽，否则线会有一半被画布边缘裁掉（观感是"缺角"）
        const sw = surface.strokeWidth
        const ins = sw > 0 ? sw / 2 : 0
        const x0 = ins, y0 = ins, x1 = w - ins, y1 = h - ins
        const rMax = Math.min((x1 - x0) / 2, (y1 - y0) / 2)
        const r = Math.max(0, Math.min(surface.radius, rMax))

        ctx.beginPath()
        if (r < 0.75) {
            ctx.rect(x0, y0, x1 - x0, y1 - y0)
        } else {
            const p = 2.0 / Math.max(2.0, surface.superness)
            const N = Math.max(3, surface.cornerSteps)
            const HALF_PI = Math.PI / 2

            // 顶边 → 右上角
            ctx.moveTo(x1 - r, y0)
            for (let i = 1; i <= N; ++i) {
                const t = HALF_PI * i / N
                ctx.lineTo(x1 - r + r * Math.pow(Math.sin(t), p),
                           y0 + r - r * Math.pow(Math.cos(t), p))
            }
            // 右边 → 右下角
            ctx.lineTo(x1, y1 - r)
            for (let i = 1; i <= N; ++i) {
                const t = HALF_PI * i / N
                ctx.lineTo(x1 - r + r * Math.pow(Math.cos(t), p),
                           y1 - r + r * Math.pow(Math.sin(t), p))
            }
            // 底边 → 左下角
            // ⚠️ 起点必须接住上一条 lineTo 的落点 (x0+r, y1)：所以 t=0 要落在**底边**，
            //    即 x 用 sin、y 用 cos（2026-10-17 这里写反过一次 ⇒ 路径自交，
            //    填充规则把左侧挖空，观感是"左上/左下露白三角"）。
            ctx.lineTo(x0 + r, y1)
            for (let i = 1; i <= N; ++i) {
                const t = HALF_PI * i / N
                ctx.lineTo(x0 + r - r * Math.pow(Math.sin(t), p),
                           y1 - r + r * Math.pow(Math.cos(t), p))
            }
            // 左边 → 左上角
            ctx.lineTo(x0, y0 + r)
            for (let i = 1; i <= N; ++i) {
                const t = HALF_PI * i / N
                ctx.lineTo(x0 + r - r * Math.pow(Math.cos(t), p),
                           y0 + r - r * Math.pow(Math.sin(t), p))
            }
            ctx.closePath()
        }

        ctx.fillStyle = surface.fillColor
        ctx.fill()

        if (sw > 0 && surface.strokeColor.a > 0.001) {
            ctx.lineWidth = sw
            ctx.strokeStyle = surface.strokeColor
            ctx.stroke()
        }
    }
}
