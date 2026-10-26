// 批注面板 · 状态行（2026-10-27 · H 批）
//
// 从 AnnotateDialog 拆出：那边 224 行撞了 QML 单文件 200 行红线。
// 拆件本身也有意义 —— 这行"是否在批注 / 第几页 / 多少笔 / 一句提示"以后
// 讲台浮动条也要显示同一份信息，独立成件才好复用。
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: st

    property var theme: null
    property bool ovActive: false
    property int pageIdx: 0
    property int pageCnt: 0
    property int strokes: 0
    property string hint: ""

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: st.ovActive ? "#4ADE80" : (st.theme ? st.theme.fg3 : "#CAC4D0")
        }
        Text {
            text: st.ovActive ? "正在批注" : "未开始"
            color: st.theme ? st.theme.fg : "#E6E1E5"
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Text {
            // 批注跟着 PPT 页码走时 pageIdx 可能为 0（未分页），按第 1 页显示，
            // 否则老师会看到"第 0 页"这种不存在的说法。
            text: st.ovActive
                  ? ("第 " + (st.pageIdx > 0 ? st.pageIdx : 1)
                     + " / " + (st.pageCnt > 0 ? st.pageCnt : 1)
                     + " 页 · " + st.strokes + " 笔")
                  : "落笔不抢课件键盘焦点"
            color: st.theme ? st.theme.fg3 : "#CAC4D0"
            font.pixelSize: 12
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }

    Text {
        Layout.fillWidth: true
        text: st.hint
        color: st.theme ? st.theme.fg3 : "#CAC4D0"
        font.pixelSize: 12
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
}
