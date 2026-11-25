import QtQuick
import Synchro.Theme

// Off-window snapshot source for Drag.imageSource. Compact on purpose:
// grabbing a list row would be a full-width bar.
Item {
    id: ghost

    property string name: ""
    property string thumbnail: ""
    property bool isDir: false
    property int count: 1

    readonly property int stack: ghost.count > 1 ? 6 : 0
    readonly property int cardW: 220
    readonly property int cardH: Math.max(Theme.fontBody + Theme.space(16), 48)
    readonly property int thumb: Math.max(Theme.space(28), Theme.fontBody + Theme.space(8))

    width: cardW + stack
    height: cardH + stack
    visible: true
    layer.enabled: true

    Repeater {
        model: ghost.count > 1 ? 2 : 0
        Rectangle {
            z: -1
            x: (index + 1) * 3
            y: (index + 1) * 3
            width: ghost.cardW
            height: ghost.cardH
            color: Theme.background
            border.color: Theme.normalBorder
            border.width: 1
            radius: Theme.radius
        }
    }

    Rectangle {
        id: card
        width: ghost.cardW
        height: ghost.cardH
        color: Theme.background
        border.color: Theme.accent
        border.width: 1
        radius: Theme.radius

        Item {
            id: iconBox
            width: ghost.thumb
            height: ghost.thumb
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter

            Image {
                id: icon
                anchors.fill: parent
                visible: ghost.thumbnail.length > 0 && icon.status === Image.Ready
                source: ghost.thumbnail
                asynchronous: false
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: iconBox.width
                sourceSize.height: iconBox.height
            }

            FolderMark {
                anchors.fill: parent
                visible: icon.status !== Image.Ready && ghost.isDir
            }

            FileMark {
                anchors.fill: parent
                visible: icon.status !== Image.Ready && !ghost.isDir
                suffix: {
                    var n = ghost.name
                    var i = n.lastIndexOf(".")
                    if (i <= 0 || i === n.length - 1)
                        return ""
                    return n.slice(i + 1).toUpperCase()
                }
            }
        }

        Text {
            anchors.left: iconBox.right
            anchors.right: badge.visible ? badge.left : parent.right
            anchors.leftMargin: Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            text: ghost.name
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        Rectangle {
            id: badge
            visible: ghost.count > 1
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(Theme.space(20), badgeText.implicitWidth + Theme.space(8))
            height: Math.max(Theme.space(18), Theme.fontBody + Theme.space(4))
            radius: height / 2
            color: Theme.accent

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: ghost.count > 99 ? "99+" : String(ghost.count)
                color: Theme.background
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }
        }
    }
}
