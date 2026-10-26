// 批注面板 · 页（2026-10-27 · H 批）
//
// 拆出来的理由同 AnnotateToolRow（体积门禁 + 复用）。
//
// 口径：
// · 「新建页」只在白板场景有意义 —— 批注跟着 PPT 走时，页由放映页码决定（I3），
//   老师手点新建一页会得到"和课件对不上的一页"。所以此处**不隐藏**但标注清楚。
// · pageIdx = 0 表示还没分页（被控端空 key 页），显示按第 1 页算，
//   否则老师会看到"第 0 页"这种不存在的说法。
import QtQuick
import QtQuick.Layouts
import "../components"

RowLayout {
    id: row

    property var theme: null
    property int pageIdx: 0
    property int pageCnt: 0

    signal prev()
    signal next()
    signal add()

    spacing: 8

    Text {
        text: "页"
        color: row.theme ? row.theme.fg3 : "#CAC4D0"
        font.pixelSize: 12
    }

    Text {
        text: (row.pageIdx > 0 ? row.pageIdx : 1)
              + " / " + (row.pageCnt > 0 ? row.pageCnt : 1)
        color: row.theme ? row.theme.fg : "#E6E1E5"
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    Btn {
        Layout.preferredWidth: 68
        theme: row.theme
        text: "上一页"
        tip: "在被控端按 Ctrl+PgUp 同样有效"
        onClicked: row.prev()
    }
    Btn {
        Layout.preferredWidth: 68
        theme: row.theme
        text: "下一页"
        tip: "在被控端按 Ctrl+PgDn 同样有效"
        onClicked: row.next()
    }
    Btn {
        Layout.preferredWidth: 68
        theme: row.theme
        text: "新建页"
        tip: "追加一页空白（批注跟着 PPT 页码走时不要用）"
        onClicked: row.add()
    }

    Item { Layout.fillWidth: true }
}
