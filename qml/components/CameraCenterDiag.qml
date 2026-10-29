// 监控中心 · 「诊断」页（2026-10-29 抽出）
// 设备连接状态 / 绑定 / 摄像头枚举 / 最近错误（人话）+ 错误码 / 重试 + 画面说明。
// 全部经 `cc` 取状态/调逻辑。
import QtQuick
import QtQuick.Layouts
import "../island"
import "md3"

Rectangle {
    id: diagCard
    property var cc
    anchors.fill: parent
    color: cc.tCard
    radius: cc.tRM
    Flickable {
        anchors.fill: parent
        anchors.margins: cc.tSpM
        contentHeight: diagCol.height
        clip: true
        Column {
            id: diagCol
            width: parent.width
            spacing: cc.tSpM
            Row { spacing: cc.tSpS
                Text { text: "选中设备"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                Text { text: cc.selUid !== "" ? cc.deviceName(cc.selUid) : "（未选设备）"; color: cc.tFg; font.pixelSize: cc.tLabelM }
            }
            Row { spacing: cc.tSpS
                Text { text: "当前绑定"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                Text { text: cc.boundDevice() !== "" ? cc.boundDevice()
                            : (cc.currentDev() !== "" ? cc.currentDev() + "（本地缓存）" : "（未绑定，默认 video0）")
                       color: cc.tFg; font.pixelSize: cc.tLabelM; elide: Text.ElideRight; width: parent.width - 120 }
            }
            Row { spacing: cc.tSpS
                Text { text: "摄像头"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                Text { text: cc.loadingCams ? "枚举中…" : (cc.cameras.length + " 个"); color: cc.tFg; font.pixelSize: cc.tLabelM }
            }
            Column { spacing: 2
                Text { text: "最近状态"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                Text { text: cc.status !== "" ? cc.status : "（无）"
                       color: cc.tFg; font.pixelSize: cc.tLabelM; width: parent.width; wrapMode: Text.WrapAnywhere }
            }
            Row { visible: cc.errCode !== ""
                spacing: cc.tSpS
                Text { text: "错误码"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                Text { text: cc.errCode; color: cc.tFg; font.pixelSize: cc.tLabelM }
            }
            Md3Btn {
                visible: cc.canRetry
                radius: cc.tRM
                variant: "tonal"
                text: "重试"
                tip: "再试一次刚才没成功的操作"
                onClicked: cc.retryLast()
            }

            // ── 画面说明（MD3 重构④：从实时页移来的长文字）──
            Rectangle { width: parent.width; height: 1; color: cc.tOutlineVar }
            Text { text: "画面说明"; color: cc.tFg3; font.pixelSize: cc.tLabelM; font.weight: Font.Medium }
            Text {
                width: parent.width
                wrapMode: Text.WrapAnywhere
                color: cc.tFg3
                font.pixelSize: cc.tLabelS
                text: "· 「实时」页两个画面格：左侧是被控端**屏幕**帧，右侧是**摄像头**实时流。\n"
                      + "· 占位「连接中...」= 已经请求、还没收到首帧；「暂无画面」= 没选设备 / 没请求流 / 被控端没有摄像头。\n"
                      + "· 摄像头格可见时会按需请被控端推流，不可见自动停（隐私红线：不看不采）。\n"
                      + "· 虚拟摄像头（OBS / Iriun 之类）能列出来但采不到画面，列表里会标「（虚拟·无画面）」。\n"
                      + "· 自检/抓拍/开录都会优先用「当前绑定」的摄像头；没绑定时由被控端自己回落 video0。"
            }
        }
    }
}
