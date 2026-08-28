import QtQuick
import Synchro.Theme 1.0

FocusScope {
    id: panel

    property var host: null
    property var fileModel: null
    property var selectionModel: null
    property var navStack: null
    property var bridge: typeof omaflow !== "undefined" ? omaflow : null
    property int selectedIndex: -1
    property string armedRule: ""
    property bool authoring: false
    property string authorPrompt: ""
    property bool stageArmed: false

    readonly property bool wide: width >= Theme.space(560)
    readonly property var rules: bridge && bridge.rules ? bridge.rules : []
    readonly property var activity: bridge && bridge.activity
                                    ? bridge.activity : []
    readonly property var staging: bridge && bridge.staging
                                   ? bridge.staging : ({})
    readonly property var selectedRule: selectedIndex >= 0 &&
                                        selectedIndex < rules.length
                                        ? rules[selectedIndex] : null
    readonly property var draftRule: staging && staging.status === "ready" &&
                                     staging.rule ? staging.rule : null
    readonly property bool reviewingDraft: draftRule !== null
    readonly property var displayRule: reviewingDraft ? draftRule : selectedRule
    readonly property bool stagedVisible: !!(staging && staging.status &&
                                             staging.status !== "idle")
    readonly property int stagedHeight: stagedVisible ? Theme.controlHeight : 0

    function focusContent() {
        if (authoring)
            authorInput.forceActiveFocus()
        else if (rules.length)
            ruleList.forceActiveFocus()
        else
            panel.forceActiveFocus()
    }

    function firstEnabledIndex() {
        for (var i = 0; i < rules.length; ++i) {
            if (rules[i].enabled !== false)
                return i
        }
        return rules.length ? 0 : -1
    }

    function titleCase(value) {
        var text = String(value || "").replace(/[-_]/g, " ")
        return text.length ? text.charAt(0).toUpperCase() + text.substring(1)
                           : "Step"
    }

    function valueText(value) {
        if (value === undefined || value === null || value === "")
            return ""
        if (Array.isArray(value) ||
                (typeof value === "object" &&
                 value.length !== undefined)) {
            var values = []
            for (var i = 0; i < value.length; ++i)
                values.push(valueText(value[i]))
            return values.join(", ")
        }
        if (typeof value === "object") {
            var parts = []
            for (var key in value) {
                var nested = valueText(value[key])
                if (nested.length)
                    parts.push(key.replace(/_/g, " ") + " " + nested)
            }
            return parts.join(" · ")
        }
        return String(value)
    }

    function describe(spec) {
        if (!spec)
            return ""
        var parts = []
        for (var key in spec) {
            if (key === "type")
                continue
            var text = valueText(spec[key])
            if (text.length)
                parts.push(key.replace(/_/g, " ") + " " + text)
        }
        return parts.length ? parts.join("  ·  ") : "no parameters"
    }

    function stepsFor(rule) {
        if (!rule)
            return []
        var out = []
        var trigger = rule.trigger || ({ type: "manual" })
        out.push({ kind: "trigger", label: titleCase(trigger.type),
                   detail: describe(trigger), spec: trigger })
        var conditions = rule.conditions || []
        for (var c = 0; c < conditions.length; ++c) {
            out.push({ kind: "condition",
                       label: titleCase(conditions[c].type),
                       detail: describe(conditions[c]),
                       spec: conditions[c] })
        }
        var actions = rule.actions || []
        for (var a = 0; a < actions.length; ++a) {
            out.push({ kind: "action", label: titleCase(actions[a].type),
                       detail: describe(actions[a]), spec: actions[a] })
        }
        return out
    }

    function graphFor(rule) {
        if (!rule)
            return ({ nodes: [], edges: [] })
        var visual = rule.visualGraph || null
        if (visual && visual.nodes && visual.edges)
            return visual

        var steps = stepsFor(rule)
        var nodes = []
        var edges = []
        for (var i = 0; i < steps.length; ++i) {
            var node = {
                id: "step-" + i,
                kind: steps[i].kind,
                label: steps[i].label,
                detail: steps[i].detail,
                parameters: steps[i].spec
            }
            nodes.push(node)
            if (i > 0) {
                edges.push({ from: "step-" + (i - 1), to: node.id,
                             label: steps[i - 1].kind === "condition"
                                    ? "YES" : "" })
            }
            if (steps[i].kind === "condition") {
                var haltId = "halt-" + i
                nodes.push({ id: haltId, kind: "terminal", tone: "fail",
                             label: "Halt", detail: "Condition did not pass" })
                edges.push({ from: node.id, to: haltId, label: "NO",
                             tone: "fail" })
            }
        }
        if (steps.length) {
            nodes.push({ id: "done", kind: "terminal", tone: "success",
                         label: "Done", detail: "Flow completed" })
            edges.push({ from: "step-" + (steps.length - 1), to: "done" })
        }
        return ({ nodes: nodes, edges: edges })
    }

    function formatWhen(value) {
        if (!value)
            return "never"
        var date = new Date(String(value))
        if (isNaN(date.getTime()))
            return String(value)
        return date.toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    function operationForSelected() {
        return bridge && selectedRule &&
               bridge.operationRule === String(selectedRule.id || "")
    }

    function drySelected() {
        if (!bridge || !selectedRule || bridge.busy)
            return
        armedRule = ""
        bridge.dryRun(String(selectedRule.id || ""))
    }

    function runSelected() {
        if (!bridge || !selectedRule || bridge.busy)
            return
        var id = String(selectedRule.id || "")
        if (armedRule !== id) {
            armedRule = id
            armTimer.restart()
            return
        }
        armedRule = ""
        bridge.run(id)
    }

    function openAuthor() {
        if (!bridge || !bridge.installed || bridge.busy)
            return
        authorPrompt = staging && staging.request
                       ? String(staging.request) : ""
        authorInput.text = authorPrompt
        authoring = true
        Qt.callLater(function() { authorInput.forceActiveFocus() })
    }

    function submitAuthor() {
        var request = String(authorPrompt || "").trim()
        if (!bridge || bridge.busy || !request.length)
            return
        if (bridge.author(request))
            authoring = false
    }

    function discardDraft() {
        if (!bridge || bridge.busy || !stagedVisible)
            return
        stageArmed = false
        bridge.rejectStage()
    }

    function installDraft() {
        if (!bridge || bridge.busy || !reviewingDraft)
            return
        if (!stageArmed) {
            stageArmed = true
            stageArmTimer.restart()
            return
        }
        stageArmed = false
        bridge.acceptStage()
    }

    function handleKey(event) {
        if (event.key === Qt.Key_D) {
            drySelected()
            event.accepted = true
        } else if (event.key === Qt.Key_R) {
            runSelected()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && armedRule.length) {
            armedRule = ""
            event.accepted = true
        }
    }

    onVisibleChanged: if (bridge) bridge.setActive(visible)
    onBridgeChanged: if (bridge) bridge.setActive(visible)
    onRulesChanged: {
        if (!rules.length)
            selectedIndex = -1
        else if (selectedIndex < 0 || selectedIndex >= rules.length)
            selectedIndex = firstEnabledIndex()
    }
    onStagingChanged: {
        if (!reviewingDraft)
            stageArmed = false
        if (staging && (staging.status === "compiling" ||
                        staging.status === "ready"))
            authoring = false
    }
    Component.onCompleted: {
        if (bridge)
            bridge.setActive(visible)
        selectedIndex = firstEnabledIndex()
    }
    Component.onDestruction: if (bridge) bridge.setActive(false)

    Timer {
        id: armTimer
        interval: 5000
        repeat: false
        onTriggered: panel.armedRule = ""
    }

    Timer {
        id: stageArmTimer
        interval: 5000
        repeat: false
        onTriggered: panel.stageArmed = false
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.opaqueBackground
    }

    Rectangle {
        id: stagedBar
        objectName: "omaflowStagedBar"
        visible: panel.stagedVisible
        x: 0
        y: 0
        width: parent.width
        height: panel.stagedHeight
        color: Theme.alpha(Theme.accent, 0.09)

        Rectangle {
            width: Theme.space(3)
            height: parent.height
            color: panel.staging.status === "error" ? Theme.urgent
                                                     : Theme.accent
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spaceLG
            anchors.right: stagedAction.left
            anchors.rightMargin: Theme.spaceLG
            anchors.verticalCenter: parent.verticalCenter
            text: panel.staging.status === "ready"
                  ? "DRAFT  ·  " + String(panel.staging.request || "awaiting review in Omaflow")
                  : panel.staging.status === "compiling"
                    ? "DRAFTING  ·  " + String(panel.staging.request || "")
                    : "DRAFT ERROR  ·  " + String(panel.staging.error || "could not compile rule")
            color: panel.staging.status === "error" ? Theme.urgent
                                                     : Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            elide: Text.ElideRight
        }

        ChromeButton {
            id: stagedAction
            objectName: "omaflowStagedAction"
            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceSM
            anchors.verticalCenter: parent.verticalCenter
            visible: panel.bridge &&
                     ((panel.bridge.busy &&
                       panel.bridge.operationKind === "author") ||
                      (!panel.bridge.busy &&
                       panel.staging.status === "error"))
            width: Theme.space(22)
            height: Theme.space(22)
            compact: true
            fallbackGlyph: "×"
            toolTip: panel.bridge && panel.bridge.busy
                     ? "Cancel this draft"
                     : "Dismiss this draft error"
            onTriggered: {
                if (!panel.bridge)
                    return
                if (panel.bridge.busy)
                    panel.bridge.cancel()
                else
                    panel.discardDraft()
            }
        }
    }

    Item {
        id: ruleRail
        objectName: "omaflowRuleRail"
        x: 0
        y: panel.stagedHeight
        width: panel.wide ? Math.min(Theme.space(270), panel.width * 0.34)
                          : panel.width
        height: panel.wide ? panel.height - panel.stagedHeight
                           : Math.min(Theme.space(190),
                                      (panel.height - panel.stagedHeight) * 0.38)

        Item {
            id: railHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spaceLG
                anchors.verticalCenter: parent.verticalCenter
                text: "AUTOMATIONS"
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                font.bold: true
                font.letterSpacing: 1
            }

            Text {
                anchors.right: newFlowButton.left
                anchors.rightMargin: Theme.spaceSM
                anchors.verticalCenter: parent.verticalCenter
                text: panel.rules.length
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
            }

            ChromeButton {
                id: newFlowButton
                objectName: "omaflowNewFlow"
                anchors.right: refreshButton.left
                anchors.rightMargin: Theme.spaceSM
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.space(22)
                label: panel.stagedVisible ? "REVISE" : "NEW"
                enabled: panel.bridge && panel.bridge.installed &&
                         !panel.bridge.busy
                toolTip: panel.stagedVisible
                         ? "Describe a replacement draft"
                         : "Author a flow with Omarchy's default agent"
                onTriggered: panel.openAuthor()
            }

            ChromeButton {
                id: refreshButton
                anchors.right: parent.right
                anchors.rightMargin: Theme.spaceSM
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.space(22)
                height: Theme.space(22)
                compact: true
                fallbackGlyph: "↻"
                toolTip: "Refresh Omaflow state"
                onTriggered: if (panel.bridge) panel.bridge.refresh()
            }
        }

        ListView {
            id: ruleList
            objectName: "omaflowRuleList"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: railHeader.bottom
            anchors.bottom: parent.bottom
            model: panel.rules
            clip: true
            currentIndex: panel.selectedIndex
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            highlightMoveDuration: 80
            onCurrentIndexChanged: if (currentIndex >= 0)
                                       panel.selectedIndex = currentIndex
            Keys.onPressed: function(event) { panel.handleKey(event) }

            delegate: Item {
                id: ruleRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: Theme.space(52)
                readonly property bool current: index === panel.selectedIndex

                Rectangle {
                    anchors.fill: parent
                    color: ruleRow.current ? Theme.selectedFill
                                           : (ruleHover.hovered
                                              ? Theme.hoverFill : "transparent")
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: ruleRow.current ? Theme.space(3) : 1
                    height: ruleRow.current ? parent.height - Theme.space(12)
                                            : Theme.space(16)
                    color: modelData.enabled === false ? Theme.muted
                                                       : Theme.accent
                    opacity: ruleRow.current ? 1 : 0.38
                }

                Rectangle {
                    id: stateDot
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spaceLG
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.space(7)
                    height: width
                    radius: width / 2
                    color: modelData.enabled === false ? "transparent"
                                                       : Theme.accent
                    border.color: Theme.muted
                    border.width: modelData.enabled === false ? 1 : 0
                }

                Column {
                    anchors.left: stateDot.right
                    anchors.leftMargin: Theme.spaceLG
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceLG
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceXS

                    Text {
                        width: parent.width
                        text: String(ruleRow.modelData.name ||
                                     ruleRow.modelData.id || "Unnamed flow")
                        color: Theme.foreground
                        opacity: ruleRow.modelData.enabled === false ? 0.55 : 1
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.bold: ruleRow.current
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: String(ruleRow.modelData.triggerSummary || "manual") +
                              "  →  " +
                              String(ruleRow.modelData.actionsSummary || "no actions")
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }

                HoverHandler { id: ruleHover }
                TapHandler {
                    onTapped: {
                        panel.selectedIndex = ruleRow.index
                        ruleList.currentIndex = ruleRow.index
                        ruleList.forceActiveFocus()
                    }
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: panel.wide ? 1 : 0
            color: Theme.normalBorder
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: panel.wide ? 0 : 1
            color: Theme.normalBorder
        }
    }

    Item {
        id: detail
        objectName: "omaflowFlowDetail"
        x: panel.wide ? ruleRail.width + 1 : 0
        y: panel.wide ? panel.stagedHeight : ruleRail.y + ruleRail.height + 1
        width: panel.wide ? panel.width - x : panel.width
        height: panel.height - y

        Item {
            anchors.fill: parent
            visible: !panel.bridge || !panel.bridge.installed

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.space(32), Theme.space(520))
                spacing: Theme.spaceLG

                Text {
                    width: parent.width
                    text: "OMAFLOW IS NOT INSTALLED"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: "Install the Omarchy plugin, then return here. Synchro will detect it automatically."
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Rectangle {
                    width: parent.width
                    height: installText.implicitHeight + Theme.space(14)
                    color: Theme.darkerBackground
                    border.color: Theme.normalBorder
                    border.width: 1
                    radius: Theme.radius

                    Text {
                        id: installText
                        anchors.centerIn: parent
                        width: parent.width - Theme.space(20)
                        text: "omarchy plugin add https://github.com/jlugner/omarchy-omaflow.git --enable"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.WrapAnywhere
                        horizontalAlignment: Text.AlignHCenter
                    }

                    TapHandler {
                        onTapped: if (panel.host)
                            panel.host.copyText(installText.text)
                    }
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: panel.bridge && panel.bridge.installed &&
                     panel.rules.length === 0 && !panel.reviewingDraft &&
                     !panel.authoring && !panel.stagedVisible

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.space(32), Theme.space(460))
                spacing: Theme.spaceLG

                Text {
                    width: parent.width
                    text: "NO FLOWS YET"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: "Describe an automation in Omaflow. Its reviewed rule and every future firing will appear here automatically."
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: panel.bridge.version
                          ? "connected  ·  Omaflow " + panel.bridge.version
                          : "connected  ·  Omaflow"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                }

                ChromeButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    label: "DESCRIBE A FLOW"
                    enabled: panel.bridge && !panel.bridge.busy
                    onTriggered: panel.openAuthor()
                }
            }
        }

        Item {
            anchors.fill: parent
            visible: panel.bridge && panel.bridge.installed &&
                     panel.rules.length === 0 && panel.stagedVisible &&
                     !panel.reviewingDraft && !panel.authoring

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.space(32), Theme.space(480))
                spacing: Theme.spaceLG

                Text {
                    width: parent.width
                    text: panel.staging.status === "compiling"
                          ? "DRAFTING FLOW"
                          : "DRAFT NEEDS REVISION"
                    color: panel.staging.status === "error" ? Theme.urgent
                                                             : Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: panel.staging.status === "compiling"
                          ? "Omarchy’s default agent is compiling the request into a reviewable graph."
                          : String(panel.staging.error || "Omaflow could not compile this request.")
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.spaceSM
                    visible: panel.staging.status === "error"

                    ChromeButton {
                        label: "DISMISS"
                        enabled: panel.bridge && !panel.bridge.busy
                        onTriggered: panel.discardDraft()
                    }

                    ChromeButton {
                        label: "REVISE"
                        enabled: panel.bridge && !panel.bridge.busy
                        onTriggered: panel.openAuthor()
                    }
                }
            }
        }

        Item {
            id: authorComposer
            objectName: "omaflowAuthorComposer"
            anchors.fill: parent
            visible: panel.authoring
            z: 20

            Rectangle {
                anchors.fill: parent
                color: Theme.opaqueBackground
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.space(40), Theme.space(620))
                spacing: Theme.space(14)

                Text {
                    width: parent.width
                    text: "DESCRIBE A FLOW"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                }

                Text {
                    width: parent.width
                    text: "Omaflow will compile this against the actions and devices available on this Omarchy system. You review the complete graph before anything is installed."
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBodySmall
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    width: parent.width
                    height: Math.max(Theme.space(86), authorInput.implicitHeight +
                                     Theme.space(24))
                    color: Theme.darkerBackground
                    border.color: authorInput.activeFocus ? Theme.focusBorder
                                                          : Theme.normalBorder
                    border.width: authorInput.activeFocus
                                  ? Math.max(1, Theme.focusBorderWidth) : 1
                    radius: Theme.radius

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.space(12)
                        visible: authorInput.text.length === 0
                        text: "When I connect the projector on weekdays, enable DND and open workspace 4…"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }

                    TextEdit {
                        id: authorInput
                        objectName: "omaflowAuthorInput"
                        anchors.fill: parent
                        anchors.margins: Theme.space(12)
                        color: Theme.foreground
                        selectionColor: Theme.selectionFill
                        selectedTextColor: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        onTextChanged: {
                            if (text.length > 2000)
                                text = text.substring(0, 2000)
                            if (panel.authorPrompt !== text)
                                panel.authorPrompt = text
                        }
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Escape) {
                                panel.authoring = false
                                ruleList.forceActiveFocus()
                                event.accepted = true
                            } else if ((event.key === Qt.Key_Return ||
                                        event.key === Qt.Key_Enter) &&
                                       (event.modifiers & Qt.ControlModifier)) {
                                panel.submitAuthor()
                                event.accepted = true
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: Math.max(authorHint.implicitHeight,
                                     authorButtons.implicitHeight)

                    Text {
                        id: authorHint
                        anchors.left: parent.left
                        anchors.right: authorButtons.left
                        anchors.rightMargin: Theme.spaceLG
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Ctrl+Enter drafts  ·  Esc returns"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }

                    Row {
                        id: authorButtons
                        anchors.right: parent.right
                        spacing: Theme.spaceSM

                        ChromeButton {
                            label: "CANCEL"
                            onTriggered: {
                                panel.authoring = false
                                ruleList.forceActiveFocus()
                            }
                        }

                        ChromeButton {
                            objectName: "omaflowDraftFlow"
                            label: panel.bridge && panel.bridge.busy &&
                                   panel.bridge.operationKind === "author"
                                   ? "DRAFTING…" : "DRAFT"
                            enabled: panel.bridge && !panel.bridge.busy &&
                                     String(panel.authorPrompt || "").trim().length > 0
                            onTriggered: panel.submitAuthor()
                        }
                    }
                }
            }
        }

        Flickable {
            id: flowFlick
            objectName: "omaflowFlowTrace"
            anchors.fill: parent
            visible: panel.bridge && panel.bridge.installed &&
                     panel.displayRule !== null && !panel.authoring
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: flowColumn.implicitHeight + Theme.space(24)

            Column {
                id: flowColumn
                x: Theme.spaceXL
                y: Theme.spaceLG
                width: flowFlick.width - Theme.space(20)
                spacing: Theme.space(14)

                Item {
                    width: parent.width
                    height: Math.max(ruleTitle.implicitHeight,
                                     ruleActions.implicitHeight)

                    Column {
                        id: ruleTitle
                        anchors.left: parent.left
                        anchors.right: ruleActions.left
                        anchors.rightMargin: Theme.spaceLG
                        spacing: Theme.spaceXS

                        Text {
                            width: parent.width
                            text: String(panel.displayRule
                                         ? panel.displayRule.name ||
                                           panel.displayRule.id : "")
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: panel.reviewingDraft
                                  ? "DRAFT  ·  COMPILED BY " +
                                    String(panel.staging.agent || "AGENT").toUpperCase() +
                                    "  ·  ENABLED ON INSTALL"
                                  : (panel.selectedRule && panel.selectedRule.renderOnly
                                   ? "RENDER LAB"
                                   : panel.selectedRule &&
                                   panel.selectedRule.enabled === false
                                   ? "DISABLED" : "ENABLED") +
                                  "  ·  last fired " +
                                  panel.formatWhen(panel.selectedRule
                                                   ? panel.selectedRule.lastFired
                                                   : "")
                            color: panel.reviewingDraft ? Theme.accent
                                   : panel.selectedRule &&
                                   panel.selectedRule.enabled === false
                                   ? Theme.muted : Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                            font.letterSpacing: 0.6
                        }
                    }

                    Row {
                        id: ruleActions
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: Theme.spaceSM

                        ChromeButton {
                            objectName: "omaflowDryRun"
                            visible: !panel.reviewingDraft
                            height: Theme.space(24)
                            label: panel.bridge && panel.bridge.busy &&
                                   panel.operationForSelected() &&
                                   panel.bridge.operationKind === "dry-run"
                                   ? "CHECKING…" : "DRY RUN"
                            enabled: panel.bridge && !panel.bridge.busy &&
                                     panel.selectedRule &&
                                     panel.selectedRule.enabled !== false
                            toolTip: "Ask Omaflow what it would do · D"
                            onTriggered: panel.drySelected()
                        }

                        ChromeButton {
                            objectName: "omaflowRun"
                            visible: !panel.reviewingDraft
                            height: Theme.space(24)
                            label: panel.armedRule === String(panel.selectedRule
                                                             ? panel.selectedRule.id
                                                             : "")
                                   ? "CONFIRM" : "RUN"
                            checked: panel.armedRule.length > 0
                            enabled: panel.bridge && !panel.bridge.busy &&
                                     panel.selectedRule &&
                                     panel.selectedRule.enabled !== false
                            toolTip: panel.armedRule.length
                                     ? "Run this flow now"
                                     : "Arm manual run · R"
                            onTriggered: panel.runSelected()
                        }

                        ChromeButton {
                            objectName: "omaflowDiscardDraft"
                            visible: panel.reviewingDraft
                            height: Theme.space(24)
                            label: "DISCARD"
                            enabled: panel.bridge && !panel.bridge.busy
                            toolTip: "Discard this staged rule"
                            onTriggered: panel.discardDraft()
                        }

                        ChromeButton {
                            objectName: "omaflowInstallDraft"
                            visible: panel.reviewingDraft
                            height: Theme.space(24)
                            label: panel.stageArmed ? "CONFIRM" : "INSTALL"
                            checked: panel.stageArmed
                            enabled: panel.bridge && !panel.bridge.busy
                            toolTip: panel.stageArmed
                                     ? "Install this enabled automation now"
                                     : "Arm installation of this enabled automation"
                            onTriggered: panel.installDraft()
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: panel.displayRule &&
                             (panel.reviewingDraft
                              ? panel.staging.request
                              : panel.displayRule.source)
                    text: "“" + String(panel.reviewingDraft
                                      ? panel.staging.request || ""
                                      : panel.displayRule
                                        ? panel.displayRule.source || "" : "") + "”"
                    color: Theme.darkForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBodySmall
                    font.italic: true
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    width: parent.width
                    height: draftWarnings.implicitHeight + Theme.space(16)
                    visible: panel.reviewingDraft &&
                             (panel.staging.warnings || []).length > 0
                    color: Theme.alpha(Theme.urgent, 0.055)
                    border.color: Theme.alpha(Theme.urgent, 0.42)
                    border.width: 1
                    radius: Theme.radius

                    Text {
                        id: draftWarnings
                        anchors.fill: parent
                        anchors.margins: Theme.spaceLG
                        text: "REVIEW NOTES  ·  " +
                              (panel.staging.warnings || []).join("  ·  ")
                        color: Theme.urgent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        wrapMode: Text.Wrap
                    }
                }

                FlowGraph {
                    id: trace
                    objectName: "omaflowFlowGraph"
                    width: parent.width
                    graphSpec: panel.graphFor(panel.displayRule)
                    horizontalLayout: flowFlick.width >= Theme.space(780) &&
                                      flowFlick.width > flowFlick.height * 1.18
                }

                Rectangle {
                    objectName: "omaflowOperationResult"
                    width: parent.width
                    height: operationCopy.implicitHeight + Theme.space(16)
                    visible: panel.bridge &&
                             (panel.operationForSelected() ||
                              (panel.reviewingDraft &&
                               String(panel.bridge.operationKind || "")
                                     .indexOf("stage-") === 0)) &&
                             (panel.bridge.busy ||
                              panel.bridge.operationOutput.length > 0)
                    color: Theme.darkerBackground
                    radius: Theme.radius

                    Text {
                        id: operationCopy
                        anchors.left: parent.left
                        anchors.right: cancelOperation.left
                        anchors.margins: Theme.spaceLG
                        anchors.verticalCenter: parent.verticalCenter
                        text: panel.bridge.busy
                              ? panel.bridge.operationKind.toUpperCase() + "  ·  running"
                              : panel.bridge.operationKind.toUpperCase() + "  ·  " +
                                panel.bridge.operationOutput
                        color: panel.bridge.busy ? Theme.accent
                                                 : Theme.darkForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }

                    ChromeButton {
                        id: cancelOperation
                        visible: panel.bridge && panel.bridge.busy
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceSM
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.space(22)
                        height: Theme.space(22)
                        compact: true
                        fallbackGlyph: "×"
                        toolTip: "Cancel"
                        onTriggered: panel.bridge.cancel()
                    }
                }

                Column {
                    width: parent.width
                    visible: !panel.reviewingDraft && panel.activity.length > 0
                    spacing: Theme.spaceSM

                    Text {
                        text: "RECENT ACTIVITY"
                        color: Theme.darkForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        font.letterSpacing: 1
                    }

                    Repeater {
                        model: Math.min(5, panel.activity.length)

                        Item {
                            required property int index
                            width: parent.width
                            height: activityText.implicitHeight + Theme.spaceSM
                            property var entry: panel.activity[index] || ({})

                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.space(5)
                                height: width
                                radius: width / 2
                                color: entry.status === "ok" ? Theme.accent
                                      : entry.status === "failed" ||
                                        entry.status === "error"
                                        ? Theme.urgent : Theme.muted
                            }

                            Text {
                                id: activityText
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.space(14)
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: panel.formatWhen(entry.at) + "  ·  " +
                                      String(entry.ruleName || entry.ruleId ||
                                             entry.kind || "activity") +
                                      "  ·  " + String(entry.status || "")
                                color: Theme.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
