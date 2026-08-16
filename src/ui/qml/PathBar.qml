import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var fileModel
    required property var navStack
    property var locationChips

    readonly property string path: fileModel ? fileModel.path : ""
    readonly property var segments: navStack ? navStack.segmentsFor(root.path) : []
    readonly property bool atHome: navStack && root.path === navStack.homePath
    readonly property var chipsModel: root.locationChips ? root.locationChips.chips : []

    implicitHeight: Math.max(Theme.fontBody + Theme.space(10), 28)

    Text {
        id: brand
        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        text: "synchro"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Flickable {
        id: crumbFlick
        anchors.left: brand.right
        anchors.leftMargin: Theme.space(12)
        anchors.right: chips.left
        anchors.rightMargin: Theme.space(8)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: crumbRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: crumbRow
            height: crumbFlick.height
            spacing: 0

            Repeater {
                model: root.segments

                Row {
                    id: crumb
                    height: crumbRow.height
                    spacing: 0

                    required property var modelData
                    required property int index

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: crumb.index > 0
                        text: " / "
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Text {
                        id: crumbLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: crumb.modelData.label
                        color: crumbHover.hovered ? Theme.accent : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody

                        HoverHandler {
                            id: crumbHover
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.navStack)
                                    root.navStack.navigate(crumb.modelData.path)
                            }
                        }
                    }
                }
            }
        }

        onContentWidthChanged: Qt.callLater(root.scrollCrumbsToEnd)
    }

    onSegmentsChanged: Qt.callLater(root.scrollCrumbsToEnd)

    function scrollCrumbsToEnd() {
        crumbFlick.contentX = Math.max(0, crumbRow.width - crumbFlick.width)
    }

    Row {
        id: chips
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space(8)

        Repeater {
            model: root.chipsModel

            Rectangle {
                id: chip
                required property var modelData
                objectName: modelData && modelData.id ? modelData.id : "locationChip"
                implicitWidth: chipLabel.implicitWidth + Theme.space(12)
                implicitHeight: chipLabel.implicitHeight + Theme.space(6)
                color: modelData && modelData.active ? Theme.selectedFill
                       : (chipHover.hovered ? Theme.hoverFill : "transparent")
                border.width: 1
                border.color: Theme.normalBorder
                radius: Theme.radius

                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: chip.modelData && chip.modelData.label ? chip.modelData.label : ""
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                HoverHandler {
                    id: chipHover
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.locationChips)
                            root.locationChips.activate(chip.modelData.id)
                    }
                }
            }
        }

        Rectangle {
            id: homeChip
            objectName: "homeChip"
            visible: !root.locationChips
            implicitWidth: homeLabel.implicitWidth + Theme.space(12)
            implicitHeight: homeLabel.implicitHeight + Theme.space(6)
            color: root.atHome ? Theme.selectedFill : (homeHover.hovered ? Theme.hoverFill : "transparent")
            border.width: 1
            border.color: Theme.normalBorder
            radius: Theme.radius

            Text {
                id: homeLabel
                anchors.centerIn: parent
                text: "home"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            HoverHandler {
                id: homeHover
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (root.navStack)
                        root.navStack.goHome()
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.normalBorder
    }
}
