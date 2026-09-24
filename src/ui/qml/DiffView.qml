import QtQuick
import Synchro.Theme

Item {
    id: root

    readonly property var lane: (typeof gitLane !== "undefined") ? gitLane : null
    property var keyMachine: null

    objectName: "diffView"
    visible: lane && lane.diffOpen
    focus: visible && (!keyMachine || keyMachine.listFocused)

    onVisibleChanged: {
        if (visible)
            forceActiveFocus()
    }

    Connections {
        target: root.keyMachine
        function onModeChanged() {
            if (root.visible && root.keyMachine && root.keyMachine.listFocused)
                root.forceActiveFocus()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    FontMetrics {
        id: metrics
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontBody
    }

    function lineColor(text) {
        if (!text)
            return Theme.foreground
        if (text.startsWith("@@"))
            return Theme.accent
        if (text.startsWith("+") && !text.startsWith("+++"))
            return Theme.fsnGreen
        if (text.startsWith("-") && !text.startsWith("---"))
            return Theme.urgent
        return Theme.foreground
    }

    function pageLines() {
        return Math.max(1, Math.floor(lines.height / Math.max(1, metrics.height)) - 1)
    }

    Keys.onPressed: function (event) {
        if (!root.lane || !root.visible)
            return
        var shift = event.modifiers & Qt.ShiftModifier
        var chord = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)
        if (chord) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape) {
            root.lane.closeDiff()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_N)
            root.lane.moveDiffFile(1)
        else if (event.key === Qt.Key_P)
            root.lane.moveDiffFile(-1)
        else if (event.key === Qt.Key_J)
            root.lane.moveDiffLine(1)
        else if (event.key === Qt.Key_K)
            root.lane.moveDiffLine(-1)
        else if (event.key === Qt.Key_H)
            shift ? root.lane.edgeDiffCol(-1) : root.lane.moveDiffCol(-1)
        else if (event.key === Qt.Key_L)
            shift ? root.lane.edgeDiffCol(1) : root.lane.moveDiffCol(1)
        else if (event.key === Qt.Key_U)
            shift ? root.lane.edgeDiffLine(-1)
                  : root.lane.pageDiffLine(-1, root.pageLines())
        else if (event.key === Qt.Key_D)
            shift ? root.lane.edgeDiffLine(1)
                  : root.lane.pageDiffLine(1, root.pageLines())
        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down ||
                 event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            event.accepted = true
            return
        }
        event.accepted = true
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.space(12)
        spacing: Theme.space(8)

        Text {
            width: parent.width
            text: (root.lane ? root.lane.diffTitle : "") +
                  "    esc closes   n/p files   hjklud lines"
            color: Theme.muted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontBodySmall
            elide: Text.ElideRight
        }

        Row {
            width: parent.width
            height: parent.height - y
            spacing: Theme.space(8)

            ListView {
                id: lines
                width: parent.width - rail.width - parent.spacing
                height: parent.height
                clip: true
                model: root.lane ? root.lane.diffLines : []
                currentIndex: root.lane ? root.lane.diffLine : 0
                boundsBehavior: Flickable.StopAtBounds

                onCurrentIndexChanged: if (currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)

                delegate: Item {
                    width: lines.width
                    height: metrics.height
                    required property int index
                    required property string modelData

                    Rectangle {
                        anchors.fill: parent
                        visible: index === lines.currentIndex
                        color: Theme.alpha(Theme.accent, 0.16)
                    }

                    Text {
                        x: root.lane ? -root.lane.diffCol * metrics.averageCharacterWidth : 0
                        text: modelData
                        color: root.lineColor(modelData)
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontBody
                    }
                }
            }

            Rectangle {
                id: rail
                width: Math.min(280, parent.width * 0.34)
                height: parent.height
                color: Theme.darkBackground
                border.width: 1
                border.color: Theme.normalBorder

                ListView {
                    id: files
                    anchors.fill: parent
                    clip: true
                    model: root.lane ? root.lane.diffFiles : []
                    currentIndex: root.lane ? root.lane.diffFile : 0
                    boundsBehavior: Flickable.StopAtBounds

                    onCurrentIndexChanged: if (currentIndex >= 0)
                        positionViewAtIndex(currentIndex, ListView.Contain)

                    delegate: Item {
                        width: files.width
                        height: metrics.height + Theme.space(4)
                        required property int index
                        required property var modelData

                        Rectangle {
                            anchors.fill: parent
                            visible: index === files.currentIndex
                            color: Theme.alpha(Theme.accent, 0.22)
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space(8)
                            anchors.rightMargin: Theme.space(8)
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.label
                            color: index === files.currentIndex
                                   ? Theme.brightForeground : Theme.foreground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontBodySmall
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }
}
