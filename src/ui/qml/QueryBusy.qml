import QtQuick
import Synchro.Theme

// Shared query veil for browser and companion result surfaces. The content
// remains visible underneath so a long query feels like an update in place,
// while the shared scene-graph animation makes the in-flight state explicit.
Item {
    id: root

    property bool running: false
    property string label: "Querying…"
    property real veilOpacity: 0.72

    objectName: "queryBusy"
    visible: running
    enabled: visible

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.darkerBackground, root.veilOpacity)
    }

    Item {
        anchors.centerIn: parent
        width: Math.min(parent.width, Theme.space(210))
        height: Theme.space(92)

        ThumbLoadingGlyph {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: Theme.space(54)
            height: width
            running: root.running
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: parent.width
            text: root.label
            color: Theme.lightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }
    }
}
