import QtQuick
import Synchro.Theme

FocusScope {
    id: root

    property var config: null
    property int foregroundWorkers: 2
    property bool imageFacts: true
    property bool catalogEnabled: true
    property bool recursiveScan: true
    property int scanMinutes: 360
    property int watchDebounce: 650
    property int watchNeighborhood: 128
    property int maxWatches: 2048
    property bool imageEmbeddings: false
    property int semanticBatch: 16
    property int semanticInterval: 60
    signal dismissed()

    objectName: "indexerSettingsOverlay"
    visible: false
    focus: visible

    function open() {
        if (!config)
            return
        var values = config.indexerSettings()
        foregroundWorkers = values.foregroundThumbnailWorkers
        imageFacts = values.foregroundImageFacts
        catalogEnabled = values.backgroundCatalogEnabled
        recursiveScan = values.backgroundRecursiveScan
        scanMinutes = values.backgroundScanIntervalMinutes
        watchDebounce = values.backgroundWatchDebounceMs
        watchNeighborhood = values.backgroundWatchNeighborhood
        maxWatches = values.backgroundMaxWatches
        imageEmbeddings = values.semanticImageEmbeddings
        semanticBatch = values.semanticBatchSize
        semanticInterval = values.semanticIntervalSeconds
        visible = true
        config.refreshIndexerStatus()
        forceActiveFocus()
    }

    function closeOverlay() {
        visible = false
        dismissed()
    }

    function apply() {
        if (!config)
            return
        var saved = config.applyIndexerSettings({
            foregroundThumbnailWorkers: foregroundWorkers,
            foregroundImageFacts: imageFacts,
            backgroundCatalogEnabled: catalogEnabled,
            backgroundRecursiveScan: recursiveScan,
            backgroundScanIntervalMinutes: scanMinutes,
            backgroundWatchDebounceMs: watchDebounce,
            backgroundWatchNeighborhood: watchNeighborhood,
            backgroundMaxWatches: maxWatches,
            semanticImageEmbeddings: imageEmbeddings,
            semanticBatchSize: semanticBatch,
            semanticIntervalSeconds: semanticInterval
        })
        if (saved)
            closeOverlay()
    }

    function daemonStatus() {
        if (!config)
            return "status unavailable"
        if (config.indexerStatusLoading)
            return "checking the index service…"
        var state = config.currentIndexerStatus
        if (!state || !state.ok)
            return state && state.error ? state.error : "index service unavailable"
        var words = [state.running ? "index service live" : "index service stopped"]
        if (state.rowCount !== undefined)
            words.push(Number(state.rowCount).toLocaleString(Qt.locale(), "f", 0) + " files")
        if (state.semantic && state.semantic.embeddedImages)
            words.push(Number(state.semantic.embeddedImages).toLocaleString(Qt.locale(), "f", 0) + " image vectors")
        if (state.semantic && state.semantic.textModelAvailable)
            words.push("visual search ready")
        return words.join("  ·  ")
    }

    function statusState() {
        return config && config.currentIndexerStatus
               ? config.currentIndexerStatus : ({})
    }

    function enrichmentState() {
        var state = statusState()
        return state.enrichment || ({})
    }

    function semanticState() {
        var state = statusState()
        return state.semantic || ({})
    }

    function countText(value) {
        var number = Number(value || 0)
        return number.toLocaleString(Qt.locale(), "f", 0)
    }

    function coverageText(current, total) {
        var denominator = Number(total || 0)
        if (denominator <= 0)
            return "—"
        return (100 * Number(current || 0) / denominator).toLocaleString(
                    Qt.locale(), "f", 1) + "%"
    }

    function ageText(epochMs) {
        var age = Math.max(0, Date.now() - Number(epochMs || 0))
        if (!epochMs)
            return "never"
        if (age < 60000)
            return Math.max(1, Math.round(age / 1000)) + "s ago"
        if (age < 3600000)
            return Math.round(age / 60000) + "m ago"
        if (age < 86400000)
            return Math.round(age / 3600000) + "h ago"
        return Math.round(age / 86400000) + "d ago"
    }

    function durationText(durationMs) {
        var ms = Number(durationMs || 0)
        return ms >= 1000 ? (ms / 1000).toLocaleString(Qt.locale(), "f", 1) + "s"
                          : Math.round(ms) + "ms"
    }

    function semanticPassText() {
        var semantic = semanticState()
        if (!semantic.available)
            return "Semantic store has not been created yet."
        var parts = ["last pass " + ageText(semantic.last_run_at),
                     durationText(semantic.last_duration_ms),
                     "+" + countText(semantic.last_embedded) + " vectors",
                     countText(semantic.last_failed) + " failed"]
        if (semantic.catalogSequenceLag !== undefined)
            parts.push(countText(semantic.catalogSequenceLag) + " catalog events behind")
        if (semantic.image_sweeps !== undefined)
            parts.push("sweep " + countText(semantic.image_sweeps))
        return parts.join("  ·  ")
    }

    function semanticRuntimeText() {
        var semantic = semanticState()
        if (!semantic.workerAvailable)
            return "semantic worker unavailable"
        var dependencies = semantic.dependencies || ({})
        var ready = 0
        var total = 0
        for (var name in dependencies) {
            ++total
            if (dependencies[name])
                ++ready
        }
        return "runtime " + ready + "/" + total +
               "  ·  vision " + (semantic.modelAvailable ? "ready" : "missing") +
               "  ·  text " + (semantic.textModelAvailable && semantic.tokenizerAvailable
                                 ? "ready" : "missing")
    }

    Keys.onEscapePressed: function(event) {
        closeOverlay()
        event.accepted = true
    }

    Timer {
        interval: 10000
        repeat: true
        running: root.visible
        onTriggered: if (root.config) root.config.refreshIndexerStatus()
    }

    component SettingSection: Rectangle {
        property string title: ""
        property string caption: ""
        default property alias contents: body.data
        implicitHeight: sectionHeader.implicitHeight + body.implicitHeight + Theme.space(34)
        color: Theme.alpha(Theme.darkerBackground, 0.5)
        border.color: Theme.alpha(Theme.normalBorder, 0.72)
        border.width: 1
        radius: Theme.radius

        Text {
            id: sectionHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space(14)
            text: parent.title
            color: Theme.lightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
        }

        Text {
            anchors.left: sectionHeader.left
            anchors.right: sectionHeader.right
            anchors.top: sectionHeader.bottom
            anchors.topMargin: Theme.space(3)
            text: parent.caption
            color: Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }

        Column {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: sectionHeader.bottom
            anchors.leftMargin: Theme.space(14)
            anchors.rightMargin: Theme.space(14)
            anchors.topMargin: Theme.space(32)
            spacing: Theme.space(7)
        }
    }

    component SettingRow: Item {
        property string label: ""
        property string detail: ""
        default property alias control: controlSlot.data
        implicitHeight: Math.max(Theme.space(48), copy.implicitHeight)

        Column {
            id: copy
            anchors.left: parent.left
            anchors.right: controlSlot.left
            anchors.rightMargin: Theme.space(18)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(2)
            Text {
                text: parent.parent.label
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: parent.parent.detail
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
        }
        Item {
            id: controlSlot
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width * 0.48, Theme.space(330))
            height: Theme.controlHeight
        }
    }

    component Switch: Rectangle {
        property bool checked: false
        signal toggled(bool checked)
        width: Theme.space(48)
        height: Theme.space(25)
        radius: height / 2
        color: checked ? Theme.alpha(Theme.accent, 0.28) : Theme.normalFill
        border.color: checked ? Theme.accent : Theme.normalBorder
        border.width: 1
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            width: Theme.space(17)
            height: width
            radius: width / 2
            y: (parent.height - height) / 2
            x: parent.checked ? parent.width - width - Theme.space(4) : Theme.space(4)
            color: parent.checked ? Theme.accent : Theme.darkForeground
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
        TapHandler { onTapped: parent.toggled(!parent.checked) }
    }

    component MetricCard: Rectangle {
        property string label: ""
        property string value: "—"
        property string detail: ""
        height: Theme.space(76)
        color: Theme.alpha(Theme.lighterBackground, 0.38)
        border.color: Theme.alpha(Theme.normalBorder, 0.66)
        border.width: 1
        radius: Theme.radius

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space(9)
            text: parent.label.toUpperCase()
            color: Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space(9)
            anchors.rightMargin: Theme.space(9)
            text: parent.value
            color: Theme.brightForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space(9)
            text: parent.detail
            color: Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.background, 0.82)

        MouseArea {
            anchors.fill: parent
            onClicked: function(mouse) {
                var point = mapToItem(card, mouse.x, mouse.y)
                if (point.x < 0 || point.y < 0 ||
                        point.x > card.width || point.y > card.height)
                    root.closeOverlay()
            }
        }
    }

    Rectangle {
        id: card
        objectName: "indexerSettingsCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.space(34), Theme.space(760))
        height: Math.min(parent.height - Theme.space(34), Theme.space(680))
        color: Theme.opaqueBackground
        border.color: Theme.focusBorder
        border.width: 1
        radius: Theme.radius

        Text {
            id: eyebrow
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Theme.space(20)
            text: "INDEXING"
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            font.letterSpacing: 1.2
        }

        Text {
            id: title
            anchors.left: eyebrow.left
            anchors.top: eyebrow.bottom
            anchors.topMargin: Theme.space(4)
            text: "Tune the file substrate"
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.bold: true
        }

        Text {
            anchors.left: title.left
            anchors.right: closeButton.left
            anchors.top: title.bottom
            anchors.topMargin: Theme.space(7)
            text: root.daemonStatus()
            color: root.config && root.config.indexerStatusLoading
                   ? Theme.accent : Theme.darkForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }

        ChromeButton {
            id: closeButton
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space(14)
            fallbackGlyph: "×"
            toolTip: "Close settings"
            onTriggered: root.closeOverlay()
        }

        Flickable {
            id: scroll
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: title.bottom
            anchors.bottom: footer.top
            anchors.leftMargin: Theme.space(20)
            anchors.rightMargin: Theme.space(20)
            anchors.topMargin: Theme.space(36)
            anchors.bottomMargin: Theme.space(12)
            contentWidth: width
            contentHeight: settingsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: settingsColumn
                width: scroll.width
                spacing: Theme.space(10)

                SettingSection {
                    width: parent.width
                    title: "While browsing"
                    caption: "Immediate work on the visible page. Changes apply to this window without a restart."
                    SettingRow {
                        width: parent.width
                        label: "Thumbnail workers"
                        detail: "More parallel decoding; two is the balanced default."
                        SegmentedControl {
                            objectName: "thumbnailWorkerControl"
                            anchors.fill: parent
                            currentId: String(root.foregroundWorkers)
                            outlineSelection: true
                            showLabels: true
                            options: [
                                { id: "1", label: "1" }, { id: "2", label: "2" },
                                { id: "3", label: "3" }, { id: "4", label: "4" }
                            ]
                            onActivated: function(id) { root.foregroundWorkers = Number(id) }
                        }
                    }
                    SettingRow {
                        width: parent.width
                        label: "Capture visual facts"
                        detail: "Store palette, dimensions and aspect data while pixels are already decoded."
                        Switch { checked: root.imageFacts; onToggled: value => root.imageFacts = value }
                    }
                }

                SettingSection {
                    width: parent.width
                    title: "Background catalog"
                    caption: "The user service watches this config file and retunes itself—no sudo or manual restart."
                    SettingRow {
                        width: parent.width
                        label: "Live catalog maintenance"
                        detail: "Keep active folders current with a bounded inotify hot set."
                        Switch { checked: root.catalogEnabled; onToggled: value => root.catalogEnabled = value }
                    }
                    SettingRow {
                        width: parent.width
                        label: "Recursive reconciliation"
                        detail: "Occasionally verify the complete indexed tree."
                        opacity: root.catalogEnabled ? 1 : 0.42
                        Switch {
                            enabled: root.catalogEnabled
                            checked: root.recursiveScan
                            onToggled: value => root.recursiveScan = value
                        }
                    }
                    SettingRow {
                        width: parent.width
                        label: "Full-scan cadence"
                        detail: "How long to wait after a complete reconciliation."
                        opacity: root.catalogEnabled && root.recursiveScan ? 1 : 0.42
                        SegmentedControl {
                            anchors.fill: parent
                            enabled: root.catalogEnabled && root.recursiveScan
                            currentId: String(root.scanMinutes)
                            showLabels: true
                            options: [
                                { id: "60", label: "1h" }, { id: "360", label: "6h" },
                                { id: "1440", label: "24h" }, { id: "10080", label: "7d" }
                            ]
                            onActivated: function(id) { root.scanMinutes = Number(id) }
                        }
                    }
                    SettingRow {
                        width: parent.width
                        label: "Hot-set size"
                        detail: "How many recently useful folders remain under direct watch."
                        SegmentedControl {
                            anchors.fill: parent
                            enabled: root.catalogEnabled
                            currentId: String(root.maxWatches)
                            showLabels: true
                            options: [
                                { id: "512", label: "light" }, { id: "2048", label: "balanced" },
                                { id: "8192", label: "wide" }
                            ]
                            onActivated: function(id) { root.maxWatches = Number(id) }
                        }
                    }
                }

                SettingSection {
                    width: parent.width
                    title: "Semantic images · experimental"
                    caption: "Build private 512-dimensional CLIP vectors in semantic.sqlite, then search them with :see <visual description>."
                    SettingRow {
                        width: parent.width
                        label: "Image embeddings"
                        detail: "Downloads a verified 336 MiB vision model. The first :see query adds the matching 242 MiB text encoder."
                        Switch { checked: root.imageEmbeddings; onToggled: value => root.imageEmbeddings = value }
                    }
                    SettingRow {
                        width: parent.width
                        label: "Batch pressure"
                        detail: "Images per background pass."
                        opacity: root.imageEmbeddings ? 1 : 0.42
                        SegmentedControl {
                            anchors.fill: parent
                            enabled: root.imageEmbeddings
                            currentId: String(root.semanticBatch)
                            showLabels: true
                            options: [
                                { id: "4", label: "4" }, { id: "8", label: "8" },
                                { id: "16", label: "16" }, { id: "32", label: "32" }
                            ]
                            onActivated: function(id) { root.semanticBatch = Number(id) }
                        }
                    }
                    SettingRow {
                        width: parent.width
                        label: "Cadence"
                        detail: "Pause between incremental passes."
                        opacity: root.imageEmbeddings ? 1 : 0.42
                        SegmentedControl {
                            anchors.fill: parent
                            enabled: root.imageEmbeddings
                            currentId: String(root.semanticInterval)
                            showLabels: true
                            options: [
                                { id: "30", label: "30s" }, { id: "60", label: "1m" },
                                { id: "300", label: "5m" }
                            ]
                            onActivated: function(id) { root.semanticInterval = Number(id) }
                        }
                    }
                }

                SettingSection {
                    objectName: "enrichmentMonitor"
                    width: parent.width
                    title: "Enrichment monitor"
                    caption: "Live catalog coverage for deterministic visual facts and local semantic vectors. Refreshes every 10 seconds while open."

                    Row {
                        width: parent.width
                        spacing: Theme.space(7)
                        MetricCard {
                            width: (parent.width - parent.spacing * 2) / 3
                            label: "Raster candidates"
                            value: root.countText(root.enrichmentState().eligibleImages)
                            detail: "indexed image files"
                        }
                        MetricCard {
                            width: (parent.width - parent.spacing * 2) / 3
                            label: "Visual facts"
                            value: root.countText(
                                       root.enrichmentState().visualFactsStoredFiles)
                            detail: root.coverageText(
                                        root.enrichmentState().visualFactsStoredFiles,
                                        root.enrichmentState().eligibleImages) +
                                    " · captured foreground facts"
                        }
                        MetricCard {
                            width: (parent.width - parent.spacing * 2) / 3
                            label: "CLIP vectors"
                            value: root.countText(
                                       root.semanticState().currentEmbeddings !== undefined
                                       ? root.semanticState().currentEmbeddings
                                       : root.semanticState().embeddedImages)
                            detail: root.coverageText(
                                        root.semanticState().currentEmbeddings,
                                        root.semanticState().eligibleImages) +
                                    " · " + root.countText(
                                        root.semanticState().pendingImages) + " pending"
                        }
                    }

                    Item {
                        width: parent.width
                        height: Theme.space(17)
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            height: Theme.space(4)
                            radius: height / 2
                            color: Theme.normalFill
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1,
                                    Number(root.semanticState().coveragePercent || 0) / 100))
                                height: parent.height
                                radius: height / 2
                                color: Theme.alpha(Theme.accent, 0.76)
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: root.semanticPassText()
                        color: Theme.foreground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        text: root.semanticRuntimeText()
                        color: Theme.darkForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        visible: root.semanticState().staleEmbeddings > 0
                        text: root.countText(root.semanticState().staleEmbeddings) +
                              " stale vectors will be replaced or pruned during reconciliation."
                        color: Theme.darkForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                    Text {
                        width: parent.width
                        visible: String(root.semanticState().last_error || "").length > 0
                        text: "LAST ERROR  ·  " + root.semanticState().last_error
                        color: Theme.urgent
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        wrapMode: Text.Wrap
                    }
                    Item {
                        width: parent.width
                        height: Theme.controlHeight
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.config && root.config.indexerStatusLoading
                                  ? "refreshing diagnostics…"
                                  : "cursor " + root.countText(root.semanticState().image_cursor) +
                                    " / rowid " + root.countText(
                                        root.semanticState().catalogMaxImageRowid)
                            color: Theme.darkForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontCaption
                        }
                        ChromeButton {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            label: "Refresh"
                            enabled: root.config && !root.config.indexerStatusLoading
                            onTriggered: root.config.refreshIndexerStatus()
                        }
                    }
                }
            }
        }

        Item {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space(20)
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Ctrl+,"
                color: Theme.darkForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontCaption
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.controlGap
                ChromeButton { label: "Cancel"; onTriggered: root.closeOverlay() }
                ChromeButton { label: "Apply"; checked: true; onTriggered: root.apply() }
            }
        }
    }
}
