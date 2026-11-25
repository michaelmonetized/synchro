import QtQuick
import Synchro.Theme 1.0

// A live relation over the browser, not a detached database IDE. `here`,
// `tree`, and `selection` are rebound for every query.
Item {
    id: panel

    property var host: null
    property var fileModel: null
    property var selectionModel: null
    property var navStack: null
    property var shell: null
    property var catalog: typeof fileCatalog !== "undefined" ? fileCatalog : null
    property var config: null

    property string sqlText: "select name, extension, size, mtime, path\n" +
                             "from here\n" +
                             "order by is_dir desc, name"
    property var result: ({})
    property bool running: false
    property double requestId: 0
    property int selectedResult: -1
    property string contextCwd: ""
    property string pendingLabel: ""
    property string resultLabel: "here"
    property var queryHistory: []
    property real editorHeight: Theme.space(132)
    property bool editorCollapsed: false
    property bool namingBookmark: false
    property string bookmarkName: ""
    property string bookmarkNotice: ""
    property var pendingBookmark: null
    property string lastRunSql: ""
    property string lastRunCwd: ""
    property string lastRunLabel: ""
    property string lastRunRelation: ""
    property string refreshSelectionPath: ""
    property bool catalogRefreshPending: false
    property string activeLens: ""
    property bool awaitingImageFacts: false
    property bool analyzeAfterTree: false

    readonly property var queryLenses: [
        {
            id: "kind",
            label: "kind",
            tip: "Browse this tree as images, code, data, archives, and other cheap extension-derived kinds",
            sql: "select kind, count(*) as files, round(sum(size) / 1073741824.0, 2) as gb\n" +
                 "from tree\nwhere not is_dir\ngroup by kind\norder by sum(size) desc"
        },
        {
            id: "size",
            label: "size",
            tip: "Turn file-size bands into drillable folders",
            sql: "select size_bucket, count(*) as files, round(sum(size) / 1073741824.0, 2) as gb\n" +
                 "from tree\nwhere not is_dir\ngroup by size_bucket\norder by max(size) desc"
        },
        {
            id: "age",
            label: "age",
            tip: "Browse today, this week, this month, this year, and older files",
            sql: "select age_bucket, count(*) as files, round(sum(size) / 1073741824.0, 2) as gb\n" +
                 "from tree\nwhere not is_dir\ngroup by age_bucket\norder by min(age_days)"
        },
        {
            id: "largest",
            label: "largest",
            tip: "Show the 200 largest files below this folder",
            sql: "select name, kind, gb, age_days, modified_date, path, is_dir\n" +
                 "from tree\nwhere not is_dir\norder by size desc\nlimit 200"
        },
        {
            id: "projects",
            label: "projects",
            tip: "Find real project folders and classify them from repository and build markers",
            sql: "select name, project_type, markers, mtime, path, is_dir\n" +
                 "from projects\norder by mtime desc, name"
        },
        {
            id: "blue",
            label: "blue",
            analyzer: "image",
            tip: "Find blue-heavy images from deterministic thumbnail pixels; coverage is shown at right",
            sql: "select name, color_family, round(blue_share * 100, 1) as blue_pct,\n" +
                 "       width, height, path, is_dir\nfrom image_facts\n" +
                 "where blue_share >= 0.18\norder by blue_share desc, name"
        },
        {
            id: "wide",
            label: "wide",
            analyzer: "image",
            tip: "Find panoramic and unusually wide images using decoded dimensions",
            sql: "select name, round(aspect_ratio, 2) as aspect, width, height,\n" +
                 "       color_family, path, is_dir\nfrom image_facts\n" +
                 "where aspect_ratio >= 1.8\norder by aspect_ratio desc, name"
        }
    ]

    readonly property var columns: result && result.columns ? result.columns : []
    readonly property var rows: result && result.rows ? result.rows : []
    readonly property string editorRelation: catalog && catalog.sourceRelation
                                             ? catalog.sourceRelation(sqlText) : ""
    readonly property var fieldNames: {
        if (editorRelation === "image_facts")
            return ["path", "name", "extension", "size", "width", "height",
                    "aspect_ratio", "orientation", "color_family",
                    "dominant_color", "average_color", "palette_0",
                    "palette_1", "palette_2", "palette_weight_0",
                    "palette_weight_1", "palette_weight_2",
                    "brightness", "saturation",
                    "blue_share", "chromatic_share", "visual_hash", "mtime",
                    "is_dir", "hidden", "kind", "file_id"]
        if (editorRelation === "projects")
            return ["path", "name", "project_type", "markers", "mtime",
                    "is_dir", "size"]
        if (editorRelation === "facts")
            return ["path", "name", "analyzer", "analyzer_version", "key",
                    "text_value", "numeric_value", "updated_at", "file_id",
                    "size", "mtime"]
        return catalog && catalog.fields ? catalog.fields : []
    }
    readonly property int rowH: Math.max(26, Theme.fontBody + Theme.space(10))
    readonly property int headerH: Math.max(26, Theme.fontBody + Theme.space(10))
    readonly property real minEditorHeight: Theme.space(58)
    readonly property int treeScanLimit: 0
    readonly property real maxEditorHeight: Math.max(minEditorHeight,
                                                      height * 0.64)
    readonly property string cwd: fileModel && fileModel.isSql
                                  ? contextCwd
                                  : (fileModel && fileModel.path ? fileModel.path : "")
    readonly property string scopeName: cwd.length ? cwd.split("/").pop() || "/" : "—"

    function publishRunning() {
        if (shell && shell.mainSqlBusy !== undefined)
            shell.mainSqlBusy = running
    }

    function focusContent() {
        queryEdit.forceActiveFocus()
    }

    function execute(label, overrideCwd, forceTree, queryOverride, liveRefresh) {
        var queryText = queryOverride === undefined || queryOverride === null
                      ? sqlText : queryOverride
        if (!catalog || running || !queryText.trim().length)
            return
        var runCwd = overrideCwd || cwd
        var relation = catalog.sourceRelation
                     ? catalog.sourceRelation(queryText) : ""
        var needsTree = relationNeedsTree(relation)
        if (needsTree && liveRefresh !== true) {
            var covered = catalog.coversTree
                        ? catalog.coversTree(runCwd)
                        : catalog.indexedRoot === runCwd
            if (forceTree || !covered)
                catalog.scanTree(runCwd, treeScanLimit)
        }
        lastRunSql = queryText
        lastRunCwd = runCwd
        lastRunLabel = label || ""
        lastRunRelation = relation
        pendingLabel = label || ""
        running = true
        if (liveRefresh === true && fileModel && fileModel.currentStat)
            refreshSelectionPath = fileModel.currentStat.path || ""
        else {
            refreshSelectionPath = ""
            selectedResult = -1
        }
        var paths = selectionModel && selectionModel.selectedPaths
                  ? selectionModel.selectedPaths() : []
        requestId = catalog.query(queryText, runCwd, paths, 200)
    }

    function scheduleCatalogRefresh() {
        if (!catalog || !relationNeedsTree(lastRunRelation) ||
                catalog.indexedRoot !== lastRunCwd ||
                !fileModel || !fileModel.isSql)
            return
        // Progress signals are frequent during a refresh. Re-run the active
        // tree query once, after reconciliation completes, rather than showing
        // a different partial result every few seconds.
        if (catalog.indexing)
            return
        if (running) {
            catalogRefreshPending = true
            return
        }
        if (!catalogRefreshTimer.running)
            catalogRefreshTimer.start()
    }

    function relationNeedsTree(name) {
        return name === "tree" || name === "facts" ||
               name === "image_facts" || name === "projects"
    }

    function forceTreeScan() {
        if (!catalog || !cwd.length)
            return
        if (catalog.sourceRelation && catalog.sourceRelation(sqlText) === "tree")
            execute(resultLabel, cwd, true)
        else
            catalog.scanTree(cwd, treeScanLimit)
    }

    function openBookmark(name, sql, savedCwd) {
        if (!sql || !sql.trim().length)
            return
        if (running) {
            pendingBookmark = { name: name, sql: sql, cwd: savedCwd }
            return
        }
        queryHistory = []
        activeLens = ""
        contextCwd = savedCwd || cwd
        sqlText = sql
        namingBookmark = false
        execute(name || "saved query", contextCwd)
    }

    function beginBookmark() {
        if (!config || !sqlText.trim().length)
            return
        bookmarkName = (resultLabel !== "here" && resultLabel !== "tree" &&
                        resultLabel !== "selection") ? resultLabel : ""
        namingBookmark = true
        Qt.callLater(function () {
            bookmarkEdit.forceActiveFocus()
            bookmarkEdit.selectAll()
        })
    }

    function commitBookmark() {
        if (!config || !bookmarkName.trim().length)
            return
        var id = config.saveSqlBookmark(bookmarkName, sqlText, cwd)
        if (!id || !id.length) {
            bookmarkNotice = "could not save query"
            noticeTimer.restart()
            return
        }
        bookmarkNotice = "saved · " + bookmarkName.trim()
        namingBookmark = false
        noticeTimer.restart()
    }

    function useRelation(name) {
        activeLens = ""
        if (name === "here")
            sqlText = "select name, extension, size, mtime, path\nfrom here\norder by is_dir desc, name"
        else if (name === "tree")
            sqlText = "select extension, count(*) as files, sum(size) as bytes\nfrom tree\ngroup by extension\norder by bytes desc"
        else
            sqlText = "select * from selection order by name"
        queryEdit.forceActiveFocus()
    }

    function useLens(lens) {
        if (!lens || running)
            return
        queryHistory = []
        activeLens = lens.id
        sqlText = lens.sql
        if (lens.analyzer === "image" && catalog && catalog.analyzeImages) {
            awaitingImageFacts = true
            var covered = catalog.coversTree ? catalog.coversTree(cwd) : false
            analyzeAfterTree = !covered
            if (covered)
                catalog.analyzeImages(cwd, 500)
        } else {
            awaitingImageFacts = false
            analyzeAfterTree = false
        }
        execute(lens.label)
    }

    function activeLensSpec() {
        for (var i = 0; i < queryLenses.length; ++i) {
            if (queryLenses[i].id === activeLens)
                return queryLenses[i]
        }
        return null
    }

    function refreshFinishedAnalysis() {
        if (!catalog || catalog.analyzing || running || !awaitingImageFacts)
            return
        var lens = activeLensSpec()
        awaitingImageFacts = false
        if (lens && lens.analyzer === "image")
            execute(lens.label, lastRunCwd, false, lastRunSql, true)
    }

    readonly property var factCoverage: result && result.factCoverage
                                        ? result.factCoverage : ({})
    readonly property string factTrace: {
        var lens = activeLensSpec()
        if ((!lens || lens.analyzer !== "image") &&
                !(catalog && catalog.analyzing))
            return ""
        if (catalog && catalog.analyzing)
            return catalog.analysisStatus || "image facts…"
        var seen = Number(factCoverage.analyzed || 0)
        var total = Number(factCoverage.total || 0)
        if (total <= 0)
            return catalog && catalog.analysisStatus
                   ? catalog.analysisStatus : "no image facts"
        return "facts " + seen.toLocaleString(Qt.locale(), "f", 0) +
               " / " + total.toLocaleString(Qt.locale(), "f", 0)
    }

    function installHighlighter() {
        if (!host || !queryEdit.textDocument)
            return
        host.attachSqlHighlighter(
                    queryEdit.textDocument,
                    Theme.themedColor("colors.magenta", Theme.accent),
                    Theme.themedColor("colors.green", Theme.lightForeground),
                    Theme.themedColor("colors.orange", Theme.urgent),
                    Theme.muted,
                    Theme.foreground)
    }

    function runDrill(row) {
        if (!row || !row._synchro_drill_sql)
            return
        queryHistory = queryHistory.concat([{ sql: sqlText,
                                               label: resultLabel,
                                               lens: activeLens }])
        activeLens = ""
        sqlText = row._synchro_drill_sql
        execute(row._synchro_label || "group")
    }

    function goQueryBack() {
        if (!queryHistory.length || running)
            return
        var prior = queryHistory[queryHistory.length - 1]
        queryHistory = queryHistory.slice(0, queryHistory.length - 1)
        sqlText = prior.sql
        activeLens = prior.lens || ""
        execute(prior.label)
    }

    function revealSelectedResult() {
        if (!resultFlick || selectedResult < 0)
            return
        var top = headerH + selectedResult * rowH
        var bottom = top + rowH
        if (top < resultFlick.contentY)
            resultFlick.contentY = top
        else if (bottom > resultFlick.contentY + resultFlick.height)
            resultFlick.contentY = Math.max(0, bottom - resultFlick.height)
    }

    function selectResult(index) {
        if (index < 0 || index >= rows.length)
            return
        selectedResult = index
        if (fileModel && fileModel.isSql)
            fileModel.selectSqlRow(index)
        Qt.callLater(revealSelectedResult)
    }

    function cellText(row, name) {
        if (!row || row[name] === undefined || row[name] === null)
            return "·"
        if (name === "kb" || name === "mb" || name === "gb") {
            var value = Number(row[name])
            if (isFinite(value)) {
                var precision = value >= 100 ? 0 : (value >= 10 ? 1 : 2)
                return value.toFixed(precision) + " " + name.toUpperCase()
            }
        }
        if (name === "size" || name === "bytes") {
            var n = Number(row[name])
            if (isFinite(n)) {
                if (n >= 1073741824) return (n / 1073741824).toFixed(1) + " GiB"
                if (n >= 1048576) return (n / 1048576).toFixed(1) + " MiB"
                if (n >= 1024) return (n / 1024).toFixed(1) + " KiB"
            }
        }
        if (name === "mtime") {
            var d = new Date(Number(row[name]))
            if (!isNaN(d.getTime()))
                return d.toLocaleString(Qt.locale(), "yyyy-MM-dd HH:mm")
        }
        return "" + row[name]
    }

    function columnWidth(name) {
        var n = (name || "").toLowerCase()
        if (n === "path" || n === "parent" || n === "snippet")
            return Theme.space(310)
        if (n === "name")
            return Theme.space(190)
        if (n === "mtime" || n === "modified")
            return Theme.space(150)
        return Math.max(Theme.space(92),
                        Math.min(Theme.space(170),
                                 Math.round(n.length * Theme.fontBody * 0.7) + Theme.space(28)))
    }

    function tableWidth() {
        var width = 0
        for (var i = 0; i < columns.length; ++i)
            width += columnWidth(columns[i].name)
        return width
    }

    function activateRow(row, open, index) {
        if (!row)
            return
        selectedResult = index
        if (!open && fileModel && fileModel.isSql) {
            fileModel.selectSqlRow(index)
            return
        }
        if (row._synchro_drill_sql) {
            if (open)
                runDrill(row)
            return
        }
        if (!host || !row.path)
            return
        var url = "file://" + row.path
        if (!open)
            host.reveal(url)
        else if (row.is_dir === true || Number(row.is_dir) === 1)
            host.navigate(url)
        else
            host.openExternal(url)
    }

    Connections {
        target: panel.catalog
        function onStatusChanged() {
            if (panel.analyzeAfterTree && !panel.catalog.indexing &&
                    panel.catalog.coversTree(panel.lastRunCwd)) {
                panel.analyzeAfterTree = false
                panel.catalog.analyzeImages(panel.lastRunCwd, 500)
            }
            panel.scheduleCatalogRefresh()
        }
        function onAnalysisChanged() {
            panel.refreshFinishedAnalysis()
        }
        function onQueryFinished(id, value) {
            if (Number(id) !== Number(panel.requestId))
                return
            panel.running = false
            panel.result = value
            if (value && value.ok) {
                if (value.cwd)
                    panel.contextCwd = value.cwd
                panel.resultLabel = panel.pendingLabel.length
                                  ? panel.pendingLabel
                                  : (value.sourceRelation || "result")
                if (panel.fileModel) {
                    panel.fileModel.showSqlResult(value, panel.resultLabel)
                    if (panel.refreshSelectionPath.length)
                        panel.fileModel.selectPath(panel.refreshSelectionPath)
                }
            }
            panel.refreshSelectionPath = ""
            panel.pendingLabel = ""
            if (panel.catalogRefreshPending) {
                panel.catalogRefreshPending = false
                if (!catalogRefreshTimer.running)
                    catalogRefreshTimer.start()
            }
            if (panel.pendingBookmark) {
                var bookmark = panel.pendingBookmark
                panel.pendingBookmark = null
                Qt.callLater(function () {
                    panel.openBookmark(bookmark.name, bookmark.sql, bookmark.cwd)
                })
            }
            Qt.callLater(panel.refreshFinishedAnalysis)
        }
    }

    Timer {
        id: catalogRefreshTimer
        interval: 750
        repeat: false
        onTriggered: {
            if (panel.running) {
                panel.catalogRefreshPending = true
                return
            }
            if (!panel.catalog || !panel.relationNeedsTree(panel.lastRunRelation) ||
                    panel.catalog.indexedRoot !== panel.lastRunCwd ||
                    !panel.fileModel || !panel.fileModel.isSql)
                return
            panel.execute(panel.lastRunLabel, panel.lastRunCwd, false,
                          panel.lastRunSql, true)
        }
    }

    Connections {
        target: panel.fileModel
        function onPathChanged() {
            if (panel.fileModel && !panel.fileModel.isSql)
                panel.contextCwd = panel.fileModel.path || ""
        }
        function onCurrentIndexChanged() {
            if (panel.fileModel && panel.fileModel.isSql) {
                panel.selectedResult = panel.fileModel.currentSqlRow
                Qt.callLater(panel.revealSelectedResult)
            }
        }
        function onSqlDrillRequested(sql, label) {
            panel.queryHistory = panel.queryHistory.concat([
                { sql: panel.sqlText, label: panel.resultLabel,
                  lens: panel.activeLens }
            ])
            panel.activeLens = ""
            panel.sqlText = sql
            panel.execute(label)
        }
        function onSqlBackRequested() { panel.goQueryBack() }
    }

    Connections {
        target: Theme.impl
        function onThemeChanged() { panel.installHighlighter() }
    }

    onHostChanged: Qt.callLater(installHighlighter)
    onFileModelChanged: {
        if (fileModel) {
            if (!fileModel.isSql)
                contextCwd = fileModel.path || ""
            fileModel.setSqlBackAvailable(queryHistory.length > 0)
        }
    }
    onQueryHistoryChanged: if (fileModel)
        fileModel.setSqlBackAvailable(queryHistory.length > 0)

    Timer {
        id: noticeTimer
        interval: 2400
        onTriggered: panel.bookmarkNotice = ""
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: panel.beginBookmark()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.opaqueBackground
    }

    Column {
        id: workbench
        anchors.fill: parent
        anchors.leftMargin: Theme.space(12)
        anchors.rightMargin: Theme.space(12)
        anchors.topMargin: Theme.space(10)
        anchors.bottomMargin: Theme.space(10)
        spacing: Theme.space(8)

        Item {
            width: parent.width
            height: Math.max(scopeLabel.implicitHeight, scopeMeta.implicitHeight) +
                    Theme.space(4)

            Text {
                id: scopeLabel
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.72
                text: "here  ›  " + panel.cwd
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.bold: true
                elide: Text.ElideMiddle
            }

            Text {
                id: scopeMeta
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.bookmarkNotice.length > 0
                text: panel.bookmarkNotice
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideLeft
                width: parent.width * 0.26
                horizontalAlignment: Text.AlignRight
            }
        }

        Row {
            width: parent.width
            height: Theme.controlHeight
            spacing: Theme.space(6)

            Rectangle {
                visible: panel.queryHistory.length > 0
                height: Theme.controlHeight
                width: visible ? backText.implicitWidth + Theme.space(18) : 0
                radius: Theme.radius
                color: backArea.containsMouse ? Theme.hoverFill : "transparent"
                border.color: Theme.normalBorder
                border.width: 1

                Text {
                    id: backText
                    anchors.centerIn: parent
                    text: "‹ query"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
                MouseArea {
                    id: backArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.goQueryBack()
                }
            }

            Repeater {
                model: ["here", "tree"]
                delegate: Rectangle {
                    id: relationChip
                    required property string modelData
                    height: Theme.controlHeight
                    width: relationText.implicitWidth + Theme.space(18)
                    radius: Theme.radius
                    color: relationArea.containsMouse ? Theme.hoverFill : "transparent"
                    border.color: relationArea.containsMouse
                                  ? Theme.accent : Theme.normalBorder
                    border.width: 1

                    Text {
                        id: relationText
                        anchors.centerIn: parent
                        text: relationChip.modelData
                        color: relationArea.containsMouse ? Theme.accent : Theme.foreground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    MouseArea {
                        id: relationArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.useRelation(relationChip.modelData)
                    }
                }
            }

            Rectangle {
                id: selectionChip
                visible: panel.selectionModel &&
                         panel.selectionModel.selectedCount > 1
                height: Theme.controlHeight
                width: visible ? selectionText.implicitWidth + Theme.space(18) : 0
                radius: Theme.radius
                color: selectionArea.containsMouse ? Theme.hoverFill : "transparent"
                border.color: selectionArea.containsMouse
                              ? Theme.accent : Theme.normalBorder
                border.width: 1

                Text {
                    id: selectionText
                    anchors.centerIn: parent
                    text: "selection · " + (panel.selectionModel
                          ? panel.selectionModel.selectedCount : 0)
                    color: selectionArea.containsMouse ? Theme.accent
                                                        : Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
                MouseArea {
                    id: selectionArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.useRelation("selection")
                }
                ToolTip {
                    anchorItem: selectionChip
                    shown: selectionArea.containsMouse
                    label: "Snapshot of the selected set · Ctrl/Shift-click, Ctrl+A, or V"
                }
            }

            Item {
                width: Math.max(0, parent.width - x -
                                (panel.namingBookmark ? bookmarkNameFrame.width
                                                      : saveButton.width) -
                                runButton.width - Theme.space(18))
                height: 1
            }

            Rectangle {
                id: saveButton
                visible: !panel.namingBookmark
                height: Theme.controlHeight
                width: visible ? saveText.implicitWidth + Theme.space(18) : 0
                radius: Theme.radius
                color: saveArea.containsMouse ? Theme.hoverFill : "transparent"
                border.color: Theme.normalBorder
                border.width: 1

                Text {
                    id: saveText
                    anchors.centerIn: parent
                    text: "☆ save"
                    color: saveArea.containsMouse ? Theme.accent : Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                MouseArea {
                    id: saveArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: panel.config && panel.sqlText.trim().length
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: panel.beginBookmark()
                }
                ToolTip {
                    anchorItem: saveButton
                    shown: saveArea.containsMouse
                    label: "Save this query as a browser location · Ctrl+Shift+S"
                }
            }

            Rectangle {
                id: bookmarkNameFrame
                visible: panel.namingBookmark
                height: Theme.controlHeight
                width: visible ? Theme.space(190) : 0
                radius: Theme.radius
                color: Theme.darkerBackground
                border.color: bookmarkEdit.activeFocus ? Theme.accent
                                                       : Theme.normalBorder
                border.width: 1

                TextInput {
                    id: bookmarkEdit
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space(9)
                    anchors.rightMargin: Theme.space(9)
                    verticalAlignment: TextInput.AlignVCenter
                    text: panel.bookmarkName
                    color: Theme.foreground
                    selectionColor: Theme.selectionFill
                    selectedTextColor: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    maximumLength: 80
                    clip: true
                    onTextChanged: panel.bookmarkName = text
                    Keys.onReturnPressed: panel.commitBookmark()
                    Keys.onEnterPressed: panel.commitBookmark()
                    Keys.onEscapePressed: {
                        panel.namingBookmark = false
                        queryEdit.forceActiveFocus()
                    }
                }
            }

            Rectangle {
                id: runButton
                height: Theme.controlHeight
                width: runText.implicitWidth + Theme.space(20)
                radius: Theme.radius
                color: panel.running ? Theme.selectedFill
                                     : (runArea.containsMouse ? Theme.hoverFill : Theme.normalFill)
                border.color: Theme.accent
                border.width: 1

                Text {
                    id: runText
                    anchors.centerIn: parent
                    text: panel.running ? "running…" : "run  Ctrl+Enter"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.bold: true
                }

                MouseArea {
                    id: runArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !panel.running
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.execute()
                }
            }
        }

        Item {
            id: lensRail
            width: parent.width
            height: Theme.space(24)

            Text {
                id: lensLabel
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "lens"
                color: Theme.darkForeground
                opacity: 0.64
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: 1.0
            }

            Flickable {
                anchors.left: lensLabel.right
                anchors.right: factStatus.visible ? factStatus.left : parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.space(8)
                contentWidth: lensRow.implicitWidth
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: lensRow
                    height: parent.height
                    spacing: Theme.space(4)

                    Repeater {
                        model: panel.queryLenses

                        ChromeButton {
                            required property var modelData
                            height: lensRow.height
                            label: modelData.label
                            checked: panel.activeLens === modelData.id
                            toolTip: modelData.tip
                            onTriggered: panel.useLens(modelData)
                        }
                    }
                }
            }

            Text {
                id: factStatus
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.space(8)
                visible: panel.factTrace.length > 0
                text: panel.factTrace
                color: panel.catalog && panel.catalog.analyzing
                       ? Theme.accent : Theme.darkForeground
                opacity: panel.catalog && panel.catalog.analyzing ? 0.82 : 0.58
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
            }
        }

        Rectangle {
            id: editorFrame
            width: parent.width
            height: panel.editorCollapsed
                    ? 0
                    : Math.max(panel.minEditorHeight,
                               Math.min(panel.maxEditorHeight,
                                        panel.editorHeight))
            visible: !panel.editorCollapsed
            color: Theme.darkerBackground
            radius: Theme.radius
            border.color: Theme.normalBorder
            border.width: 1

            TextEdit {
                id: queryEdit
                objectName: "sqlQueryEdit"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: fieldRail.visible ? fieldRail.left : parent.right
                anchors.leftMargin: Theme.space(10)
                anchors.topMargin: Theme.space(10)
                anchors.bottomMargin: Theme.space(10)
                anchors.rightMargin: Theme.space(10)
                text: panel.sqlText
                color: Theme.foreground
                selectionColor: Theme.selectionFill
                selectedTextColor: Theme.foreground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontTitle
                font.bold: true
                wrapMode: TextEdit.NoWrap
                clip: true
                selectByMouse: true
                persistentSelection: true
                onTextChanged: if (panel.sqlText !== text) {
                    panel.sqlText = text
                    panel.activeLens = ""
                }
                Component.onCompleted: Qt.callLater(panel.installHighlighter)

                Keys.onPressed: function(event) {
                    if ((event.modifiers & Qt.ControlModifier) &&
                            (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                        panel.execute()
                        event.accepted = true
                    }
                }
            }

            Item {
                id: fieldRail
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space(8)
                width: Math.min(Theme.space(310), editorFrame.width * 0.44)
                visible: panel.fieldNames.length > 0 &&
                         editorFrame.width >= Theme.space(560) &&
                         editorFrame.height >= Theme.space(78)
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: Theme.darkerBackground
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: Theme.normalBorder
                    opacity: 0.42
                }

                Text {
                    id: fieldLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: Theme.space(10)
                    text: "fields"
                    color: Theme.darkForeground
                    opacity: 0.58
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.letterSpacing: 1.1
                }

                Grid {
                    id: fieldGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: fieldLabel.bottom
                    anchors.leftMargin: Theme.space(10)
                    anchors.topMargin: Theme.space(3)
                    columns: fieldRail.width >= Theme.space(300) ? 4 : 3
                    columnSpacing: Theme.space(5)
                    rowSpacing: 0
                    readonly property real cellWidth:
                        (width - columnSpacing * (columns - 1)) / columns

                    Repeater {
                        model: panel.fieldNames

                        Text {
                            required property string modelData
                            width: fieldGrid.cellWidth
                            height: Theme.space(13)
                            text: modelData
                            color: Theme.darkForeground
                            opacity: 0.54
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        Item {
            id: editorDivider
            width: parent.width
            height: Theme.space(14)

            Rectangle {
                anchors.left: parent.left
                anchors.right: dividerToggle.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: Theme.space(8)
                height: 1
                color: resizeArea.containsMouse ? Theme.accent
                                                : Theme.normalBorder
            }

            Rectangle {
                anchors.centerIn: parent
                width: Theme.space(42)
                height: 3
                color: resizeArea.containsMouse ? Theme.accent
                                                : Theme.darkForeground
                opacity: 0.8
            }

            MouseArea {
                id: resizeArea
                anchors.left: parent.left
                anchors.right: dividerToggle.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                hoverEnabled: true
                cursorShape: Qt.SizeVerCursor
                property real startY: 0
                property real startHeight: 0

                onPressed: function(mouse) {
                    startY = mapToItem(null, mouse.x, mouse.y).y
                    startHeight = panel.editorCollapsed
                                  ? panel.minEditorHeight : editorFrame.height
                    panel.editorCollapsed = false
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    var y = mapToItem(null, mouse.x, mouse.y).y
                    panel.editorHeight = Math.max(
                                panel.minEditorHeight,
                                Math.min(panel.maxEditorHeight,
                                         startHeight + y - startY))
                }
                onDoubleClicked: panel.editorCollapsed =
                                     !panel.editorCollapsed
            }

            Text {
                id: dividerToggle
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: panel.editorCollapsed ? "show query  ▾" : "query  ▴"
                color: toggleArea.containsMouse ? Theme.accent
                                                : Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption

                MouseArea {
                    id: toggleArea
                    anchors.fill: parent
                    anchors.margins: -Theme.space(5)
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.editorCollapsed =
                                   !panel.editorCollapsed
                }
            }
        }

        Item {
            id: resultArea
            width: parent.width
            height: parent.height - y

            Text {
                visible: panel.running
                anchors.centerIn: parent
                anchors.verticalCenterOffset: Theme.space(36)
                text: "querying " + panel.scopeName + "…"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.bold: true
            }

            ThumbLoadingGlyph {
                objectName: "sqlResultSpinner"
                visible: panel.running
                running: panel.running
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -Theme.space(14)
                width: Theme.space(58)
                height: width
            }

            Text {
                visible: !panel.running && panel.result && panel.result.ok === false
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.space(30), Theme.space(620))
                text: panel.result.error || "query failed"
                color: Theme.urgent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                visible: !panel.running && panel.result && panel.result.ok === true &&
                         panel.rows.length === 0
                anchors.centerIn: parent
                text: "0 rows  ·  this relation is quiet"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            Flickable {
                id: resultFlick
                anchors.fill: parent
                visible: !panel.running && panel.result && panel.result.ok === true &&
                         panel.rows.length > 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, panel.tableWidth())
                contentHeight: resultTable.height
                activeFocusOnTab: true

                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                        var delta = event.key === Qt.Key_Up ? -1 : 1
                        var next = panel.selectedResult < 0 ? 0
                                  : Math.max(0, Math.min(panel.rows.length - 1,
                                                        panel.selectedResult + delta))
                        panel.selectResult(next)
                        event.accepted = true
                    } else if ((event.key === Qt.Key_Return ||
                                event.key === Qt.Key_Enter) &&
                               panel.selectedResult >= 0) {
                        panel.activateRow(panel.rows[panel.selectedResult], true,
                                          panel.selectedResult)
                        event.accepted = true
                    }
                }

                Column {
                    id: resultTable
                    width: Math.max(resultFlick.width, panel.tableWidth())

                    Row {
                        height: panel.headerH
                        Repeater {
                            model: panel.columns
                            delegate: Rectangle {
                                required property var modelData
                                width: panel.columnWidth(modelData.name)
                                height: panel.headerH
                                color: Theme.normalFill

                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space(9)
                                    anchors.rightMargin: Theme.space(9)
                                    text: modelData.name
                                    color: Theme.muted
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontCaption
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Theme.normalBorder
                                }
                            }
                        }
                    }

                    Repeater {
                        model: panel.rows
                        delegate: Item {
                            id: resultRow
                            required property var modelData
                            required property int index
                            readonly property bool selected:
                                index === panel.selectedResult
                            width: resultTable.width
                            height: panel.rowH

                            Rectangle {
                                anchors.fill: parent
                                color: resultRow.selected
                                       ? Theme.alpha(Theme.accent, 0.18)
                                       : (rowArea.containsMouse
                                          ? Theme.hoverFill
                                          : (resultRow.index % 2
                                             ? Theme.alpha(Theme.normalFill, 0.18)
                                             : "transparent"))
                                border.color: resultRow.selected
                                              ? Theme.accent : "transparent"
                                border.width: resultRow.selected ? 1 : 0
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: resultRow.selected ? 3 : 0
                                color: Theme.accent
                            }

                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: panel.columns
                                    delegate: Item {
                                        required property var modelData
                                        width: panel.columnWidth(modelData.name)
                                        height: panel.rowH

                                        Text {
                                            anchors.fill: parent
                                            anchors.leftMargin: Theme.space(9)
                                            anchors.rightMargin: Theme.space(9)
                                            text: panel.cellText(resultRow.modelData, modelData.name)
                                            color: resultRow.selected
                                                   ? Theme.brightForeground
                                                   : (modelData.name === "path"
                                                      ? Theme.accent
                                                      : Theme.foreground)
                                            font.family: Theme.monoFontFamily
                                            font.pixelSize: Theme.fontBody
                                            font.bold: resultRow.selected
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.alpha(Theme.normalBorder, 0.45)
                            }

                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    resultFlick.forceActiveFocus()
                                    panel.activateRow(resultRow.modelData, false,
                                                      resultRow.index)
                                }
                                onDoubleClicked: panel.activateRow(
                                                     resultRow.modelData, true,
                                                     resultRow.index)
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: resultFlick.visible
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: resultMeta.implicitWidth + Theme.space(14)
                height: resultMeta.implicitHeight + Theme.space(6)
                color: Theme.alpha(Theme.background, 0.92)

                Text {
                    id: resultMeta
                    anchors.centerIn: parent
                    text: panel.rows.length + (panel.result.truncated ? "+" : "") +
                          " rows  ·  " + panel.result.elapsedMs + " ms"
                    color: Theme.muted
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontCaption
                }
            }
        }
    }

    Component.onCompleted: {
        contextCwd = fileModel && fileModel.path ? fileModel.path : ""
        if (fileModel)
            fileModel.setSqlBackAvailable(queryHistory.length > 0)
        if (catalog)
            catalog.refreshCurrent()
        Qt.callLater(installHighlighter)
        publishRunning()
    }
    onRunningChanged: publishRunning()
    onShellChanged: publishRunning()
    Component.onDestruction: if (shell && shell.mainSqlBusy !== undefined)
                                 shell.mainSqlBusy = false
}
