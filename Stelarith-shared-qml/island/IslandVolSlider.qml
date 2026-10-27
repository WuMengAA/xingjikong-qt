import QtQuick 2.15
import "."

// 灵动岛 · 通用音量滑块（2026-10-27 · C 批）
//
// 不引 QtQuick.Controls（岛全系不用 Controls，避免与自绘 MD3 风格冲突），
// 自己用 MouseArea + Rectangle 画一条轨道 + 手柄。
//
// value：0..100（外部拖动时只推不拉，避免与绑定回写互相打架——见 IslandSettingsFace）。
// 颜色随主题（th.light 决定轨道底色），填充色固定蓝（深浅背景都看得清）。
//
// ⚠️ 不要声明 signal valueChanged：value 是属性，QML 会自动生成 valueChanged 信号，
//   再显式声明一个会触发 "Duplicate signal name"（实测让整个类型加载失败，连带
//   引用它的 IslandSettingsFace 也 unavailable）。要监听变化直接用 onValueChanged，
//   在 handler 里读 value 属性即可。
Item {
    id: slider
    property int value: 75
    property var th: null

    height: 28
    implicitHeight: 28

    readonly property color _track: (slider.th && slider.th.light) ? "#D0D0D0" : "#3C3C3C"
    readonly property color _fill: "#1E88E5"

    Item {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 4

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: slider._track
        }
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: 4; radius: 2
            width: Math.max(0, Math.min(parent.width, parent.width * (slider.value / 100.0)))
            color: slider._fill
        }
    }

    Rectangle {
        id: handle
        width: 16; height: 16; radius: 8
        color: "#FFFFFF"
        border.width: 2; border.color: slider._fill
        anchors.verticalCenter: track.verticalCenter
        x: {
            const w = track.width
            if (w <= 0) return 0
            const cx = w * (slider.value / 100.0)
            return Math.max(0, Math.min(w - width, cx - width / 2))
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: update(mouse.x)
        onPositionChanged: if (pressed) update(mouse.x)
        function update(mx) {
            const w = track.width
            if (w <= 0) return
            let v = Math.round((mx / w) * 100)
            v = Math.max(0, Math.min(100, v))
            // 赋属性即触发自动生成的 valueChanged 信号（handler 里读 value 属性）
            if (v !== slider.value) slider.value = v
        }
    }
}
