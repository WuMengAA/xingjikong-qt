import QtQuick

// 卡片标题（shadcn CardTitle）
//
// 字号取 text-base(16) + DemiBold：shadcn 的 CardTitle 是 text-2xl，
// 那是"内容卡"用法；设置页一张卡里要放 6-8 行，24 号会把卡撑成两层楼。
// 这里用 16，比正文 14 高一级 —— 层级靠字重+字号，不靠颜色（shadcn 不用彩色标题）。
//
// ⚠️ 属性名 `text2` 沿用拆分前的命名：本件继承 Text，`text` 已被基类占用，
//    自定义同名属性会与基类成员冲突（项目里 Md3Btn 的 enabled 踩过）。
Text {
    id: root

    property var th
    property string text2: ""

    text: text2
    font.pixelSize: th ? th.fsBase : 16
    font.weight: Font.DemiBold
    color: th ? th.foreground : "#09090b"
    bottomPadding: 4
    elide: Text.ElideRight
}
