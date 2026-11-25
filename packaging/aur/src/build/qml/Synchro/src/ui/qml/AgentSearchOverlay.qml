import QtQuick
import Synchro.Theme

FocusScope {
    id: root

    property var bridge: null
    property string cwd: ""
    property string requestText: ""
    readonly property bool running: bridge ? bridge.running : false
    signal dismissed()

    objectName: "agentSearchOverlay"
    visible: false
    focus: visible

    function openFor(scope) {
        if (bridge && !bridge.running)
            bridge.cancel()
        cwd = scope || ""
        requestText = ""
        visible = true
        Qt.callLater(function () { requestEdit.forceActiveFocus() })
    }

    function closeOverlay(cancelWork) {
        if (cancelWork && bridge && bridge.running)
            bridge.cancel()
        visible = false
        dismissed()
    }

    function submit() {
        if (!bridge || running || !requestText.trim().length)
            return
        bridge.start(requestText, cwd)
    }

    function complete() {
        visible = false
    }

    Keys.onEscapePressed: function(event) {
        root.closeOverlay(true)
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.background, 0.78)

        MouseArea {
            anchors.fill: parent
            enabled: !root.running
            onClicked: function(mouse) {
                var point = mapToItem(card, mouse.x, mouse.y)
                if (point.x < 0 || point.y < 0 ||
                        point.x > card.width || point.y > card.height)
                    root.closeOverlay(false)
            }
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.space(36), Theme.space(760))
        height: Math.min(parent.height - Theme.space(36),
                         root.running ? Theme.space(560) : Theme.space(360))
        color: Theme.opaqueBackground
        border.color: root.running ? Theme.accent : Theme.normalBorder
        border.width: root.running ? 2 : 1
        radius: Theme.radius

        Text {
            id: eyebrow
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: Theme.space(22)
            anchors.topMargin: Theme.space(18)
            text: "FIND WITH AGENT"
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            font.letterSpacing: 1.1
        }

        Text {
            anchors.left: eyebrow.left
            anchors.right: closeButton.left
            anchors.top: eyebrow.bottom
            anchors.topMargin: Theme.space(5)
            text: root.running
                  ? ((root.bridge && root.bridge.agent.length
                      ? root.bridge.agent : "agent") +
                     " is sorting through Synchro's catalog")
                  : "Describe a useful file location in ordinary language."
            color: Theme.lightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.bold: true
            elide: Text.ElideRight
        }

        Text {
            anchors.left: eyebrow.left
            anchors.right: closeButton.left
            anchors.top: eyebrow.bottom
            anchors.topMargin: Theme.space(34)
            text: root.cwd
            color: Theme.darkForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideMiddle
        }

        Rectangle {
            id: closeButton
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space(14)
            width: Theme.controlHeight
            height: width
            radius: Theme.radius
            color: closeHover.hovered ? Theme.hoverFill : "transparent"

            Text {
                anchors.centerIn: parent
                text: "×"
                color: closeHover.hovered ? Theme.brightForeground : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
            }
            HoverHandler { id: closeHover }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.closeOverlay(true)
            }
        }

        Rectangle {
            id: requestFrame
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.space(22)
            anchors.rightMargin: Theme.space(22)
            anchors.topMargin: Theme.space(108)
            height: root.running ? Theme.space(58) : Theme.space(112)
            color: Theme.alpha(Theme.darkerBackground, 0.72)
            border.width: 0
            radius: Theme.radius
            clip: true

            TextEdit {
                id: requestEdit
                anchors.fill: parent
                anchors.margins: Theme.space(14)
                visible: !root.running
                text: root.requestText
                color: Theme.brightForeground
                selectionColor: Theme.selectionFill
                selectedTextColor: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle + 3
                font.weight: Font.Medium
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                onTextChanged: root.requestText = text
                Keys.onPressed: function(event) {
                    if ((event.modifiers & Qt.ControlModifier) &&
                            (event.key === Qt.Key_Return ||
                             event.key === Qt.Key_Enter)) {
                        root.submit()
                        event.accepted = true
                    }
                }
            }

            Text {
                anchors.fill: parent
                anchors.margins: Theme.space(14)
                visible: root.running
                text: "“" + root.requestText + "”"
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBodySmall
                font.italic: true
                wrapMode: Text.Wrap
                elide: Text.ElideRight
            }

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Theme.space(14)
                visible: !root.running && !requestEdit.text.length
                text: "e.g. large blue images modified this month, or Python projects with recent changes"
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSubtitle
                wrapMode: Text.Wrap
            }
        }

        Item {
            id: sorter
            anchors.left: requestFrame.left
            anchors.right: requestFrame.right
            anchors.top: requestFrame.bottom
            anchors.bottom: footer.top
            anchors.topMargin: Theme.space(12)
            anchors.bottomMargin: Theme.space(12)
            visible: root.running
            clip: true
            property int phase: 0
            readonly property int poolCount:
                root.bridge && root.bridge.thumbnails
                ? root.bridge.thumbnails.length : 0
            readonly property int columns: width >= Theme.space(600) ? 6 : 4
            readonly property int rows: 5
            readonly property int tileCount:
                Math.min(columns * rows, poolCount)
            readonly property real gap: Theme.space(7)
            readonly property real tileWidth:
                (width - gap * (columns - 1)) / columns
            readonly property real tileHeight:
                Math.max(40, (height - gap * (rows - 1)) / rows)

            Timer {
                interval: 460
                running: root.running
                repeat: true
                onTriggered: sorter.phase++
            }

            Repeater {
                model: sorter.tileCount

                delegate: Rectangle {
                    id: sortTile
                    required property int index
                    readonly property int slot:
                        sorter.tileCount > 0
                        ? (index + sorter.phase * 3) % sorter.tileCount : 0
                    readonly property int sourceIndex:
                        sorter.poolCount > 0
                        ? (index + Math.floor(sorter.phase / 2) *
                           sorter.tileCount) % sorter.poolCount : 0
                    x: (slot % sorter.columns) *
                       (sorter.tileWidth + sorter.gap)
                    y: Math.floor(slot / sorter.columns) *
                       (sorter.tileHeight + sorter.gap)
                    width: sorter.tileWidth
                    height: sorter.tileHeight
                    color: Theme.normalFill
                    border.color: Theme.alpha(Theme.accent, 0.34)
                    border.width: 1
                    radius: Theme.radius
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: root.bridge.thumbnails[sortTile.sourceIndex]
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        opacity: 0.72
                    }

                    Behavior on x {
                        NumberAnimation { duration: 380; easing.type: Easing.OutCubic }
                    }
                    Behavior on y {
                        NumberAnimation { duration: 380; easing.type: Easing.OutCubic }
                    }
                }
            }

            QueryBusy {
                anchors.fill: parent
                running: root.running && sorter.tileCount === 0
                label: "Reading the catalog…"
                veilOpacity: 0
            }
        }

        Text {
            anchors.left: requestFrame.left
            anchors.right: requestFrame.right
            anchors.top: requestFrame.bottom
            anchors.topMargin: Theme.space(14)
            visible: !root.running &&
                     (root.bridge ? root.bridge.error.length > 0 : false)
            text: root.bridge ? root.bridge.error : ""
            color: Theme.urgent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            wrapMode: Text.Wrap
        }

        Item {
            id: footer
            anchors.left: requestFrame.left
            anchors.right: requestFrame.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space(18)
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.running ? "Esc cancels" : "Catalog-only by default  ·  Ctrl+Enter"
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
            }

            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: actionText.implicitWidth + Theme.space(24)
                height: Theme.controlHeight
                radius: Theme.radius
                color: root.running ? Theme.normalFill
                                    : (actionHover.hovered ? Theme.accent
                                                           : Theme.selectedFill)
                border.color: Theme.accent
                border.width: 1

                Text {
                    id: actionText
                    anchors.centerIn: parent
                    text: root.running ? "cancel" : "find files"
                    color: !root.running && actionHover.hovered
                           ? Theme.background : Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.bold: true
                }
                HoverHandler { id: actionHover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.running ? root.closeOverlay(true)
                                            : root.submit()
                }
            }
        }
    }
}
