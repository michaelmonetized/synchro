import QtQuick

FocusScope {
    id: root

    property var options: []
    property string currentId: ""
    property bool showLabels: true
    property bool outlineSelection: false
    property bool fillWidth: false
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
        width: root.fillWidth ? root.width : implicitWidth
        height: parent.height

        Repeater {
            model: root.options

            ChromeButton {
                required property var modelData
                objectName: "segment-" + modelData.id
                width: root.fillWidth && root.options.length > 0
                       ? row.width / root.options.length : implicitWidth
                height: row.height
                compact: !root.showLabels
                iconName: modelData.icon || ""
                fallbackGlyph: modelData.glyph || ""
                label: root.showLabels ? (modelData.label || "") : ""
                checked: root.currentId === modelData.id
                checkedOutline: root.outlineSelection
                toolTip: modelData.tip || modelData.label || ""
                onTriggered: root.activated(modelData.id)
            }
        }
    }
}
