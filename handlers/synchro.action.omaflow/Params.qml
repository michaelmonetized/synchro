import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    property var matches: root.host ? root.host.omaflowMatches() : []
    property int flowIndex: 0
    property bool dryRun: true
    property bool armed: false

    readonly property var chosen: root.matches.length > 0 &&
                                  root.flowIndex < root.matches.length
                                  ? root.matches[root.flowIndex] : null

    function choose(index) {
        root.flowIndex = Math.max(0, Math.min(index, root.matches.length - 1))
        root.armed = false
        if (root.host)
            root.host.doParamsFocused = true
    }

    function setDryRun(value) {
        root.dryRun = value
        root.armed = false
        if (root.host)
            root.host.doParamsFocused = true
    }

    function commit() {
        if (!root.host || !root.chosen || !root.chosen.id)
            return false
        if (!root.dryRun && !root.armed) {
            root.armed = true
            return false
        }
        return root.host.runOmaflow(root.chosen.id, root.dryRun)
    }

    function actionKey(key, modifiers) {
        if (key === Qt.Key_J || key === Qt.Key_Down || key === Qt.Key_S) {
            root.choose(root.flowIndex + 1)
            return true
        }
        if (key === Qt.Key_K || key === Qt.Key_Up || key === Qt.Key_W) {
            root.choose(root.flowIndex - 1)
            return true
        }
        if (key === Qt.Key_H || key === Qt.Key_Left || key === Qt.Key_A) {
            root.setDryRun(true)
            return true
        }
        if (key === Qt.Key_L || key === Qt.Key_Right || key === Qt.Key_D) {
            root.setDryRun(false)
            return true
        }
        if (key === Qt.Key_Return || key === Qt.Key_Enter) {
            if (!root.dryRun && !root.armed) {
                root.armed = true
                return true
            }
            return false
        }
        return false
    }

    Row {
        id: modeRail
        anchors.top: parent.top
        anchors.left: parent.left
        spacing: Theme.space(4)

        Repeater {
            model: [
                { label: "DRY RUN", value: true },
                { label: "RUN", value: false }
            ]

            delegate: Rectangle {
                required property var modelData
                width: modeLabel.implicitWidth + Theme.space(14)
                height: Math.max(22, Theme.fontBody + Theme.space(8))
                color: root.dryRun === modelData.value
                       ? Theme.selectedFill : "transparent"
                border.width: root.dryRun === modelData.value ? 1 : 0
                border.color: root.dryRun === modelData.value
                              ? Theme.accent : "transparent"

                Text {
                    id: modeLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.dryRun === modelData.value
                           ? Theme.foreground : Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: root.dryRun === modelData.value
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.setDryRun(modelData.value)
                }
            }
        }
    }

    Text {
        id: status
        anchors.left: modeRail.right
        anchors.right: parent.right
        anchors.verticalCenter: modeRail.verticalCenter
        anchors.leftMargin: Theme.space(8)
        text: root.dryRun ? "Inspect the exact plan"
                          : (root.armed ? "ARMED · Enter runs it"
                                        : "Enter once to arm")
        color: !root.dryRun && root.armed ? Theme.accent : Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
    }

    ListView {
        id: flows
        anchors.top: modeRail.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.space(5)
        clip: true
        model: root.matches
        currentIndex: root.flowIndex
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0

        highlight: Rectangle {
            color: Theme.selectedFill

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 2
                color: Theme.accent
            }
        }

        delegate: Item {
            id: flowRow
            required property int index
            required property var modelData
            width: ListView.view.width
            height: Math.max(36, Theme.fontBody * 2 + Theme.space(12))

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.space(8)
                anchors.rightMargin: Theme.space(8)
                anchors.topMargin: Theme.space(4)
                text: "↯  " + (flowRow.modelData.name || flowRow.modelData.id)
                color: flows.currentIndex === flowRow.index
                       ? Theme.foreground : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.bold: flows.currentIndex === flowRow.index
                elide: Text.ElideRight
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.space(24)
                anchors.rightMargin: Theme.space(8)
                anchors.bottomMargin: Theme.space(4)
                text: flowRow.modelData.matchReason || "Accepts this selection"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.choose(flowRow.index)
            }
        }
    }

    Text {
        visible: root.matches.length === 0
        anchors.centerIn: parent
        text: "No flow accepts this selection"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }
}
