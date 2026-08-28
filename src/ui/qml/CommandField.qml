import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var keyMachine
    property var fileModel: null
    property var filterProxy: null
    property bool lookAvailable: false
    property bool lookHasRoom: true
    property bool lookHasSelection: false
    property bool lookOpen: true
    property bool agentAvailable: false
    signal lookToggleRequested()
    signal agentRequested()

    readonly property bool fieldActive: keyMachine && keyMachine.fieldFocused
    readonly property bool typingSearch: keyMachine && keyMachine.mode === "field-search"
    readonly property bool viewingSearch: fileModel && fileModel.isSearch
    readonly property bool contentSearch: {
        var t = keyMachine ? keyMachine.fieldText : ""
        return t.length >= 2 && t.charAt(0) === "?" && t.charAt(1) === "?"
    }
    readonly property bool viewToggle: keyMachine &&
                                       !keyMachine.fieldFocused &&
                                       !keyMachine.peekOpen &&
                                       !typingSearch && !viewingSearch &&
                                       keyMachine.mode !== "field-filter" &&
                                       keyMachine.mode !== "field-command" &&
                                       keyMachine.mode !== "field-jump"

    implicitHeight: Theme.controlHeight + Theme.spaceLG * 2

    function focusInput() {
        input.forceActiveFocus()
        if (input.text.length > 0)
            input.cursorPosition = input.text.length
    }

    function promptGlyph() {
        if (root.keyMachine && root.keyMachine.mode === "field-command") return ":"
        if (root.keyMachine && root.keyMachine.mode === "field-search")
            return root.contentSearch ? "??" : "?"
        if (root.keyMachine && root.keyMachine.mode === "field-jump") return "~"
        return "/"
    }

    function placeholder() {
        if (root.keyMachine && root.keyMachine.mode === "field-command")
            return "Run a Synchro command…"
        if (root.keyMachine && root.keyMachine.mode === "field-search")
            return root.contentSearch ? "Search inside files…" : "Search file names…"
        if (root.keyMachine && root.keyMachine.mode === "field-jump")
            return "Jump to a path…"
        return "Filter this folder  ·  / search  ·  : command"
    }

    function modeLabel() {
        if (root.keyMachine && root.keyMachine.statusMessage.length &&
                !(root.keyMachine.fsnMode &&
                  root.keyMachine.statusMessage === "it's a unix system"))
            return root.keyMachine.statusMessage
        if (root.typingSearch) return root.contentSearch ? "Content search" : "Name search"
        if (root.viewingSearch) return root.fileModel.isContentSearch ? "Content results"
                                                                      : "Search results"
        if (root.keyMachine && root.keyMachine.mode === "field-filter") return "Filtering"
        if (root.keyMachine && root.keyMachine.mode === "field-command") return "Command"
        if (root.keyMachine && root.keyMachine.mode === "field-jump") return "Path"
        return ""
    }

    function currentViewId() {
        if (!root.keyMachine)
            return "list"
        if (root.keyMachine.fsnMode)
            return root.keyMachine.fsnTreeView ? "tree" : "map"
        return root.keyMachine.gridMode ? "grid" : "list"
    }

    function activateView(id) {
        if (!root.keyMachine)
            return
        if (id === "tree" || id === "map") {
            root.keyMachine.fsnTreeView = id === "tree"
            root.keyMachine.fsnMode = true
        } else {
            root.keyMachine.fsnMode = false
            root.keyMachine.gridMode = id === "grid"
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    Row {
        id: rightControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.spaceLG
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.controlGap

        SegmentedControl {
            id: kindChips
            objectName: "kindChips"
            visible: root.viewToggle && !!root.filterProxy &&
                     !(root.keyMachine && root.keyMachine.fsnMode)
            showLabels: root.width >= Theme.space(700)
            currentId: root.filterProxy ? root.filterProxy.kindFilter : "all"
            options: [
                { id: "all", label: "All", glyph: "◆", tip: "Show files and folders" },
                { id: "files", label: "Files", glyph: "□", tip: "Show files only" },
                { id: "folders", label: "Folders", glyph: "▰", tip: "Show folders only" }
            ]
            onActivated: function(id) {
                if (root.filterProxy) root.filterProxy.kindFilter = id
            }
        }

        SegmentedControl {
            objectName: "viewControl"
            visible: root.viewToggle
            showLabels: false
            currentId: root.currentViewId()
            options: [
                { id: "list", label: "List", icon: "view-list-symbolic", glyph: "☷",
                  tip: "List view  ·  V" },
                { id: "grid", label: "Grid", icon: "view-grid-symbolic", glyph: "▦",
                  tip: "Grid view  ·  V" },
                { id: "tree", label: "StrataV", glyph: "Y",
                  tip: "StrataV 3D landscape  ·  Ctrl+M" },
                { id: "map", label: "MapV", glyph: "▱",
                  tip: "MapV 3D view  ·  M from StrataV" }
            ]
            onActivated: function(id) { root.activateView(id) }
        }

        ChromeButton {
            objectName: "browserAgentFind"
            visible: root.agentAvailable && root.viewToggle
            height: Theme.controlHeight
            label: root.width >= Theme.space(760) ? "Find" : ""
            fallbackGlyph: "✦"
            toolTip: "Find files with Omarchy's default agent  ·  :ask"
            onTriggered: root.agentRequested()
        }

        ChromeButton {
            objectName: "browserLookToggle"
            visible: root.lookAvailable && root.viewToggle
            height: Theme.controlHeight
            label: root.width >= Theme.space(660) ? "Look" : ""
            iconName: "xsi-preview-symbolic"
            fallbackGlyph: "◫"
            checked: root.lookOpen
            toolTip: !root.lookHasRoom
                     ? "Look is enabled · enlarge the window to show it"
                     : (!root.lookHasSelection && checked
                        ? "Look is enabled · select an item to preview"
                        : (checked ? "Hide Look preview"
                                   : "Show Look preview"))
            onTriggered: root.lookToggleRequested()
        }

        Rectangle {
            id: modePill
            visible: root.modeLabel().length > 0
            height: Theme.controlHeight
            width: Math.min(Theme.space(210), modeText.implicitWidth + Theme.controlPaddingX * 2)
            color: Theme.focusFill
            border.color: Theme.focusBorder
            border.width: 1
            radius: Theme.radius

            Text {
                id: modeText
                objectName: "commandModeHint"
                anchors.fill: parent
                anchors.leftMargin: Theme.controlPaddingX
                anchors.rightMargin: Theme.controlPaddingX
                text: root.modeLabel()
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        id: fieldSurface
        anchors.left: parent.left
        anchors.leftMargin: Theme.spaceLG
        anchors.right: rightControls.left
        anchors.rightMargin: Theme.controlGap
        anchors.verticalCenter: parent.verticalCenter
        height: Theme.controlHeight
        color: root.fieldActive ? Theme.focusFill : Theme.normalFill
        border.color: root.fieldActive ? Theme.focusBorder : Theme.normalBorder
        border.width: root.fieldActive ? Math.max(1, Theme.focusBorderWidth)
                                       : Theme.normalBorderWidth
        radius: Theme.radius

        Rectangle {
            id: promptBadge
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(Theme.controlHeight,
                            prompt.implicitWidth + Theme.controlPaddingX * 2)
            color: root.fieldActive || root.viewingSearch ? Theme.selectedFill
                                                         : Theme.normalFill
            border.color: "transparent"

            Text {
                id: prompt
                anchors.centerIn: parent
                text: root.promptGlyph()
                color: root.fieldActive || root.viewingSearch ? Theme.accent
                                                              : Theme.darkForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSubtitle
                font.bold: root.fieldActive
            }
        }

        TextInput {
            id: input
            objectName: "commandInput"
            anchors.left: promptBadge.right
            anchors.leftMargin: Theme.controlPaddingX
            anchors.right: parent.right
            anchors.rightMargin: Theme.controlPaddingX
            anchors.verticalCenter: parent.verticalCenter
            clip: true
            color: Theme.brightForeground
            selectedTextColor: Theme.background
            selectionColor: Theme.accent
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontBody
            selectByMouse: true
            activeFocusOnPress: true
            activeFocusOnTab: true
            focus: false
            text: {
                var t = root.keyMachine ? root.keyMachine.fieldText : ""
                if (root.keyMachine && root.keyMachine.mode === "field-command" &&
                        t.length > 0 && t.charAt(0) === ":")
                    return t.substring(1)
                if (root.keyMachine && root.keyMachine.mode === "field-search") {
                    if (t.length >= 2 && t.charAt(0) === "?" && t.charAt(1) === "?")
                        return t.substring(2)
                    if (t.length > 0 && t.charAt(0) === "?")
                        return t.substring(1)
                }
                return t
            }

            onTextEdited: {
                if (!root.keyMachine) return
                if (root.keyMachine.mode === "field-command")
                    root.keyMachine.fieldText = ":" + text
                else if (root.keyMachine.mode === "field-search") {
                    if (text.indexOf("??") === 0)
                        root.keyMachine.fieldText = text
                    else if (text.length > 0 && text.charAt(0) === "?")
                        root.keyMachine.fieldText = "?" + text
                    else if (root.contentSearch)
                        root.keyMachine.fieldText = "??" + text
                    else
                        root.keyMachine.fieldText = "?" + text
                } else
                    root.keyMachine.fieldText = text
            }

            onActiveFocusChanged: {
                if (activeFocus && root.keyMachine && root.keyMachine.listFocused &&
                        !root.keyMachine.peekOpen)
                    root.keyMachine.focusFilter()
            }

            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                if (root.keyMachine &&
                        root.keyMachine.handleFieldKey(event.key, event.modifiers))
                    event.accepted = true
            }
        }

        Text {
            visible: input.text.length === 0
            enabled: false
            anchors.left: input.left
            anchors.right: input.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.placeholder()
            color: Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            elide: Text.ElideRight
        }
    }

    Connections {
        target: root.keyMachine
        function onJumpEpochChanged() {
            if (root.keyMachine && root.keyMachine.fieldFocused) {
                input.forceActiveFocus()
                input.selectAll()
            }
        }
    }
}
