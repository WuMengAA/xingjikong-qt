// 系统设置 · 分类「更新」（2026-10-25 从 SettingsPage 拆出）
//
// 状态机在 C++ 的 Updater 里，界面只做三件事：把 updater.message 原样显示、
// 按状态决定按钮文案与灰否、把更新说明摊开。「有没有新版」是云端算的
// （客户端只把本地版本号发过去），界面不自己比版本。
import QtQuick
import QtQuick.Layouts

import "../../components"

SettingsCard {
    id: card

    title: "更新"
    gap: 10

    property var ctx

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        visible: updater.message !== ""
        text: updater.message
        color: updater.state === "failed"
               ? (card.md3 ? card.md3.cOnSurface : "#1C1B1F")
               : (card.md3 ? card.md3.cOnSurfaceVar : "#49454F")
        font.pixelSize: 12
        font.bold: updater.state === "failed"
    }

    Text {
        width: parent.width
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
        visible: updater.state === "available" && updater.notes !== ""
        text: updater.sizeBytes > 0
              ? ("更新说明：" + updater.notes +
                 "（" + (updater.sizeBytes / 1048576).toFixed(1) + " MB）")
              : ("更新说明：" + updater.notes)
        color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
        font.pixelSize: 12
    }

    RowLayout {
        width: parent.width
        spacing: 10

        Btn {
            Layout.preferredWidth: 118
            Layout.preferredHeight: 26
            theme: card.md3
            text: updater.busy ? "处理中…" : "检查更新"
            tip: "检查云端是否有新版本"
            disabled: updater.busy
            onClicked: updater.checkForUpdate()
        }
        // 只在"确实有新版本"时出现 —— 常驻一颗不能点的按钮只会让人犯嘀咕
        Btn {
            Layout.preferredWidth: 118
            Layout.preferredHeight: 26
            theme: card.md3
            visible: updater.state === "available"
            strong: true
            text: updater.mandatory ? "必须更新" : "立即更新"
            tip: "下载、校验、解压、自动重启，全程无需手动操作"
            disabled: updater.busy
            onClicked: updater.installUpdate()
        }
        Btn {
            Layout.preferredWidth: 118
            Layout.preferredHeight: 26
            theme: card.md3
            text: "去官网下载页"
            tip: "打开 www.245959623.xyz/download"
            onClicked: backend.openExternal("https://www.245959623.xyz/download")
        }
    }
}
