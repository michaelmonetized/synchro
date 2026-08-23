import QtQuick

FocusScope {
    id: root

    property var options: []
    property string currentId: ""
    property bool showLabels: true
    signal activated(string optionId)

    implicitHeight: Theme.controlHeight
    implicitWidth: row.implicitWidth

    Rectangle {
        anchors.fill: parent
        color: Theme.normalFill
        border.color: Theme.normalBorder
        border.width: Theme.normalBorderWidth
        radius: Theme.radius
    }

    Row {
        id: row
        height: parent.height

        Repeater {
            model: root.options

            ChromeButton {
                required property var modelData
                height: row.height
                compact: !root.showLabels
                iconName: modelData.icon || ""
                fallbackGlyph: modelData.glyph || ""
                label: root.showLabels ? (modelData.label || "") : ""
                checked: root.currentId === modelData.id
                toolTip: modelData.tip || modelData.label || ""
                onTriggered: root.activated(modelData.id)
            }
        }
    }
}
