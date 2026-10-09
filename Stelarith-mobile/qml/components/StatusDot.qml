// 手机端状态圆点（2026-10-09 · 彩色规范 ok/err/warn）
import QtQuick

Rectangle {
    id: dot

    property int state: 1   // 1=在线(ok) 0=离线(err) 2=待办(warn)
    property var theme: ({})

    width: 10
    height: 10
    radius: 5
    color: {
        if (dot.state === 1) return dot.theme.ok || "#2E9E5B"
        if (dot.state === 2) return dot.theme.warn || "#D4A017"
        return dot.theme.err || "#D64545"
    }
}