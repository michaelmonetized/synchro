import QtQuick
import Synchro.Theme 1.0

// A passive, window-level compound tooltip. Keeping it outside the graph's
// clipped Flickable lets dense flows remain inspectable at every dock edge.
Rectangle {
    id: root

    property Item anchorItem: null
    property var node: ({})
    property var incoming: []
    property var outgoing: []
    property bool shown: false
    property bool pinned: false
    property color accentColor: Theme.accent
    property int positionRevision: 0

    readonly property int edgeGap: Theme.spaceLG
    readonly property int contentPadding: Theme.space(13)
    readonly property var parameterRows: collectParameters()
    readonly property int routeLimit: 4
    readonly property int naturalHeight: inspectorColumn.implicitHeight +
                                         contentPadding * 2

    function displayValue(value) {
        if (value === undefined)
            return "—"
        if (value === null)
            return "null"
        if (typeof value === "boolean")
            return value ? "true" : "false"
        if (Array.isArray(value) ||
                (typeof value === "object" && value.length !== undefined)) {
            var values = []
            for (var i = 0; i < value.length; ++i)
                values.push(displayValue(value[i]))
            return values.join(" · ")
        }
        if (typeof value === "object") {
            var keys = Object.keys(value).sort()
            var parts = []
            for (var k = 0; k < keys.length; ++k)
                parts.push(keys[k].replace(/_/g, " ") + " " +
                           displayValue(value[keys[k]]))
            return parts.join(" · ")
        }
        return String(value)
    }

    function collectParameters() {
        var raw = root.node && root.node.rawData
                  ? root.node.rawData : (root.node || ({}))
        var rows = []
        var seen = ({})
        var reserved = ({
            id: true, kind: true, tone: true, label: true, detail: true,
            order: true, rank: true, requestedRank: true, sourceIndex: true,
            cycle: true, x: true, y: true, rawData: true, parameters: true
        })

        function append(source, skipType) {
            if (!source || typeof source !== "object")
                return
            var keys = Object.keys(source).sort()
            for (var i = 0; i < keys.length; ++i) {
                var key = keys[i]
                if (reserved[key] || (skipType && key === "type") || seen[key])
                    continue
                seen[key] = true
                rows.push({ key: key.replace(/_/g, " ").toUpperCase(),
                            value: displayValue(source[key]) })
            }
        }

        append(raw.parameters, true)
        append(raw, false)
        return rows
    }

    function anchorOrigin() {
        if (!anchorItem || !parent)
            return Qt.point(0, 0)
        var scenePoint = anchorItem.mapToItem(null, 0, 0)
        return parent.mapFromItem(null, scenePoint.x, scenePoint.y)
    }

    onShownChanged: if (shown) ++positionRevision
    onAnchorItemChanged: ++positionRevision

    parent: anchorItem && anchorItem.Window.window
            ? anchorItem.Window.window.contentItem : anchorItem
    visible: shown && anchorItem !== null
    enabled: false
    z: 12000
    width: parent ? Math.min(Theme.space(344),
                             Math.max(Theme.space(250),
                                      parent.width - edgeGap * 2))
                  : Theme.space(344)
    height: parent ? Math.min(naturalHeight, parent.height - edgeGap * 2)
                   : naturalHeight
    x: {
        var revision = root.positionRevision
        if (!anchorItem || !parent)
            return 0
        var p = anchorOrigin()
        var right = p.x + anchorItem.width + edgeGap
        if (right + width + edgeGap <= parent.width)
            return right
        return Math.max(edgeGap, p.x - width - edgeGap)
    }
    y: {
        var revision = root.positionRevision
        if (!anchorItem || !parent)
            return 0
        var p = anchorOrigin()
        var centered = p.y + anchorItem.height / 2 - height / 2
        return Math.max(edgeGap,
                        Math.min(parent.height - height - edgeGap, centered))
    }
    color: Theme.alpha(Theme.tooltipBackground, 0.985)
    border.color: Theme.alpha(accentColor, pinned ? 0.88 : 0.62)
    border.width: 1
    radius: Theme.radius
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.space(3)
        color: root.accentColor
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: Theme.space(3)
        contentWidth: width
        contentHeight: inspectorColumn.implicitHeight + root.contentPadding * 2
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        clip: true

        Column {
            id: inspectorColumn
            x: root.contentPadding
            y: root.contentPadding
            width: parent.width - root.contentPadding * 2
            spacing: Theme.spaceMD

            Item {
                width: parent.width
                height: Math.max(stepKind.implicitHeight, pinState.implicitHeight)

                Text {
                    id: stepKind
                    anchors.left: parent.left
                    text: String(root.node.kind || "step").toUpperCase()
                    color: root.accentColor
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 1
                }

                Text {
                    id: pinState
                    anchors.right: parent.right
                    text: root.pinned ? "●  PINNED" : "HOVER"
                    color: root.pinned ? root.accentColor : Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 0.7
                }
            }

            Text {
                width: parent.width
                text: String(root.node.label || "Step")
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                visible: text.length > 0
                text: String(root.node.detail || "")
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBodySmall
                wrapMode: Text.Wrap
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.alpha(root.accentColor, 0.28)
            }

            FlowInspectorRoutes {
                width: parent.width
                heading: "INCOMING"
                directionGlyph: "←"
                routes: root.incoming
                accentColor: root.accentColor
                routeLimit: root.routeLimit
            }

            Column {
                width: parent.width
                visible: root.parameterRows.length > 0
                spacing: Theme.spaceSM

                Text {
                    text: "PARAMETERS"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                    font.letterSpacing: 0.8
                }

                Repeater {
                    model: root.parameterRows.slice(0, 8)

                    Item {
                        objectName: "omaflowInspectorParameter"
                        required property var modelData
                        width: parent.width
                        height: Math.max(parameterKey.implicitHeight,
                                         parameterValue.implicitHeight)

                        Text {
                            id: parameterKey
                            width: Math.min(Theme.space(104), parent.width * 0.34)
                            text: String(parent.modelData.key || "")
                            color: Theme.alpha(root.accentColor, 0.78)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            id: parameterValue
                            anchors.left: parameterKey.right
                            anchors.leftMargin: Theme.spaceLG
                            anchors.right: parent.right
                            text: String(parent.modelData.value || "—")
                            color: Theme.foreground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Text {
                    visible: root.parameterRows.length > 8
                    text: "+ " + (root.parameterRows.length - 8) + " more fields"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                }
            }

            FlowInspectorRoutes {
                width: parent.width
                heading: "OUTGOING"
                directionGlyph: "→"
                routes: root.outgoing
                accentColor: root.accentColor
                routeLimit: root.routeLimit
            }

            Text {
                width: parent.width
                text: root.pinned ? "click step again or Esc to close"
                                  : "click step to pin"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
