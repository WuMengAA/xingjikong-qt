// Step 3 · 确认班级信息（2026-10-20）
// 两条路（扫码 / 输入码）成功后都汇到这里：一个巨大的成功标记 + 班级名 + 机器码。
// 「完成配置，进入系统」= 关向导、加载主界面（C++ 侧 wizardFinished(true)）。
import QtQuick
import ".."

Item {
    id: page
    property var tk: ({})
    property var wizard: null

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)
        spacing: 0

        // 成功标记：主色圆底 + 汉字「✓」不好保证字形，用一个自绘对勾（两条线）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 112
            height: 112
            radius: 56
            color: tk.cPrimaryCont
            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = page.tk.cOnPrimaryCont;
                    ctx.lineWidth = 8;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";
                    ctx.beginPath();
                    ctx.moveTo(32, 58);
                    ctx.lineTo(50, 76);
                    ctx.lineTo(80, 38);
                    ctx.stroke();
                }
            }
        }

        Item { width: 1; height: 28 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "绑定成功！"
            font.pixelSize: 24
            font.weight: Font.Bold
            color: tk.cOnSurface
        }

        Item { width: 1; height: 12 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: oobe.className.length > 0 ? oobe.className : "班级信息已记录"
            font.pixelSize: 32
            font.weight: Font.Bold
            color: tk.cPrimary
        }

        Item { width: 1; height: 20 }

        // 机器码：给老师念给管理员听的一串，不叫 uid（技术词不外露）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            height: 40
            width: machineRow.width + 32
            radius: tk.rS
            color: tk.cCont
            Row {
                id: machineRow
                anchors.centerIn: parent
                spacing: 8
                Text { text: "机器码"; font.pixelSize: 12; color: tk.cOnSurfaceVar; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: oobe.machineCode
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: tk.cOnSurface
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        Item { width: 1; height: 48 }

        OobeBigButton {
            width: parent.width
            tk: page.tk
            text: "完成配置，进入系统"
            onClicked: page.wizard.completeNow()
        }
    }
}
