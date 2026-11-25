import QtQuick
import Synchro.Theme 1.0

Column {
    id: root

    property string heading: "ROUTES"
    property string directionGlyph: "→"
    property var routes: []
    property color accentColor: Theme.accent
    property int routeLimit: 4

    visible: routes.length > 0
    spacing: Theme.spaceSM

    Text {
        text: root.heading
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption
        font.bold: true
        font.letterSpacing: 0.8
    }

    Repeater {
        model: root.routes.slice(0, root.routeLimit)

        Item {
            objectName: "omaflowInspectorRoute"
            required property var modelData
            width: parent.width
            height: Math.max(routePill.height, routeText.implicitHeight)

            Rectangle {
                id: routePill
                objectName: "omaflowInspectorRoutePill"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                readonly property int horizontalPadding: Theme.spaceLG
                readonly property bool contentFits:
                    pillText.implicitWidth <= width - horizontalPadding * 2
                width: Math.min(pillText.implicitWidth + horizontalPadding * 2,
                                Math.max(Theme.space(48), parent.width * 0.46))
                height: Math.max(Theme.space(20),
                                 pillText.implicitHeight + Theme.spaceXS * 2)
                radius: height / 2
                color: Theme.alpha(root.accentColor, 0.10)
                border.color: Theme.alpha(root.accentColor, 0.34)
                border.width: 1
                clip: true

                Text {
                    id: pillText
                    anchors.fill: parent
                    anchors.leftMargin: routePill.horizontalPadding
                    anchors.rightMargin: routePill.horizontalPadding
                    text: root.directionGlyph + "  " +
                          String(parent.parent.modelData.label || "NEXT")
                    color: root.accentColor
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                id: routeText
                anchors.left: routePill.right
                anchors.leftMargin: Theme.spaceLG
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: String(parent.modelData.peer || "step") +
                      (parent.modelData.backEdge ? "  ·  loop" : "")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }

    Text {
        visible: root.routes.length > root.routeLimit
        text: "+ " + (root.routes.length - root.routeLimit) + " more routes"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption
    }
}
