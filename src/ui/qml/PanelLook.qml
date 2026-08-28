import QtQuick
import QtMultimedia
import Synchro.Theme

// Ambient, non-blocking preview companion for docked panel apps. Rich content
// is admitted by handler manifest; otherwise this surface only uses metadata
// and thumbnails already owned by the browser model.
Item {
    id: root

    property var host: null
    property var fileModel: null
    property var filterProxy: null
    property var selectionModel: null
    property bool horizontalSplit: true
    property bool mainQueryBusy: false
    readonly property bool hovered: hover.hovered
    readonly property var stat: host && host.inlinePreviewStat
                                ? host.inlinePreviewStat : ({})
    readonly property string mode: host ? host.inlinePreviewMode : ""
    readonly property var imagePalette:
        stat && stat.imagePalette ? stat.imagePalette : []
    readonly property var imagePaletteWeights:
        stat && stat.imagePaletteWeights ? stat.imagePaletteWeights : []
    readonly property real imageSaturation:
        stat && stat.imageSaturation !== undefined
        ? Number(stat.imageSaturation) : 0
    readonly property bool imageBloomEnabled:
        mode === "image" && imagePalette.length > 0 &&
        imageSaturation >= 0.10
    readonly property string imagePaletteSignature:
        imagePalette.join("|") + ":" + imagePaletteWeights.join("|") +
        ":" + imageSaturation
    readonly property bool rich: mode === "rich" &&
                                 host && host.inlinePreviewItem
    readonly property bool corePreview: mode === "text" ||
                                        mode === "markdown" ||
                                        mode === "image" ||
                                        mode === "video"
    readonly property bool folderLens: mode === "folder" && host &&
                                       host.inlineFolderProxy
    readonly property int selectionEpoch: selectionModel
                                          ? selectionModel.epoch : 0
    property int multiRefreshEpoch: 0
    readonly property var multiSummary: {
        var epoch = root.selectionEpoch
        var refresh = root.multiRefreshEpoch
        if (root.mode !== "multi" || !root.selectionModel ||
                !root.selectionModel.previewSummary)
            return ({})
        return root.selectionModel.previewSummary(10, 10000)
    }
    readonly property var multiItems: multiSummary && multiSummary.items
                                      ? multiSummary.items : []
    readonly property int multiTileLimit:
        root.host && root.host.inlinePreviewCount <= 4 ? 4
        : (root.host && root.host.inlinePreviewCount <= 24 ? 8 : 10)
    readonly property var multiVisibleItems:
        multiItems && multiItems.slice
        ? multiItems.slice(0, multiTileLimit) : multiItems
    property bool folderGridMode: true
    property string folderViewPath: ""
    property double coreRequestId: 0
    property var coreData: ({})
    property bool videoActivated: false
    property string videoIdentity: ""
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
        root.coreRequestId = root.host.requestPreview(
                    url, 65536, 0,
                    root.mode === "markdown" ? Theme.markdownStyle() : ({}))
    }

    function toggleVideoPlayback() {
        if (root.mode !== "video")
            return
        if (!root.videoActivated) {
            root.videoActivated = true
            return
        }
        var player = videoLoader.item
        if (!player)
            return
        if (player.playbackState === MediaPlayer.PlayingState)
            player.pause()
        else
            player.play()
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

    function multiDetail() {
        var s = root.multiSummary || ({})
        var parts = []
        if (s.kindSummary && s.kindSummary.length)
            parts.push(s.kindSummary.join("  ·  "))
        if (Number(s.bytes) > 0) {
            var size = root.humanSize(Number(s.bytes))
            parts.push(s.sizeComplete ? size : size + " sampled")
        }
        if (s.aggregateComplete === false)
            parts.push(Number(s.aggregateCount).toLocaleString() +
                       " inspected")
        return parts.join("  ·  ")
    }

    onVisibleChanged: {
        if (!host)
            return
        host.setInlinePreviewActive(visible)
        if (visible)
            schedule()
        else
            videoActivated = false
    }
    onHostChanged: if (host && visible) host.setInlinePreviewActive(true)
    Component.onCompleted: {
        if (host) {
            videoIdentity = host.inlinePreviewMode + "\n" +
                            host.inlinePreviewPath
        }
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

    Timer {
        id: multiRefreshTimer
        interval: 90
        repeat: false
        onTriggered: root.multiRefreshEpoch++
    }

    Connections {
        target: root.fileModel
        function onCurrentStatChanged() { root.schedule() }
        function onPathChanged() { root.schedule() }
        function onDataChanged() {
            if (root.visible && root.mode === "multi")
                multiRefreshTimer.restart()
        }
    }

    Connections {
        target: root.selectionModel
        function onSelectionChanged() {
            if (!root.host)
                return
            if (!root.selectionModel || root.selectionModel.selectedCount <= 0) {
                root.host.setInlinePreviewActive(false)
                return
            }
            if (root.visible)
                root.host.setInlinePreviewActive(true)
            root.schedule()
        }
    }

    Connections {
        target: root.host
        function onInlinePreviewChanged() {
            var nextIdentity = root.host.inlinePreviewMode + "\n" +
                               root.host.inlinePreviewPath
            if (root.videoIdentity !== nextIdentity) {
                root.videoIdentity = nextIdentity
                root.videoActivated = false
            }
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
            text: root.mode === "card" || root.mode === "multi"
                  ? ""
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
                toolTip: "Expand in Peek · Shift+Space"
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

        Item {
            id: imageStage
            objectName: "lookImageStage"
            anchors.fill: parent
            visible: root.mode === "image"

            Canvas {
                id: imageBloom
                objectName: "lookImageBloom"
                anchors.fill: parent
                visible: root.imageBloomEnabled
                opacity: 1.0
                renderStrategy: Canvas.Cooperative
                property string paletteSignature:
                    root.imagePaletteSignature + ":" + Theme.epoch

                onPaletteSignatureChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onVisibleChanged: if (visible) requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    if (!root.imageBloomEnabled || width <= 0 || height <= 0)
                        return
                    var positions = [[0.12, 0.22], [0.88, 0.40], [0.46, 0.92]]
                    var radius = Math.max(width, height) * 0.82
                    for (var i = 0; i < root.imagePalette.length && i < 3; ++i) {
                        var color = Qt.color(root.imagePalette[i])
                        var hi = Math.max(color.r, color.g, color.b)
                        var lo = Math.min(color.r, color.g, color.b)
                        var saturation = hi > 0 ? (hi - lo) / hi : 0
                        if (saturation < 0.10)
                            continue
                        var weight = i < root.imagePaletteWeights.length
                                     ? Number(root.imagePaletteWeights[i]) : 0.25
                        var alpha = Math.min(0.31,
                                             0.11 + Math.sqrt(Math.max(0, weight)) * 0.17)
                        var red = Math.round(color.r * 255)
                        var green = Math.round(color.g * 255)
                        var blue = Math.round(color.b * 255)
                        var x = width * positions[i][0]
                        var y = height * positions[i][1]
                        var gradient = ctx.createRadialGradient(x, y, 0,
                                                               x, y, radius)
                        gradient.addColorStop(0, "rgba(" + red + "," + green +
                                              "," + blue + "," + alpha + ")")
                        gradient.addColorStop(0.52, "rgba(" + red + "," + green +
                                              "," + blue + "," + (alpha * 0.52) + ")")
                        gradient.addColorStop(1, "rgba(" + red + "," + green +
                                              "," + blue + ",0)")
                        ctx.fillStyle = gradient
                        ctx.fillRect(0, 0, width, height)
                    }
                }
            }

            Image {
                id: coreImage
                anchors.fill: parent
                anchors.margins: Theme.spaceMD
                source: imageStage.visible && root.stat && root.stat.uri
                        ? root.stat.uri : ""
                sourceSize.width: Math.max(1, width)
                sourceSize.height: Math.max(1, height)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
            }
        }

        Item {
            id: videoStage
            objectName: "lookVideoStage"
            anchors.fill: parent
            visible: root.mode === "video"
            clip: true

            Image {
                id: videoPoster
                anchors.fill: parent
                anchors.margins: Theme.spaceMD
                source: videoStage.visible && root.stat && root.stat.thumbnail
                        ? root.stat.thumbnail : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                visible: !root.videoActivated && source.toString().length > 0 &&
                         status !== Image.Error
            }

            FileMark {
                width: Math.min(parent.width, parent.height) * 0.28
                height: width * 1.25
                anchors.centerIn: parent
                suffix: root.suffix()
                visible: !root.videoActivated && !videoPoster.visible
                opacity: 0.62
            }

            Loader {
                id: videoLoader
                objectName: "lookVideoLoader"
                anchors.fill: parent
                active: videoStage.visible && root.videoActivated
                sourceComponent: Component {
                    Video {
                        objectName: "lookVideoPlayer"
                        source: root.stat && root.stat.uri ? root.stat.uri : ""
                        fillMode: VideoOutput.PreserveAspectFit
                        muted: true
                        volume: 0
                        autoPlay: true
                        loops: MediaPlayer.Infinite
                        focus: false
                        endOfStreamPolicy: VideoOutput.KeepLastFrame
                    }
                }
            }

            Rectangle {
                id: playAffordance
                anchors.centerIn: parent
                width: playLabel.implicitWidth + Theme.spaceXL
                height: Theme.space(38)
                radius: height / 2
                color: Theme.alpha(Theme.darkBackground, 0.88)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.72)
                visible: !root.videoActivated

                Text {
                    id: playLabel
                    anchors.centerIn: parent
                    text: "▶  PLAY VIDEO"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 0.8
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spaceMD
                width: playerState.implicitWidth + Theme.spaceLG
                height: Theme.space(26)
                radius: height / 2
                color: Theme.alpha(Theme.darkBackground, 0.78)
                visible: root.videoActivated && videoLoader.item

                Text {
                    id: playerState
                    anchors.centerIn: parent
                    text: videoLoader.item &&
                          videoLoader.item.playbackState ===
                          MediaPlayer.PlayingState
                          ? "PAUSE" : "PLAY"
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleVideoPlayback()
            }
        }

        Flickable {
            id: coreTextFlick
            anchors.fill: parent
            visible: root.mode === "text" || root.mode === "markdown"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: root.mode === "markdown"
                          ? width : Math.max(width, coreText.implicitWidth)
            contentHeight: coreText.implicitHeight

            TextEdit {
                id: coreText
                objectName: "inlineLookText"
                width: root.mode === "markdown"
                       ? Math.min(coreTextFlick.width,
                                  Math.max(420, Theme.fontBody * 58))
                       : coreTextFlick.contentWidth
                x: root.mode === "markdown"
                   ? Math.max(0, (coreTextFlick.width - width) / 2) : 0
                readOnly: true
                selectByMouse: true
                activeFocusOnPress: false
                persistentSelection: true
                text: root.coreData && root.coreData.ok
                      ? (root.mode === "markdown" &&
                         root.coreData.markdownHtml
                         ? root.coreData.markdownHtml
                         : (root.mode === "text" && root.coreData.highlighted &&
                         root.coreData.html
                         ? root.coreData.html : root.coreData.text))
                      : (root.coreData && root.coreData.loading
                         ? "Loading…"
                         : (root.coreData && root.coreData.error
                            ? root.coreData.error : ""))
                textFormat: root.mode === "markdown" &&
                            root.coreData && root.coreData.markdownHtml
                            ? TextEdit.RichText
                            : root.mode === "markdown"
                            ? TextEdit.MarkdownText
                            : (root.coreData && root.coreData.highlighted &&
                               root.coreData.html
                               ? TextEdit.RichText : TextEdit.PlainText)
                color: Theme.foreground
                selectedTextColor: Theme.background
                selectionColor: Theme.accent
                font.family: root.mode === "markdown" ? Theme.fontFamily
                                                      : Theme.monoFontFamily
                font.pixelSize: root.mode === "markdown"
                                ? Theme.fontBody : Theme.fontBodySmall
                wrapMode: root.mode === "markdown" ? TextEdit.Wrap
                                                    : TextEdit.NoWrap
                padding: root.mode === "markdown"
                         ? Theme.space(10) : Theme.spaceLG
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
            queryBusy: root.mainQueryBusy ||
                       (root.host ? root.host.inlineFolderLoading : false)
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
                id: multiCard
                objectName: "lookMultiCard"
                anchors.fill: parent
                anchors.margins: Theme.spaceXL
                visible: root.mode === "multi"
                spacing: Theme.spaceSM

                Text {
                    width: parent.width
                    text: root.host
                          ? Number(root.host.inlinePreviewCount).toLocaleString()
                          : "0"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: "SELECTED"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: root.multiDetail()
                    visible: text.length > 0
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Item {
                    id: mosaicArea
                    width: parent.width
                    height: Math.max(0, parent.height - y)

                    Grid {
                        id: multiMosaic
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: Theme.spaceXL
                        spacing: Theme.spaceSM
                        readonly property int itemCount:
                            root.multiVisibleItems
                            ? root.multiVisibleItems.length : 0
                        readonly property int columnCount:
                            itemCount <= 4 ? Math.max(1, Math.min(2, itemCount))
                            : (itemCount <= 8 ? 4 : 5)
                        readonly property int rowCount:
                            Math.max(1, Math.ceil(itemCount / columnCount))
                        columns: columnCount

                        Repeater {
                            model: root.multiVisibleItems

                            delegate: Rectangle {
                                id: multiTile
                                required property var modelData
                                readonly property string tileName:
                                    modelData && modelData.name
                                    ? String(modelData.name) : ""
                                readonly property string tileSuffix: {
                                    var dot = tileName.lastIndexOf(".")
                                    return dot > 0
                                           ? tileName.substring(dot + 1)
                                                     .toLowerCase() : ""
                                }
                                width: Math.max(Theme.space(42),
                                    (multiMosaic.width -
                                     multiMosaic.spacing *
                                     (multiMosaic.columnCount - 1)) /
                                     multiMosaic.columnCount)
                                height: Math.max(Theme.space(56),
                                    Math.min(Theme.space(124),
                                        (mosaicArea.height -
                                         multiMosaic.spacing *
                                         (multiMosaic.rowCount - 1)) /
                                         multiMosaic.rowCount))
                                color: Theme.normalFill
                                border.color: Theme.normalBorder
                                border.width: 1
                                radius: Theme.radius
                                clip: true

                                Image {
                                    id: multiThumb
                                    anchors.fill: parent
                                    anchors.margins: Theme.spaceSM
                                    anchors.bottomMargin: multiName.height +
                                                          Theme.spaceSM
                                    source: multiTile.modelData &&
                                            multiTile.modelData.thumbnail
                                            ? multiTile.modelData.thumbnail : ""
                                    asynchronous: true
                                    cache: true
                                    fillMode: Image.PreserveAspectFit
                                    visible: source.toString().length > 0 &&
                                             status !== Image.Error
                                }

                                FolderMark {
                                    width: Math.min(parent.width,
                                                    parent.height) * 0.45
                                    height: width * 0.72
                                    anchors.centerIn: parent
                                    anchors.verticalCenterOffset:
                                        -multiName.height / 3
                                    visible: !multiThumb.visible &&
                                             !!multiTile.modelData.isDir
                                    opacity: 0.66
                                }

                                FileMark {
                                    width: Math.min(parent.width,
                                                    parent.height) * 0.34
                                    height: width * 1.25
                                    anchors.centerIn: parent
                                    anchors.verticalCenterOffset:
                                        -multiName.height / 3
                                    suffix: multiTile.tileSuffix
                                    visible: !multiThumb.visible &&
                                             !multiTile.modelData.isDir
                                    opacity: 0.66
                                }

                                Text {
                                    id: multiName
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.spaceSM
                                    text: multiTile.tileName
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCaption
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }
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
                              ? "folder · Shift+Space to inspect"
                              : ((root.stat && root.stat.size >= 0
                                  ? root.humanSize(root.stat.size) + " · " : "") +
                                 "Shift+Space to inspect")
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                    }
                }
            }
        }
    }
}
