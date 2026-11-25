import QtQuick

Rectangle {
    id: root

    property string label: ""

    color: Theme.normalFill
    border.color: Theme.normalBorder
    border.width: 1
    radius: Math.max(Theme.radius, Theme.spaceXS)
    implicitHeight: Math.max(Theme.fontBodySmall + Theme.spaceMD * 2,
                             Theme.controlHeight - Theme.spaceSM)
    implicitWidth: keyText.implicitWidth + Theme.spaceLG * 2

    Text {
        id: keyText
        anchors.centerIn: parent
        text: root.label
        color: Theme.brightForeground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontBodySmall
        font.bold: true
    }
}
