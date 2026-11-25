import QtQuick

// A tiny acquisition scanner for work that is genuinely in flight. Animation
// comes from Theme.thumbnailPhase, so hundreds of delegates still share one
// clock and only transform scene-graph geometry.
Item {
    id: root

    property bool running: true
    property color color: Theme.accent
    readonly property real phase: running ? Theme.thumbnailPhase : 0
    readonly property real orbitSize: Math.max(16, Math.min(width, height) * 0.44)

    objectName: "thumbLoadingGlyph"
    visible: running
    opacity: 0.7 + 0.18 * Math.sin(phase * Math.PI * 2)

    Rectangle {
        anchors.centerIn: parent
        width: root.orbitSize
        height: width
        radius: width / 2
        color: "transparent"
        border.color: Theme.alpha(root.color, 0.24)
        border.width: 1
    }

    Item {
        anchors.centerIn: parent
        width: root.orbitSize
        height: width
        rotation: root.phase * 360

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -height / 2
            width: Math.max(3, Theme.spaceSM)
            height: width
            radius: width / 2
            color: root.color
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height - height / 2
            width: Math.max(2, Theme.spaceXS)
            height: width
            radius: width / 2
            color: Theme.alpha(root.color, 0.55)
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.max(3, root.orbitSize * 0.13)
        height: width
        rotation: 45
        color: Theme.alpha(root.color, 0.72)
    }
}
