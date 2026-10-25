// OOBE 配置窗布局模板（560×640 · 需求 #2「离线自动发现与配置」界面预演）
//
// 设计决策（用户需求原文）：「连不上就进等待配置状态，界面给一个短码输入
// （不要 URL / token 两个字段）」——URL/token 收进「高级」折叠区，默认只露接入码。
import QtQuick
import "../components"
import "../components/theme"   // Md3 令牌（pragma Singleton，2026-10-25；dark 由主外壳统一写）
import "../components/md3"

Item {
    id: root
    width: 560; height: 640
    // MD3 令牌：内部自建实例（深/浅由 dark 驱动）。
    // ⚠️ 不许让宿主晚赋值替换整个 md3 实例——创建期 md3 为空会让 Behavior 时长
    //    变 NaN 卡死、颜色停默认值、空实例子树盖住宿主内容（2026-10-14 探针实测）。
    //    与手机端 mobile/ 页面同一模式：页面自建 Md3，宿主只传 dark 布尔。
    property bool dark: false
    readonly property var md3: Md3
    property string code: ""
    property string status: "idle"     // idle | connecting | ok | error
    property bool advanced: false

    // 背板（模拟桌面，凸显窗口）
    Rectangle { anchors.fill: parent; color: md3.cContLowest }
    // 窗口卡（MD3 28 圆角）
    Rectangle {
        anchors.fill: parent
        anchors.margins: 16
        radius: md3.rXL
        color: md3.cContLow
        Column {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.margins: 32
            spacing: 20

            // 标题区
            Row {
                spacing: 14
                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: md3.cPrimaryCont
                    Text { anchors.centerIn: parent; text: "🐋"; font.pixelSize: 22 }
                }
                Column {
                    spacing: 2
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: "配置教室"; font.pixelSize: md3.headlineS; font.weight: Font.Medium; color: md3.cOnSurface }
                    Text { text: "输入管理员发放的接入码，自动连接云端"; font.pixelSize: md3.bodyS; color: md3.cOnSurfaceVar }
                }
            }

            // 接入码输入（唯一必填项）
            Md3TextField {
                md3: root.md3
                width: parent.width
                placeholder: "班级接入码"
                helper: "形如 XJK-XXXX-XXX，由管理员发放；填错可改"
                text: root.code
                onTextChanged: root.code = text
            }

            // 状态行
            Rectangle {
                width: parent.width; height: 44
                radius: md3.rS
                color: root.status === "error" ? md3.cErrorCont
                     : (root.status === "ok" ? root.okContIfAny() : md3.cContHigh)
                visible: root.status !== "idle"
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.status === "connecting" ? "正在连接云端…"
                        : root.status === "ok" ? "✓ 已连接，正在进入教室"
                        : "✗ 接入码无效或云端不可达（错误码 E-OTA-02）"
                    font.pixelSize: md3.bodyM
                    color: root.status === "error" ? md3.cOnErrorCont : md3.cOnSurface
                }
            }

            // 主按钮
            Md3Btn {
                md3: root.md3
                width: parent.width
                text: root.status === "connecting" ? "连接中…" : "保存并连接"
                variant: "filled"
                enabled: root.code.length >= 6 && root.status !== "connecting"
                onClicked: root.status = "connecting"
            }

            // 高级折叠（URL / token 收在这里 —— 默认用户不该看见）
            Column {
                width: parent.width
                spacing: 12
                Row {
                    spacing: 8
                    Text { text: "⚙️"; font.pixelSize: 14 }
                    Text {
                        text: root.advanced ? "收起高级选项" : "展开高级选项"
                        font.pixelSize: md3.labelL; font.weight: Font.Medium; color: md3.cPrimary
                    }
                    MouseArea { anchors.fill: parent; onClicked: root.advanced = !root.advanced }
                }
                Column {
                    width: parent.width
                    spacing: 12
                    visible: root.advanced
                    Md3TextField { md3: root.md3; width: parent.width; placeholder: "云端地址（默认内置）" }
                    Md3TextField { md3: root.md3; width: parent.width; placeholder: "设备令牌（可选）"; password: true }
                }
            }

            Text {
                width: parent.width
                text: "被控端只认接入码：URL 与令牌由接入码在云端兑换，本机不手填。"
                wrapMode: Text.Wrap
                font.pixelSize: md3.bodyS; color: md3.cOutline
            }
        }
    }
    // ok 容器兜底（Md3 令牌没有 ok container，用 outline 组合表达）
    function okContIfAny() { return md3.cContHigh }
}
