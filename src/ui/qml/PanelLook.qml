import QtQuick
import Synchro.Theme

// Ambient, non-blocking preview companion for docked panel apps. Rich content
// is admitted by handler manifest; otherwise this surface only uses metadata
// and thumbnails already owned by the browser model.
Item {
    id: root

    property var host: null
    property var fileModel: null
    property var selectionModel: null
    property bool horizontalSplit: true
    readonly property bool hovered: hover.hovered
    readonly property var stat: host && host.inlinePreviewStat
                                ? host.inlinePreviewStat : ({})
    readonly property string mode: host ? host.inlinePreviewMode : ""
    readonly property bool rich: mode === "rich" &&
                                 host && host.inlinePreviewItem
    readonly property bool corePreview: mode === "text" ||
                                        mode === "markdown" ||
                                        mode === "image"
    readonly property bool folderLens: mode === "folder" && host &&
                                       host.inlineFolderProxy
    property bool folderGridMode: true
    property string folderViewPath: ""
    property double coreRequestId: 0
    property var coreData: ({})
    signal collapseRequested()

    objectName: "panelLook"
    clip: true

    function schedule() {
        if (!visible || !host)
            return
        refreshTimer.restart()
    }

    function syncRichItem() {
        if (!host || !host.inlinePreviewItem)
            return
        var item = host.inlinePreviewItem
        item.parent = richMount
        item.x = 0
        item.y = 0
        item.width = richMount.width
        item.height = richMount.height
        item.visible = true
    }

    function loadCorePreview() {
        root.coreData = ({})
        if (!root.host || (root.mode !== "text" &&
                           root.mode !== "markdown"))
            return
        var url = root.stat && root.stat.uri ? root.stat.uri : ""
        if (!url || !url.toString().length)
            return
        root.coreData = ({ loading: true })
        root.coreRequestId = root.host.requestPreview(url, 65536, 0)
    }

    function syncFolderViewMode() {
        var path = root.folderLens && root.stat && root.stat.path
                   ? String(root.stat.path) : ""
        if (!root.folderLens) {
            root.folderViewPath = ""
            return
        }
        if (root.folderViewPath !== path) {
            root.folderViewPath = path
            root.folderGridMode = true
        }
    }

    function humanSize(bytes) {
        var n = Number(bytes)
        if (!isFinite(n) || n < 0)
            return ""
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024)
            return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function suffix() {
        var name = stat && stat.name ? String(stat.name) : ""
        var dot = name.lastIndexOf(".")
        return dot > 0 ? name.substring(dot + 1).toLowerCase() : ""
    }

    onVisibleChanged: {
        if (!host)
            return
        host.setInlinePreviewActive(visible)
        if (visible)
            schedule()
    }
    onHostChanged: if (host && visible) host.setInlinePreviewActive(true)
    Component.onCompleted: {
        if (host && visible)
            host.setInlinePreviewActive(true)
        schedule()
    }
    Component.onDestruction: if (host) host.setInlinePreviewActive(false)

    Timer {
        id: refreshTimer
        interval: 150
        repeat: false
        onTriggered: if (root.host && root.visible)
                         root.host.refreshInlinePreview()
    }

    Connections {
        target: root.fileModel
        function onCurrentStatChanged() { root.schedule() }
        function onPathChanged() { root.schedule() }
    }

    Connections {
        target: root.selectionModel
        function onSelectionChanged() { root.schedule() }
    }

    Connections {
        target: root.host
        function onInlinePreviewChanged() {
            Qt.callLater(root.syncFolderViewMode)
            Qt.callLater(root.syncRichItem)
            Qt.callLater(root.loadCorePreview)
        }
        function onPreviewReady(requestId, file, preview) {
            if (requestId === root.coreRequestId)
                root.coreData = preview
        }
        function onOpenChanged() {
            if (!root.host.open)
                root.schedule()
        }
    }

    HoverHandler { id: hover }

    Rectangle {
        anchors.fill: parent
        color: Theme.darkerBackground

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.alpha(Theme.accent, 0.35)
        }
    }

    Rectangle {
        id: lookHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.controlHeight
        color: Theme.darkBackground

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spaceLG
            anchors.verticalCenter: parent.verticalCenter
            text: "LOOK"
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            font.letterSpacing: 1
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(58)
            anchors.right: actions.left
            anchors.rightMargin: Theme.spaceLG
            anchors.verticalCenter: parent.verticalCenter
            text: root.mode === "multi"
                  ? root.host.inlinePreviewCount + " selected"
                  : (root.folderLens
                     ? (root.stat.name + "  /  " +
                        root.host.inlineFolderProxy.count +
                        (root.host.inlineFolderTruncated ? "+" : ""))
                     : (root.stat && root.stat.name ? root.stat.name : ""))
            color: Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideMiddle
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceSM
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spaceSM

            ChromeButton {
                objectName: "millerGridToggle"
                visible: root.folderLens
                height: Theme.space(22)
                compact: true
                iconName: "view-grid-symbolic"
                fallbackGlyph: "▦"
                checked: root.folderGridMode
                toolTip: "Miller grid · V"
                onTriggered: {
                    root.folderGridMode = true
                    folderLensView.forceActiveFocus()
                }
            }

            ChromeButton {
                objectName: "millerListToggle"
                visible: root.folderLens
                height: Theme.space(22)
                compact: true
                iconName: "view-list-symbolic"
                fallbackGlyph: "☷"
                checked: !root.folderGridMode
                toolTip: "Miller list · V"
                onTriggered: {
                    root.folderGridMode = false
                    folderLensView.forceActiveFocus()
                }
            }

            ChromeButton {
                height: Theme.space(22)
                compact: true
                iconName: "view-fullscreen-symbolic"
                fallbackGlyph: "↗"
                toolTip: "Expand in Peek · Space"
                enabled: root.host && root.mode.length > 0 &&
                         root.mode !== "multi"
                onTriggered: root.host.promoteInlinePreview()
            }

            ChromeButton {
                height: Theme.space(22)
                compact: true
                iconName: "go-down-symbolic"
                fallbackGlyph: "—"
                toolTip: "Collapse Look"
                onTriggered: root.collapseRequested()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.normalBorder
            opacity: 0.55
        }
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: lookHeader.bottom
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceMD

        Item {
            id: richMount
            anchors.fill: parent
            visible: root.rich
            onWidthChanged: root.syncRichItem()
            onHeightChanged: root.syncRichItem()
        }

        Image {
            id: coreImage
            anchors.fill: parent
            anchors.margins: Theme.spaceMD
            visible: root.mode === "image"
            source: visible && root.stat && root.stat.uri ? root.stat.uri : ""
            sourceSize.width: Math.max(1, width)
            sourceSize.height: Math.max(1, height)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
        }

        Flickable {
            id: coreTextFlick
            anchors.fill: parent
            visible: root.mode === "text" || root.mode === "markdown"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: coreText.implicitHeight

            TextEdit {
                id: coreText
                width: coreTextFlick.width
                readOnly: true
                selectByMouse: true
                activeFocusOnPress: false
                persistentSelection: true
                text: root.coreData && root.coreData.ok
                      ? root.coreData.text
                      : (root.coreData && root.coreData.loading
                         ? "Loading…"
                         : (root.coreData && root.coreData.error
                            ? root.coreData.error : ""))
                textFormat: root.mode === "markdown"
                            ? TextEdit.MarkdownText : TextEdit.PlainText
                color: Theme.foreground
                selectedTextColor: Theme.background
                selectionColor: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBodySmall
                wrapMode: root.mode === "markdown" ? TextEdit.Wrap
                                                    : TextEdit.NoWrap
                padding: Theme.spaceLG
            }
        }

        FolderLens {
            id: folderLensView
            anchors.fill: parent
            visible: root.folderLens
            host: root.host
            folderModel: root.host ? root.host.inlineFolderModel : null
            folderProxy: root.host ? root.host.inlineFolderProxy : null
            loading: root.host ? root.host.inlineFolderLoading : false
            errorText: root.host ? root.host.inlineFolderError : ""
            truncated: root.host ? root.host.inlineFolderTruncated : false
            gridMode: root.folderGridMode
            onViewToggleRequested: root.folderGridMode =
                                   !root.folderGridMode
        }

        Item {
            id: card
            anchors.fill: parent
            visible: !root.rich && !root.corePreview && !root.folderLens

            Rectangle {
                anchors.fill: parent
                color: Theme.background
            }

            Image {
                id: thumb
                anchors.fill: parent
                anchors.margins: Theme.spaceLG
                source: root.stat && root.stat.thumbnail
                        ? root.stat.thumbnail : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                visible: source.toString().length > 0 &&
                         status !== Image.Error
            }

            FolderMark {
                width: Math.min(parent.width, parent.height) * 0.45
                height: width * 0.72
                anchors.centerIn: parent
                visible: !thumb.visible && !!(root.stat && root.stat.isDir)
                opacity: 0.72
            }

            FileMark {
                width: Math.min(parent.width, parent.height) * 0.36
                height: width * 1.25
                anchors.centerIn: parent
                suffix: root.suffix()
                visible: !thumb.visible && root.mode !== "multi" &&
                         !(root.stat && root.stat.isDir)
                opacity: 0.72
            }

            Column {
                anchors.centerIn: parent
                visible: root.mode === "multi"
                spacing: Theme.spaceSM

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.host ? root.host.inlinePreviewCount : 0
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontDisplay
                    font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "FILES SELECTED"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.letterSpacing: 1
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: meta.implicitHeight + Theme.space(14)
                color: Theme.alpha(Theme.darkerBackground, 0.93)
                visible: root.mode !== "multi" &&
                         !!(root.stat && root.stat.name)

                Row {
                    id: meta
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spaceLG
                    anchors.rightMargin: Theme.spaceLG
                    spacing: Theme.spaceLG

                    Text {
                        width: Math.max(0, parent.width - detail.implicitWidth -
                                        parent.spacing)
                        text: root.stat && root.stat.name ? root.stat.name : ""
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBodySmall
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    Text {
                        id: detail
                        text: root.stat && root.stat.isDir
                              ? "folder · Space to inspect"
                              : ((root.stat && root.stat.size >= 0
                                  ? root.humanSize(root.stat.size) + " · " : "") +
                                 "Space to inspect")
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                    }
                }
            }
        }
    }
}
