// 班级工作台 · 右栏下「班级动态」时间线（#461-#472，2026-10-24 清新极简改版）
//
// 数据真源：ClassEventModel（typed，#460，新在头、id 水位去重、cap 200）——
// 直接当 ListView 的 model。thumbnail 事件不进本模型（那是画面数据不是动态，
// ClassService 侧已分流）⇒ 这里出现的每一条都是"班里发生了什么事"。
//
// kind → 人话映射（教育 B 端规范：英文 kind 不许裸奔上界面）：
//   member.online  → 设备上线（cOk）
//   member.offline → 设备离线（cOutline）
//   其余           → 班级事件（cInfo，附 detail 里的人话）
// 时间：at 是 ISO 8601 原文（云端权威），本组件负责格式化成 HH:mm；
//       解析失败（脏数据）显示 "——" 而不崩、不显示 Invalid Date。
import QtQuick
import QtQuick.Controls          // ScrollBar
import "../../components/fresh"    // FreshCard
import "../../island"               // SvgIcon
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: pane

    property var model: null      // ClassEventModel

    /** ISO 8601 → "HH:mm"；解析不了给 "——"（宁缺毋滥，不显示 Invalid Date）。 */
    function hhmm(at) {
        if (!at) return "——"
        const d = new Date(at)
        if (isNaN(d.getTime())) return "——"
        const p = function (n) { return (n < 10 ? "0" : "") + n }
        return p(d.getHours()) + ":" + p(d.getMinutes())
    }

    /** kind → { icon, tint, label }（tint 用令牌名，delegate 里解析成色值）。 */
    function kindStyle(kind, isMember) {
        if (kind === "member.online")  return { icon: "circle-check",  tint: "ok",   label: "设备上线" }
        if (kind === "member.offline") return { icon: "circle-dashed", tint: "out",  label: "设备离线" }
        if (kind === "publish")        return { icon: "tv",            tint: "info", label: "已发布" }
        if (isMember === true)         return { icon: "circle-dot",    tint: "info", label: "设备动态" }
        return { icon: "info", tint: "info", label: "班级事件" }
    }

    /** 第二行：publish 只给标题（from 是内部标识，对老师无意义）；
     *  其余保持 "uid · 原因" 口径（离线原因等要能看见）。 */
    function subText(kind, uid, detail) {
        if (kind === "publish") return detail || ""
        if ((uid || "") === "") return detail || ""
        return (detail || "") !== "" ? (uid + " · " + detail) : uid
    }

    FreshCard {
        anchors.fill: parent

        ListView {
            id: list
            anchors.fill: parent
            model: pane.model
            clip: true
            spacing: 4
            cacheBuffer: 400

            delegate: Rectangle {
                width: list.width
                height: row.implicitHeight + 12
                radius: Theme.radius.md
                color: evMa.containsMouse ? Theme.color.surfaceVariant || "#EEF0F3" : "transparent"

                Row {
                    id: row
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8

                    // 时间（等宽，事件流的"刻度"）
                    Text {
                        text: pane.hhmm(model.at)
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: Theme.color.textSecondary || "#6B7280"
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    SvgIcon {
                        property var st: pane.kindStyle(model.kind, model.isMember)
                        name: st.icon
                        px: 14
                        tint: st.tint === "ok" ? (Theme.color.success || "#22C55E")
                              : st.tint === "out" ? (Theme.color.textSecondary || "#9CA3AF")
                              : (Theme.color.textSecondary || "#3B82F6")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Column {
                        spacing: 1
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 96

                        Text {
                            width: parent.width
                            text: pane.kindStyle(model.kind, model.isMember).label
                            font.pixelSize: 13
                            color: Theme.color.textPrimary || "#111827"
                            elide: Text.ElideRight
                        }
                        // 第二行：设备名位（uid）+ detail 里的人话（离线原因 / 发布标题）
                        Text {
                            width: parent.width
                            property string sub: pane.subText(model.kind, model.uid, model.detail)
                            visible: sub !== ""
                            text: sub
                            font.pixelSize: 12
                            color: Theme.color.textSecondary || "#6B7280"
                            elide: Text.ElideRight
                        }
                    }
                }

                MouseArea {
                    id: evMa
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            ScrollBar.vertical: ScrollBar { width: 8 }
        }

        // 空态：订了但没事件（新班/刚上线都正常，不是故障）
        Column {
            anchors.centerIn: parent
            visible: list.count === 0
            spacing: 10

            SvgIcon {
                name: "clock"
                px: 36
                tint: Theme.color.textSecondary || "#9CA3AF"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "暂无班级动态"
                font.pixelSize: 13
                color: Theme.color.textSecondary || "#6B7280"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "设备上下线等事件会实时出现在这里"
                font.pixelSize: 12
                color: Theme.color.textSecondary || "#9CA3AF"
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
