import QtQuick
import Synchro.Theme

Item {
    id: root

    property var host: null
    onHostChanged: {
        if (!host)
            return
        if (host.registerDoSurface && paramSurface)
            host.registerDoSurface(paramSurface)
        if (host.registerDoContentSurface && previewSurface)
            host.registerDoContentSurface(previewSurface)
    }
    objectName: "actionOverlay"
    visible: root.host && root.host.actionOpen
    z: 110
    focus: false
    activeFocusOnTab: false

    signal closed()

    readonly property bool paramsOn: root.host && root.host.doParamsFocused

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.8
        MouseArea {
            anchors.fill: parent
            onClicked: if (root.host)
                root.host.closeAction()
        }
    }

    Rectangle {
        id: frame
        objectName: "doOverlay"
        anchors.fill: parent
        anchors.margins: Theme.space(28)
        color: Theme.background
        border.color: Theme.accent
        border.width: 1

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onClicked: function (mouse) {
                mouse.accepted = true
            }
        }

        Text {
            id: caption
            objectName: "doCaption"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.space(10)
            text: root.host ? root.host.doCaption : "do"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        Rectangle {
            id: accentBar
            anchors.top: caption.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: Theme.space(8)
            height: 1
            color: Theme.accent
            opacity: 0.45
        }

        Text {
            id: hint
            objectName: "doHint"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space(10)
            text: root.host && root.host.doHint ? root.host.doHint
                                               : "W/S verbs · Enter run · Esc leave"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }

        Item {
            id: verbPane
            anchors.top: accentBar.bottom
            anchors.left: parent.left
            anchors.bottom: hint.top
            anchors.topMargin: Theme.space(8)
            anchors.bottomMargin: Theme.space(8)
            anchors.leftMargin: Theme.space(8)
            width: Math.min(Theme.space(220), Math.max(Theme.space(140),
                                                       parent.width * 0.32))

            Text {
                id: verbHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                text: "ACTIONS"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            ListView {
                id: verbs
                objectName: "doVerbList"
                anchors.top: verbHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.topMargin: Theme.space(6)
                clip: true
                focus: false
                keyNavigationEnabled: false
                boundsBehavior: Flickable.StopAtBounds
                highlightFollowsCurrentItem: true
                highlightMoveDuration: 0
                model: root.host ? root.host.doVerbs : []
                currentIndex: root.host ? root.host.doIndex : -1

                highlight: Rectangle {
                    color: Theme.selectedFill
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    height: Math.max(Theme.fontBody + Theme.space(10), 24)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 2
                        visible: verbs.currentIndex === row.index
                        color: Theme.accent
                    }

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space(10)
                        anchors.rightMargin: Theme.space(8)
                        verticalAlignment: Text.AlignVCenter
                        text: row.modelData && row.modelData.name
                              ? row.modelData.name : ""
                        color: verbs.currentIndex === row.index
                               ? Theme.foreground : Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: if (root.host)
                            root.host.doIndex = row.index
                        onDoubleClicked: {
                            if (root.host) {
                                root.host.doIndex = row.index
                                root.host.runDoVerb()
                            }
                        }
                    }
                }
            }

            Text {
                visible: verbs.count === 0
                anchors.centerIn: verbs
                text: "Nothing to do"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }
        }

        Rectangle {
            id: split
            anchors.top: accentBar.bottom
            anchors.bottom: hint.top
            anchors.left: verbPane.right
            anchors.topMargin: Theme.space(8)
            anchors.bottomMargin: Theme.space(8)
            width: 1
            color: Theme.normalBorder
        }

        Item {
            id: briefPane
            anchors.top: accentBar.bottom
            anchors.left: split.right
            anchors.right: parent.right
            anchors.bottom: hint.top
            anchors.margins: Theme.space(8)

            readonly property bool looking: root.host && root.host.doParamsFocused
                                            && !root.host.doHasParams
            readonly property bool hasMosaic: root.host &&
                                              root.host.doMosaicUrl.length > 0
            readonly property bool hasPreview: root.host && root.host.doPreviewItem

            Text {
                id: briefHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                text: root.host && root.host.doHasParams ? "LOOK · PARAMS" : "LOOK"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            Text {
                id: briefTitle
                objectName: "doBriefTitle"
                anchors.top: briefHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: Theme.space(6)
                text: root.host ? root.host.doBriefTitle : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }

            Text {
                id: briefBody
                objectName: "doBriefBody"
                anchors.top: briefTitle.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: Theme.space(4)
                text: root.host ? root.host.doBriefBody : ""
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Item {
                id: contentBox
                objectName: "doContentBox"
                anchors.top: briefBody.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: paramSurface.visible ? paramSurface.top
                                                     : parent.bottom
                anchors.topMargin: Theme.space(8)
                anchors.bottomMargin: paramSurface.visible ? Theme.space(8) : 0

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1
                    border.color: briefPane.looking ? Theme.accent
                                                    : Theme.normalBorder
                    opacity: 0.8
                }

                Image {
                    id: mosaic
                    objectName: "doMosaicImage"
                    anchors.fill: parent
                    anchors.margins: Theme.space(10)
                    visible: briefPane.hasMosaic
                    source: root.host ? root.host.doMosaicUrl : ""
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: Math.max(128, Math.round(width))
                    sourceSize.height: Math.max(128, Math.round(height))
                }

                Item {
                    id: previewSurface
                    objectName: "doContentSurface"
                    anchors.fill: parent
                    anchors.margins: Theme.space(4)
                    visible: briefPane.hasPreview
                    Component.onCompleted: if (root.host &&
                                               root.host.registerDoContentSurface)
                        root.host.registerDoContentSurface(previewSurface)
                }

                FolderMark {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) * 0.42
                    height: width
                    visible: root.host && root.host.doTargetIsDir &&
                             !briefPane.hasMosaic
                }

                FileMark {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) * 0.28
                    height: Math.round(width * 1.16)
                    visible: root.host && !root.host.doTargetIsDir &&
                             !briefPane.hasPreview
                    suffix: {
                        var n = root.host ? root.host.doTargetName : ""
                        var i = n.lastIndexOf(".")
                        if (i <= 0 || i === n.length - 1)
                            return ""
                        return n.slice(i + 1).toUpperCase()
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: briefPane.hasPreview &&
                             !(root.host && root.host.doHasParams)
                    onClicked: if (root.host)
                        root.host.doParamsFocused = true
                }
            }

            Item {
                id: paramSurface
                objectName: "doParamSurface"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: visible
                        ? Math.min(Theme.space(200),
                                   Math.max(Theme.space(88),
                                            parent.height * 0.34))
                        : 0
                visible: root.host && root.host.doHasParams

                Rectangle {
                    anchors.fill: parent
                    visible: root.paramsOn
                    color: Theme.selectedFill
                    opacity: 0.28
                }

                Component.onCompleted: if (root.host && root.host.registerDoSurface)
                    root.host.registerDoSurface(paramSurface)
            }

            MouseArea {
                anchors.fill: paramSurface
                enabled: paramSurface.visible
                z: -1
                onClicked: if (root.host)
                    root.host.doParamsFocused = true
            }
        }

        function reparentAction() {
            if (!root.host || !root.host.actionItem || !paramSurface)
                return
            var item = root.host.actionItem
            item.parent = paramSurface
            item.anchors.fill = paramSurface
            item.focus = false
        }

        function reparentPreview() {
            if (!root.host || !root.host.doPreviewItem || !previewSurface)
                return
            var item = root.host.doPreviewItem
            item.parent = previewSurface
            item.anchors.fill = previewSurface
            item.focus = false
        }

        Connections {
            target: root.host
            function onActionItemChanged() { Qt.callLater(frame.reparentAction) }
            function onDoIndexChanged() { Qt.callLater(frame.reparentAction) }
            function onDoPreviewChanged() { Qt.callLater(frame.reparentPreview) }
            function onActionOpenChanged() {
                if (root.host && root.host.actionOpen) {
                    Qt.callLater(frame.reparentAction)
                    Qt.callLater(frame.reparentPreview)
                }
                if (root.host && !root.host.actionOpen)
                    root.closed()
            }
        }
    }
}
