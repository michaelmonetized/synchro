import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var fileModel
    property var keyMachine
    property var selection
    property var filterProxy
    property var host: null
    property var fileOps: null
    property var catalog: null
    property string extra: ""

    readonly property var stat: fileModel && fileModel.currentStat
                                ? fileModel.currentStat : ({})
    readonly property bool hasSelection: !!(stat && stat.name)
    readonly property bool roomy: width >= Theme.space(760)
    readonly property bool catalogStatusActive: !!(root.catalog &&
        (root.catalog.indexing ||
         (root.keyMachine &&
          root.keyMachine.panelId === "synchro.panel.sql" &&
          root.catalog.statusText !== "current folder is live")))

    implicitHeight: Theme.controlHeight + Theme.spaceSM * 2

    function fmtSize(n) {
        if (n === undefined || n === null || n < 0) return "—"
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }

    function fmtTime(ms) {
        if (!ms) return ""
        var d = new Date(ms)
        if (isNaN(d.getTime())) return ""
        return d.toLocaleString(Qt.locale(), "MMM d, yyyy  h:mm AP")
    }

    function itemCountText() {
        if (root.selection && root.selection.statusText)
            return root.selection.statusText
        var count = root.filterProxy && root.filterProxy.count !== undefined
                    ? root.filterProxy.count
                    : (root.fileModel && root.fileModel.count !== undefined
                       ? root.fileModel.count : 0)
        var total = root.fileModel && root.fileModel.count !== undefined
                    ? root.fileModel.count : count
        if (count !== total) return count + " of " + total + " items"
        return count + (count === 1 ? " item" : " items")
    }

    function contextText() {
        if (root.fileOps && root.fileOps.busy)
            return root.fileOps.progressText || "Working…"
        if (root.catalogStatusActive)
            return root.catalog.statusText
        if (root.keyMachine && root.keyMachine.statusMessage)
            return root.keyMachine.statusMessage
        if (root.extra) return root.extra
        if (root.host && root.host.actionOpen && root.host.doHint) return root.host.doHint
        if (root.keyMachine && root.keyMachine.panelFocused) return "Ctrl+` returns to files"
        return root.hasSelection ? "Space toggles Look  ·  Shift+Space opens Peek"
                                 : "Press / to filter  ·  ? for help"
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.darkBackground
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.normalBorder
    }

    Row {
        id: leftInfo
        visible: !root.catalogStatusActive
        anchors.left: parent.left
        anchors.leftMargin: Theme.spaceLG
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spaceLG

        Text {
            objectName: "statusLineText"
            text: root.itemCountText()
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            font.bold: root.selection && root.selection.selectedCount > 1
        }

        Text {
            visible: root.fileModel && root.fileModel.showHidden
            text: "Hidden shown"
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
        }

        Text {
            visible: root.filterProxy && root.filterProxy.kindFilter !== "all"
            text: root.filterProxy ? root.filterProxy.kindFilter : ""
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
        }
    }

    Row {
        id: itemInfo
        visible: !root.catalogStatusActive && root.roomy && root.hasSelection
        anchors.left: leftInfo.right
        anchors.leftMargin: Theme.spaceXXL
        anchors.right: quickActions.left
        anchors.rightMargin: Theme.spaceLG
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spaceLG

        Text {
            width: Math.min(implicitWidth, Math.max(Theme.space(100), itemInfo.width * 0.38))
            text: root.stat.name || ""
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            font.bold: true
            elide: Text.ElideMiddle
        }

        Text {
            visible: root.stat.size !== undefined
            text: root.fmtSize(root.stat.size)
            color: Theme.darkForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontCaption
        }

        Text {
            visible: !!root.stat.mtime
            text: root.fmtTime(root.stat.mtime)
            color: Theme.darkForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontCaption
        }

        Text {
            visible: !!root.stat.perm && root.width >= Theme.space(1050)
            text: root.stat.perm || ""
            color: Theme.darkForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontCaption
        }
    }

    Row {
        id: quickActions
        anchors.right: context.left
        anchors.rightMargin: Theme.spaceLG
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spaceSM
        visible: !!(!root.catalogStatusActive && root.hasSelection &&
                    root.width >= Theme.space(650) &&
                    root.keyMachine && !root.keyMachine.panelFocused)

        ChromeButton {
            height: Theme.controlHeight - Theme.spaceSM
            compact: true
            iconName: "document-open-symbolic"
            fallbackGlyph: "↗"
            toolTip: "Open  ·  Enter"
            onTriggered: root.keyMachine.handleListKey(Qt.Key_Return, Qt.NoModifier, "")
        }
        ChromeButton {
            height: Theme.controlHeight - Theme.spaceSM
            compact: true
            iconName: "xsi-preview-symbolic"
            fallbackGlyph: "◉"
            toolTip: "Toggle Look  ·  Space"
            onTriggered: root.keyMachine.handleListKey(Qt.Key_Space, Qt.NoModifier, "")
        }
        ChromeButton {
            height: Theme.controlHeight - Theme.spaceSM
            compact: true
            iconName: "open-menu-symbolic"
            fallbackGlyph: "⋯"
            toolTip: "Actions  ·  Ctrl+Enter"
            onTriggered: root.keyMachine.handleListKey(Qt.Key_Return,
                                                       Qt.ControlModifier, "")
        }
    }

    Text {
        id: context
        anchors.right: parent.right
        anchors.rightMargin: Theme.spaceLG
        anchors.verticalCenter: parent.verticalCenter
        width: root.catalogStatusActive
               ? root.width - Theme.spaceLG * 2
               : Math.min(contextMetrics.advanceWidth,
                          root.roomy ? Theme.space(330) : root.width * 0.42)
        text: root.contextText()
        color: root.catalogStatusActive && root.catalog.indexing
               ? Theme.accent
               : (root.keyMachine && root.keyMachine.statusMessage.length
                  ? Theme.accent : Theme.darkForeground)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption
        fontSizeMode: root.catalogStatusActive ? Text.Fit : Text.FixedSize
        minimumPixelSize: Math.max(8, Theme.fontCaption - 2)
        horizontalAlignment: Text.AlignRight
        elide: root.catalogStatusActive ? Text.ElideNone : Text.ElideLeft
        wrapMode: Text.NoWrap
    }

    TextMetrics {
        id: contextMetrics
        text: context.text
        font: context.font
    }
}
