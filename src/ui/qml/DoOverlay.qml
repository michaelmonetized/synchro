import QtQuick
import Synchro.Theme

Item {
    id: root

    property var host: null
    objectName: "actionOverlay"
    visible: root.host && root.host.actionOpen
    z: 110
    focus: false
    activeFocusOnTab: false

    signal closed()

    readonly property bool paramsOn: root.host && root.host.doParamsFocused
    readonly property bool operationVisible: root.host && root.host.doOperationState.length > 0
    readonly property bool operationRunning: operationVisible && root.host.doOperationState === "running"

    function clamp(value, low, high) {
        return Math.max(low, Math.min(value, high))
    }

    function deckX(deckWidth) {
        if (!root.host || !root.host.doContextual)
            return Math.round((root.width - deckWidth) / 2)
        var right = root.host.doAnchorX + Theme.space(12)
        var left = root.host.doAnchorX - deckWidth - Theme.space(12)
        return root.clamp(right + deckWidth <= root.width - Theme.space(16) ? right : left,
                          Theme.space(16), root.width - deckWidth - Theme.space(16))
    }

    function deckY(deckHeight) {
        if (!root.host || !root.host.doContextual)
            return Math.round((root.height - deckHeight) / 2)
        var below = root.host.doAnchorY + Theme.space(10)
        var above = root.host.doAnchorY - deckHeight - Theme.space(10)
        return root.clamp(below + deckHeight <= root.height - Theme.space(16) ? below : above,
                          Theme.space(16), root.height - deckHeight - Theme.space(16))
    }

    onHostChanged: {
        if (!host)
            return
        if (host.registerDoSurface && paramSurface)
            host.registerDoSurface(paramSurface)
        if (host.registerDoContentSurface && previewSurface)
            host.registerDoContentSurface(previewSurface)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.background, 0.68)
        MouseArea {
            anchors.fill: parent
            onClicked: if (root.host) root.host.closeAction()
        }
    }

    Rectangle {
        id: shadow
        x: frame.x + Theme.space(8)
        y: frame.y + Theme.space(10)
        width: frame.width
        height: frame.height
        color: Theme.alpha(Theme.darkerBackground, 0.72)
        radius: frame.radius
    }

    Rectangle {
        id: frame
        objectName: "doOverlay"
        width: Math.min(root.width - Theme.space(32),
                        Math.max(Theme.space(640), Math.min(Theme.space(860), root.width * 0.68)))
        height: Math.min(root.height - Theme.space(32),
                         Math.max(Theme.space(450), Math.min(Theme.space(620), root.height * 0.72)))
        x: root.deckX(width)
        y: root.deckY(height)
        color: Theme.popupBackground
        border.color: Theme.popupBorder
        border.width: 1
        radius: Math.max(Theme.radius, Theme.space(2))
        clip: true

        Behavior on x { NumberAnimation { duration: 90 } }
        Behavior on y { NumberAnimation { duration: 90 } }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onClicked: function(mouse) { mouse.accepted = true }
        }

        Rectangle {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.space(52)
            color: Theme.alpha(Theme.lighterBackground, 0.44)

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Theme.space(3)
                color: root.operationVisible && root.host.doOperationState === "failed" ? Theme.urgent : Theme.accent
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(16)
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - closeButton.width - Theme.space(42)
                spacing: Theme.space(2)

                Text {
                    text: root.operationVisible ? "ACTION RECEIPT" : "ACTION DECK"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1.2
                }
                Text {
                    id: caption
                    objectName: "doCaption"
                    width: parent.width
                    text: root.host ? root.host.doCaption : "Actions"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideMiddle
                }
            }

            ChromeButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(10)
                anchors.verticalCenter: parent.verticalCenter
                fallbackGlyph: "×"
                toolTip: root.operationRunning ? "Close; action continues" : "Close"
                onTriggered: if (root.host) root.host.closeAction()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.alpha(Theme.popupBorder, 0.38)
            }
        }

        Item {
            id: body
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top

            Item {
                id: verbPane
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: Math.min(Theme.space(276), parent.width * 0.39)

                Rectangle { anchors.fill: parent; color: Theme.alpha(Theme.darkerBackground, 0.24) }

                Text {
                    id: verbHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.topMargin: Theme.space(12)
                    anchors.leftMargin: Theme.space(14)
                    text: "AVAILABLE NOW"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                }

                ListView {
                    id: verbs
                    objectName: "doVerbList"
                    anchors.top: verbHeader.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.topMargin: Theme.space(8)
                    anchors.bottomMargin: Theme.space(8)
                    clip: true
                    focus: false
                    keyNavigationEnabled: false
                    boundsBehavior: Flickable.StopAtBounds
                    highlightFollowsCurrentItem: true
                    highlightMoveDuration: 85
                    model: root.host ? root.host.doVerbs : []
                    currentIndex: root.host ? root.host.doIndex : -1

                    highlight: Rectangle {
                        color: Theme.selectedFill
                        border.color: Theme.alpha(Theme.accent, 0.45)
                        border.width: 1
                    }

                    delegate: Item {
                        id: actionRow
                        required property int index
                        required property var modelData
                        width: ListView.view.width
                        height: Theme.space(46)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Theme.space(3)
                            visible: verbs.currentIndex === actionRow.index
                            color: Theme.accent
                        }
                        Text {
                            id: actionGlyph
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space(14)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.space(18)
                            visible: !(actionRow.modelData.icon || "").length
                            text: actionRow.modelData.runtime === "omaflow" ? "↯" :
                                  (actionRow.modelData.effect === "destructive" ? "!" : "›")
                            color: actionRow.modelData.effect === "destructive" ? Theme.urgent : Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                        }
                        AppIcon {
                            anchors.centerIn: actionGlyph
                            width: Theme.space(18)
                            height: width
                            iconSize: Math.round(width)
                            name: actionRow.modelData.icon || ""
                            visible: name.length > 0
                        }
                        Column {
                            anchors.left: actionGlyph.right
                            anchors.leftMargin: Theme.space(6)
                            anchors.right: providerBadge.left
                            anchors.rightMargin: Theme.space(6)
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Text {
                                width: parent.width
                                text: actionRow.modelData.name || ""
                                color: verbs.currentIndex === actionRow.index ? Theme.brightForeground : Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.bold: verbs.currentIndex === actionRow.index
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: actionRow.modelData.group === "automation" ? "automation" : actionRow.modelData.group
                                color: Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            id: providerBadge
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space(10)
                            anchors.verticalCenter: parent.verticalCenter
                            width: providerLabel.implicitWidth + Theme.space(10)
                            height: Theme.space(20)
                            visible: actionRow.modelData.runtime === "omaflow"
                            color: Theme.alpha(Theme.accent, 0.12)
                            border.color: Theme.alpha(Theme.accent, 0.38)
                            border.width: 1
                            radius: height / 2
                            Text {
                                id: providerLabel
                                anchors.centerIn: parent
                                text: "FLOW"
                                color: Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                font.bold: true
                            }
                        }
                        HoverHandler { id: actionHover }
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.hoverFill
                            visible: actionHover.hovered && verbs.currentIndex !== actionRow.index
                        }
                        TapHandler {
                            onTapped: if (root.host) root.host.doIndex = actionRow.index
                        }
                    }
                }

                Text {
                    visible: verbs.count === 0
                    anchors.centerIn: verbs
                    text: "Nothing available here"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: verbPane.right
                width: 1
                color: Theme.normalBorder
            }

            Item {
                id: detailPane
                anchors.top: parent.top
                anchors.left: verbPane.right
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 1

                Item {
                    id: actionDetail
                    anchors.fill: parent
                    anchors.margins: Theme.space(16)
                    visible: !root.operationVisible

                    Row {
                        id: metaRow
                        anchors.top: parent.top
                        anchors.left: parent.left
                        spacing: Theme.space(8)
                        Rectangle {
                            width: providerText.implicitWidth + Theme.space(12)
                            height: Theme.space(22)
                            color: Theme.alpha(Theme.accent, 0.10)
                            border.color: Theme.alpha(Theme.accent, 0.32)
                            border.width: 1
                            radius: height / 2
                            Text {
                                id: providerText
                                anchors.centerIn: parent
                                text: root.host && root.host.doProvider ? root.host.doProvider.toUpperCase() : "SYNCHRO"
                                color: Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                font.bold: true
                            }
                        }
                        Rectangle {
                            width: effectText.implicitWidth + Theme.space(12)
                            height: Theme.space(22)
                            color: Theme.alpha(root.host && root.host.doEffect === "destructive" ? Theme.urgent : Theme.foreground, 0.06)
                            border.color: Theme.alpha(root.host && root.host.doEffect === "destructive" ? Theme.urgent : Theme.normalBorder, 0.7)
                            border.width: 1
                            radius: height / 2
                            Text {
                                id: effectText
                                anchors.centerIn: parent
                                text: root.host && root.host.doEffect ? root.host.doEffect.toUpperCase() : "ACTION"
                                color: root.host && root.host.doEffect === "destructive" ? Theme.urgent : Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                font.bold: true
                            }
                        }
                    }

                    Text {
                        id: briefTitle
                        objectName: "doBriefTitle"
                        anchors.top: metaRow.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space(10)
                        text: root.host ? root.host.doBriefTitle : ""
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        id: briefBody
                        objectName: "doBriefBody"
                        anchors.top: briefTitle.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space(5)
                        text: root.host ? root.host.doBriefBody : ""
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        id: contentBox
                        objectName: "doContentBox"
                        anchors.top: briefBody.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space(12)
                        height: root.host && root.host.doHasParams ? Theme.space(130) :
                                Math.min(Theme.space(220), parent.height * 0.48)
                        color: Theme.alpha(Theme.darkerBackground, 0.52)
                        border.color: Theme.alpha(Theme.normalBorder, 0.62)
                        border.width: 1
                        radius: Theme.radius
                        clip: true

                        Item {
                            id: previewPane
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            width: Math.round(parent.width * 0.48)

                        DoFolderGrid {
                            anchors.fill: previewPane
                            anchors.margins: Theme.space(5)
                            visible: root.host && root.host.doTargetIsDir
                            fileModel: root.host ? root.host.doFolderModel : null
                            filterProxy: root.host ? root.host.doFolderProxy : null
                        }
                        Item {
                            id: previewSurface
                            objectName: "doContentSurface"
                            anchors.fill: previewPane
                            anchors.margins: Theme.space(5)
                            visible: root.host && root.host.doPreviewItem
                            Component.onCompleted: if (root.host && root.host.registerDoContentSurface)
                                root.host.registerDoContentSurface(previewSurface)
                        }
                        FileMark {
                            anchors.centerIn: previewPane
                            width: Math.min(previewPane.width, previewPane.height) * 0.32
                            height: Math.round(width * 1.16)
                            visible: root.host && !root.host.doTargetIsDir && !root.host.doPreviewItem
                            suffix: {
                                var n = root.host ? root.host.doTargetName : ""
                                var i = n.lastIndexOf(".")
                                return i > 0 && i < n.length - 1 ? n.slice(i + 1).toUpperCase() : ""
                            }
                        }
                        Rectangle {
                            anchors.left: previewPane.left
                            anchors.right: previewPane.right
                            anchors.bottom: previewPane.bottom
                            height: Theme.space(24)
                            color: Theme.alpha(Theme.darkerBackground, 0.82)
                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Theme.space(8)
                                anchors.rightMargin: Theme.space(8)
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.host ? root.host.doTargetName : ""
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideMiddle
                            }
                        }
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.left: previewPane.right
                            width: 1
                            color: Theme.alpha(Theme.normalBorder, 0.54)
                        }

                        DoMetadata {
                            objectName: "doMetadata"
                            anchors.top: parent.top
                            anchors.left: previewPane.right
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 1
                            metadata: root.host ? root.host.doMetadata : ({})
                        }
                    }

                    Item {
                        id: paramSurface
                        objectName: "doParamSurface"
                        anchors.top: contentBox.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.topMargin: visible ? Theme.space(10) : 0
                        visible: root.host && root.host.doHasParams
                        Rectangle {
                            anchors.fill: parent
                            color: root.paramsOn ? Theme.focusFill : Theme.alpha(Theme.darkerBackground, 0.24)
                            border.color: root.paramsOn ? Theme.focusBorder : Theme.alpha(Theme.normalBorder, 0.55)
                            border.width: 1
                            radius: Theme.radius
                        }
                        Component.onCompleted: if (root.host && root.host.registerDoSurface)
                            root.host.registerDoSurface(paramSurface)
                        TapHandler { onTapped: if (root.host) root.host.doParamsFocused = true }
                    }
                }

                Item {
                    id: receipt
                    anchors.fill: parent
                    anchors.margins: Theme.space(18)
                    visible: root.operationVisible

                    Text {
                        id: receiptEyebrow
                        anchors.top: parent.top
                        anchors.left: parent.left
                        text: root.operationRunning ? "IN FLIGHT" :
                              (root.host && root.host.doOperationState === "succeeded" ? "COMPLETE" : "NEEDS ATTENTION")
                        color: root.host && root.host.doOperationState === "failed" ? Theme.urgent : Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        font.letterSpacing: 1.2
                    }
                    Text {
                        id: receiptTitle
                        anchors.top: receiptEyebrow.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space(6)
                        text: root.host ? root.host.doOperationTitle : ""
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Item {
                        id: route
                        anchors.top: receiptTitle.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space(14)
                        height: Theme.space(64)
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.space(18)
                            anchors.rightMargin: Theme.space(18)
                            height: 1
                            color: Theme.alpha(Theme.accent, 0.38)
                        }
                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                required property int index
                                x: Theme.space(8) + index * ((route.width - Theme.space(32)) / 2)
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.space(index === 1 ? 12 : 16)
                                height: width
                                radius: width / 2
                                color: root.host && root.host.doOperationState === "failed" && index === 2 ? Theme.urgent : Theme.accent
                                border.color: Theme.foreground
                                border.width: 1
                                opacity: !root.operationRunning ? 1 : 0.25 + 0.75 * Math.max(0,
                                         1 - Math.abs(((Theme.thumbnailPhase + index * 0.33) % 1) - 0.5) * 3)
                            }
                        }
                        Text { anchors.left: parent.left; anchors.bottom: parent.bottom; text: "SELECTION"; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontCaption }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "FLOW"; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontCaption }
                        Text { anchors.right: parent.right; anchors.bottom: parent.bottom; text: "RESULT"; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontCaption }
                    }
                    Text {
                        id: receiptSummary
                        anchors.top: route.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space(12)
                        text: root.host ? root.host.doOperationSummary : ""
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }
                    ListView {
                        id: artifacts
                        anchors.top: receiptSummary.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.topMargin: Theme.space(12)
                        clip: true
                        spacing: Theme.space(4)
                        model: root.host ? root.host.doOperationArtifacts : []
                        visible: count > 0
                        delegate: Rectangle {
                            id: artifactRow
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: Theme.space(34)
                            color: artifactHover.hovered ? Theme.hoverFill : Theme.normalFill
                            border.color: artifactHover.hovered ? Theme.hoverBorder : Theme.normalBorder
                            border.width: 1
                            radius: Theme.radius
                            Text {
                                anchors.left: parent.left
                                anchors.right: artifactType.left
                                anchors.leftMargin: Theme.space(10)
                                anchors.rightMargin: Theme.space(8)
                                anchors.verticalCenter: parent.verticalCenter
                                text: artifactRow.modelData.label || artifactRow.modelData.path || "Result"
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBodySmall
                                elide: Text.ElideMiddle
                            }
                            Text {
                                id: artifactType
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space(10)
                                anchors.verticalCenter: parent.verticalCenter
                                text: (artifactRow.modelData.kind || "file").toUpperCase()
                                color: Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                            }
                            HoverHandler { id: artifactHover }
                            TapHandler { onTapped: if (root.host) root.host.revealDoArtifact(artifactRow.index) }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.space(50)
            color: Theme.alpha(Theme.darkerBackground, 0.38)
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.alpha(Theme.popupBorder, 0.32) }
            Text {
                id: hint
                objectName: "doHint"
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(14)
                anchors.right: buttonRow.left
                anchors.rightMargin: Theme.space(10)
                anchors.verticalCenter: parent.verticalCenter
                text: root.host && root.host.doHint ? root.host.doHint : "↑/↓ actions · Enter run · Esc close"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
            Row {
                id: buttonRow
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(10)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(6)
                ChromeButton {
                    visible: !root.operationVisible && root.host && root.host.doProvider === "FLOW"
                    enabled: !root.operationRunning
                    label: "Inspect plan"
                    fallbackGlyph: "?"
                    onTriggered: if (root.host) root.host.inspectDoVerb()
                }
                ChromeButton {
                    visible: root.operationRunning
                    label: "Cancel"
                    fallbackGlyph: "×"
                    onTriggered: if (root.host) root.host.cancelDoOperation()
                }
                ChromeButton {
                    visible: root.operationVisible && !root.operationRunning
                    label: "Back"
                    fallbackGlyph: "‹"
                    onTriggered: if (root.host) root.host.clearDoOperation()
                }
                ChromeButton {
                    visible: !root.operationRunning
                    checked: true
                    label: root.operationVisible ? "Done" : (root.host && root.host.doArmed ? "Confirm" : "Run")
                    fallbackGlyph: root.operationVisible ? "✓" : (root.host && root.host.doArmed ? "!" : "↵")
                    onTriggered: if (root.host) root.host.runDoVerb()
                }
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
