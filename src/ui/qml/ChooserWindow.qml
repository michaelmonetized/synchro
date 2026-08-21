import QtQuick
import Synchro.Theme

Window {
    id: root

    required property var chooser

    readonly property var files: chooser.directoryModel
    readonly property var listing: chooser.filterProxy
    readonly property var history: chooser.navStack
    readonly property var keys: chooser.keyMachine
    readonly property var selection: chooser.selectionModel

    width: 875
    height: 600
    minimumWidth: 480
    minimumHeight: 320
    visible: false
    title: chooser.title
    color: Theme.background
    flags: Qt.Dialog

    onClosing: function (event) {
        if (!chooser)
            return
        chooser.cancel()
    }

    PathBar {
        id: pathBar
        objectName: "pathBar"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        fileModel: root.files
        navStack: root.history
        locationChips: chooser.locationChips
    }

    CommandField {
        id: commandField
        objectName: "chooserCommandField"
        anchors.top: pathBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        enabled: !chooser.overwriteOpen
        keyMachine: root.keys
        fileModel: root.files
    }

    readonly property bool gridMode: root.keys ? root.keys.gridMode : false

    Rectangle {
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        color: Theme.opaqueBackground
        z: 0
    }

    Loader {
        id: listingLoader
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        z: 1
        enabled: !chooser.overwriteOpen
        sourceComponent: root.gridMode ? gridComp : listComp
        onLoaded: {
            if (root.gridMode && item && root.keys)
                root.keys.gridStride = item.columns
            if (root.keys && root.keys.listFocused && item)
                item.forceActiveFocus()
        }
    }

    Component {
        id: listComp
        FileList {
            objectName: "chooserFileList"
            fileModel: root.files
            filterProxy: root.listing
            navStack: root.history
            keyMachine: root.keys
            selection: root.selection
            host: chooser.hostApi
            dndEnabled: false
            onViewToggleRequested: if (root.keys) root.keys.gridMode = true
            onDoRequested: if (chooser.hostApi) chooser.hostApi.openDoLayer()
        }
    }

    Component {
        id: gridComp
        FileGrid {
            objectName: "chooserFileGrid"
            fileModel: root.files
            filterProxy: root.listing
            navStack: root.history
            keyMachine: root.keys
            selection: root.selection
            host: chooser.hostApi
            dndEnabled: false
            onViewToggleRequested: if (root.keys) root.keys.gridMode = false
            onDoRequested: if (chooser.hostApi) chooser.hostApi.openDoLayer()
        }
    }

    StatusLine {
        id: statusLine
        objectName: "chooserStatusLine"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: extraBar.top
        fileModel: root.files
        keyMachine: root.keys
        selection: root.selection
        filterProxy: root.listing
        extra: chooser.destPreview ? chooser.destPreview : ""
        host: chooser.hostApi
    }

    PeekOverlay {
        anchors.fill: parent
        host: chooser.hostApi
        keys: root.keys
        indexModel: root.listing
    }

    // Dialog windows steal focus to the first TextInput. Keep peek keys
    // on a sink so Q / j / k still reach KeyMachine.
    Item {
        id: peekKeys
        objectName: "chooserPeekKeys"
        anchors.fill: parent
        z: 101
        focus: root.keys && root.keys.peekOpen && !chooser.overwriteOpen
        enabled: focus
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: function (event) {
            if (!root.keys || !root.keys.peekOpen)
                return
            if (root.keys.handleListKey(event.key, event.modifiers, event.text))
                event.accepted = true
        }
    }

    DoOverlay {
        anchors.fill: parent
        host: chooser.hostApi
        onClosed: {
            if (root.keys && root.keys.fieldFocused)
                commandField.focusInput()
            else
                root.focusListing()
        }
    }

    Rectangle {
        id: extraBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: actionBar.top
        height: extraCol.implicitHeight + (extraCol.implicitHeight > 0 ? Theme.space(8) : 0)
        color: Theme.background

        Column {
            id: extraCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            spacing: Theme.space(6)

            Repeater {
                model: chooser.choiceModels
                delegate: Row {
                    required property var modelData
                    required property int index
                    spacing: Theme.space(8)
                    width: extraCol.width

                    Text {
                        text: modelData.label
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: modelData.optionLabels && modelData.optionLabels.length
                              ? modelData.value
                              : (modelData.value === "true" ? "[x]" : "[ ]")
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        anchors.verticalCenter: parent.verticalCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.optionIds && modelData.optionIds.length) {
                                    var i = modelData.optionIds.indexOf(modelData.value)
                                    i = (i + 1) % modelData.optionIds.length
                                    chooser.setChoiceValue(index, modelData.optionIds[i])
                                } else {
                                    chooser.setChoiceValue(index,
                                        modelData.value === "true" ? "false" : "true")
                                }
                            }
                        }
                    }
                }
            }

            Row {
                id: saveRow
                visible: chooser.showSaveName
                spacing: Theme.space(8)
                width: extraCol.width

                Text {
                    id: saveLabel
                    text: "Name"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextInput {
                    id: saveName
                    objectName: "chooserSaveName"
                    width: Math.max(40, saveRow.width - saveLabel.implicitWidth - saveRow.spacing)
                    color: Theme.foreground
                    selectedTextColor: Theme.background
                    selectionColor: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    text: chooser.saveName
                    selectByMouse: true
                    enabled: !chooser.overwriteOpen
                    onTextEdited: chooser.saveName = text
                    Keys.onReturnPressed: chooser.accept()
                    Keys.onEnterPressed: chooser.accept()
                    Keys.onEscapePressed: function (event) {
                        root.focusListing()
                        event.accepted = true
                    }
                    Keys.onTabPressed: function (event) {
                        root.focusListing()
                        event.accepted = true
                    }
                    Keys.onBacktabPressed: function (event) {
                        root.focusListing()
                        event.accepted = true
                    }
                }
            }
        }
    }

    Rectangle {
        id: actionBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.max(Theme.fontBody + Theme.space(16), 36)
        color: Theme.background

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.normalBorder
        }

        Text {
            id: filterLabel
            visible: chooser.showFilters
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            text: chooser.filterNames.length
                  ? chooser.filterNames[chooser.currentFilterIndex]
                  : ""
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (!chooser.filterNames.length)
                        return
                    chooser.currentFilterIndex =
                            (chooser.currentFilterIndex + 1) % chooser.filterNames.length
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(8)

            Rectangle {
                implicitWidth: cancelLabel.implicitWidth + Theme.space(16)
                implicitHeight: cancelLabel.implicitHeight + Theme.space(8)
                color: cancelHover.hovered ? Theme.hoverFill : "transparent"
                border.width: 1
                border.color: Theme.normalBorder
                radius: Theme.radius

                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                HoverHandler { id: cancelHover }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (chooser.overwriteOpen)
                            chooser.dismissOverwrite()
                        else
                            chooser.cancel()
                    }
                }
            }

            Rectangle {
                implicitWidth: acceptLabel.implicitWidth + Theme.space(16)
                implicitHeight: acceptLabel.implicitHeight + Theme.space(8)
                color: acceptHover.hovered ? Theme.hoverFill : Theme.selectedFill
                border.width: 1
                border.color: Theme.normalBorder
                radius: Theme.radius

                Text {
                    id: acceptLabel
                    anchors.centerIn: parent
                    text: chooser.acceptLabel
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                HoverHandler { id: acceptHover }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (chooser.overwriteOpen)
                            chooser.confirmOverwrite()
                        else
                            chooser.accept()
                    }
                }
            }
        }
    }

    Item {
        id: overwrite
        objectName: "overwritePrompt"
        anchors.fill: parent
        visible: chooser.overwriteOpen
        enabled: visible
        focus: visible
        activeFocusOnTab: true
        z: 20

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_Escape) {
                chooser.dismissOverwrite()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                chooser.confirmOverwrite()
                event.accepted = true
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onClicked: overwrite.forceActiveFocus()
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.background
            opacity: 0.72
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.space(32), 420)
            implicitHeight: overwriteCol.implicitHeight + Theme.space(24)
            color: Theme.background
            border.width: 1
            border.color: Theme.normalBorder
            radius: Theme.radius

            Column {
                id: overwriteCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.space(12)
                anchors.rightMargin: Theme.space(12)
                spacing: Theme.space(10)

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Overwrite " + chooser.overwriteName + "?"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Row {
                    spacing: Theme.space(8)
                    anchors.right: parent.right

                    Text {
                        text: "Cancel"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chooser.dismissOverwrite()
                        }
                    }

                    Text {
                        text: "Overwrite"
                        color: Theme.urgent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: chooser.confirmOverwrite()
                        }
                    }
                }
            }
        }

        Shortcut {
            enabled: overwrite.visible
            sequence: "Escape"
            onActivated: chooser.dismissOverwrite()
        }

        Shortcut {
            enabled: overwrite.visible
            sequence: "Return"
            onActivated: chooser.confirmOverwrite()
        }

        Shortcut {
            enabled: overwrite.visible
            sequence: "Enter"
            onActivated: chooser.confirmOverwrite()
        }
    }

    Shortcut {
        sequence: "Ctrl+K"
        enabled: root.keys && !root.keys.peekOpen && !chooser.overwriteOpen
        onActivated: root.keys.focusFilter()
    }

    Shortcut {
        sequence: "Ctrl+L"
        enabled: root.keys && !root.keys.peekOpen && !chooser.overwriteOpen
        onActivated: root.keys.focusJump()
    }

    Item {
        id: helpOverlay
        objectName: "chooserHelpOverlay"
        anchors.fill: parent
        z: 120
        visible: root.keys && root.keys.helpOpen
        focus: false

        Rectangle {
            anchors.fill: parent
            color: Theme.background
            opacity: 0.9
            MouseArea {
                anchors.fill: parent
                onClicked: if (root.keys) root.keys.escape()
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.space(48), 560)
            height: Math.min(parent.height - Theme.space(48), 300)
            color: Theme.background
            border.color: Theme.normalBorder
            border.width: 1

            Text {
                anchors.fill: parent
                anchors.margins: Theme.space(16)
                text: root.keys ? root.keys.helpText : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }
        }
    }

    function focusListing() {
        if (chooser.overwriteOpen) {
            overwrite.forceActiveFocus()
            return
        }
        if (root.keys && root.keys.peekOpen) {
            peekKeys.forceActiveFocus()
            return
        }
        if (root.keys && !root.keys.listFocused)
            return
        if (listingLoader.item)
            listingLoader.item.forceActiveFocus()
    }

    function focusSaveName() {
        if (!chooser.showSaveName || chooser.overwriteOpen)
            return
        saveName.forceActiveFocus()
        saveName.selectAll()
    }

    Connections {
        target: root.keys
        function onModeChanged() {
            if (chooser.overwriteOpen) {
                overwrite.forceActiveFocus()
                return
            }
            if (root.keys.peekOpen || root.keys.listFocused)
                root.focusListing()
            else
                commandField.focusInput()
        }
        function onGridModeChanged() {
            if (!root.gridMode && root.keys)
                root.keys.gridStride = 1
            Qt.callLater(root.focusListing)
        }
        function onSaveNameFocusRequested() {
            root.focusSaveName()
        }
    }

    Connections {
        target: root.files
        function onPathChanged() {
            if (chooser.overwriteOpen)
                return
            root.focusListing()
        }
    }

    Connections {
        target: chooser
        function onOverwriteOpenChanged() {
            if (chooser.overwriteOpen)
                overwrite.forceActiveFocus()
            else
                root.focusListing()
        }
    }

    onActiveChanged: if (active)
        Qt.callLater(root.focusListing)

    Component.onCompleted: Qt.callLater(root.focusListing)
}
