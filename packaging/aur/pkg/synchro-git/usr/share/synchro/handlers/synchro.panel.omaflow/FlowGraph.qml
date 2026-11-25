import QtQuick
import Synchro.Theme 1.0

// Adaptive layered graph for both Omaflow schema-v1 chains and render-only
// future DAGs. The layout is deterministic, bounded for malformed cycles, and
// keeps execution semantics in the edges rather than inventing visual order.
Item {
    id: root

    property var graphSpec: ({ nodes: [], edges: [] })
    property bool horizontalLayout: false
    property var inspectedNode: null
    property Item inspectedAnchor: null
    property bool inspectorPinned: false
    property var pendingNode: null
    property Item pendingAnchor: null
    property int inspectorPositionRevision: 0

    readonly property int pad: Theme.space(14)
    readonly property int cardWidth: horizontalLayout ? Theme.space(178)
                                                      : Theme.space(190)
    readonly property int cardHeight: Theme.space(72)
    readonly property int rankGap: horizontalLayout ? Theme.space(66)
                                                    : Theme.space(50)
    readonly property int laneGap: Theme.space(28)
    readonly property var layoutData: computeLayout()
    readonly property int nodeCount: layoutData.nodes.length
    readonly property int graphWidth: Math.max(width, layoutData.width)
    readonly property int graphHeight: layoutData.height
    readonly property bool inspectorShown: inspectedNode !== null &&
                                           inspectedAnchor !== null

    implicitHeight: graphHeight
    clip: true

    function nodeColor(kind, tone) {
        if (tone === "fail")
            return Theme.urgent
        if (tone === "success")
            return Theme.themedColor("colors.green", Theme.accent)
        if (kind === "trigger")
            return Theme.accent
        if (kind === "condition")
            return Theme.themedColor("colors.orange", Theme.urgent)
        if (kind === "fork" || kind === "join")
            return Theme.themedColor("colors.cyan", Theme.accent)
        if (kind === "delay" || kind === "subflow")
            return Theme.themedColor("colors.magenta", Theme.lightForeground)
        return Theme.lightForeground
    }

    function edgeColor(tone) {
        if (tone === "fail")
            return Theme.alpha(Theme.urgent, 0.64)
        if (tone === "success" || tone === "pass")
            return Theme.alpha(Theme.themedColor("colors.green", Theme.accent), 0.68)
        if (tone === "retry")
            return Theme.alpha(Theme.themedColor("colors.orange", Theme.urgent), 0.72)
        return Theme.alpha(Theme.lightForeground, 0.48)
    }

    function kindLabel(index, kind) {
        var labels = {
            trigger: "WHEN", condition: "IF", action: "DO",
            fork: "SPLIT", join: "JOIN", delay: "WAIT",
            subflow: "CALL", terminal: "END"
        }
        var base = labels[kind] || "STEP"
        if (kind === "trigger" || kind === "fork" || kind === "join" ||
                kind === "terminal")
            return base
        var count = 0
        for (var i = 0; i <= index && i < layoutData.nodes.length; ++i) {
            if (String(layoutData.nodes[i].kind || "") === kind)
                ++count
        }
        return base + "  " + String(count).padStart(2, "0")
    }

    function routesFor(nodeId, incoming) {
        var routes = []
        var edges = layoutData.edges || []
        for (var i = 0; i < edges.length; ++i) {
            var edge = edges[i]
            if ((incoming && edge.to !== nodeId) ||
                    (!incoming && edge.from !== nodeId))
                continue
            var peer = incoming ? edge.fromNode : edge.toNode
            routes.push({
                label: String(edge.label || (edge.backEdge ? "LOOP" : "NEXT")),
                peer: String(peer ? peer.label || peer.id : "step"),
                tone: String(edge.tone || ""),
                backEdge: !!edge.backEdge
            })
        }
        return routes
    }

    function inspectNode(node, anchor, pinned) {
        pendingNode = null
        pendingAnchor = null
        hoverDelay.stop()
        inspectedNode = node
        inspectedAnchor = anchor
        inspectorPinned = !!pinned
        ++inspectorPositionRevision
    }

    function previewNode(node, anchor) {
        if (inspectorPinned)
            return
        pendingNode = node
        pendingAnchor = anchor
        hoverDelay.restart()
    }

    function leaveNode(node) {
        if (inspectorPinned)
            return
        if (pendingNode && node && pendingNode.id === node.id) {
            pendingNode = null
            pendingAnchor = null
            hoverDelay.stop()
        }
        if (inspectedNode && node && inspectedNode.id === node.id)
            dismissInspector()
    }

    function toggleInspector(node, anchor) {
        if (inspectorPinned && inspectedNode && node &&
                inspectedNode.id === node.id) {
            dismissInspector()
            return
        }
        inspectNode(node, anchor, true)
    }

    function dismissInspector() {
        pendingNode = null
        pendingAnchor = null
        hoverDelay.stop()
        inspectorPinned = false
        inspectedNode = null
        inspectedAnchor = null
    }

    function computeLayout() {
        var spec = root.graphSpec || ({})
        var sourceNodes = spec.nodes || []
        var sourceEdges = spec.edges || []
        var nodes = []
        var byId = ({})
        var i
        for (i = 0; i < sourceNodes.length; ++i) {
            var raw = sourceNodes[i] || ({})
            var id = String(raw.id || ("node-" + i))
            if (byId[id] !== undefined)
                id += "-" + i
            var copy = ({
                id: id,
                kind: String(raw.kind || "action"),
                tone: String(raw.tone || ""),
                label: String(raw.label || id),
                detail: String(raw.detail || ""),
                rawData: raw,
                order: raw.order === undefined ? i : Number(raw.order),
                requestedRank: raw.rank === undefined ? -1 : Number(raw.rank),
                sourceIndex: i,
                rank: 0,
                cycle: false,
                x: 0,
                y: 0
            })
            byId[id] = nodes.length
            nodes.push(copy)
        }

        var edges = []
        var indegree = []
        var outgoing = []
        for (i = 0; i < nodes.length; ++i) {
            indegree.push(0)
            outgoing.push([])
        }
        for (i = 0; i < sourceEdges.length; ++i) {
            var rawEdge = sourceEdges[i] || ({})
            var fromId = String(rawEdge.from || "")
            var toId = String(rawEdge.to || "")
            if (byId[fromId] === undefined || byId[toId] === undefined)
                continue
            var edge = ({
                from: fromId,
                to: toId,
                fromIndex: byId[fromId],
                toIndex: byId[toId],
                label: String(rawEdge.label || ""),
                tone: String(rawEdge.tone || ""),
                backEdge: false
            })
            edges.push(edge)
            indegree[edge.toIndex] += 1
            outgoing[edge.fromIndex].push(edge.toIndex)
        }

        // Longest-path ranks via Kahn. Explicit ranks are lower bounds and
        // also give cyclic render-lab graphs a stable author-controlled shape.
        var queue = []
        for (i = 0; i < nodes.length; ++i) {
            nodes[i].rank = Math.max(0, nodes[i].requestedRank)
            if (indegree[i] === 0)
                queue.push(i)
        }
        var seen = []
        for (i = 0; i < nodes.length; ++i)
            seen.push(false)
        while (queue.length) {
            var current = queue.shift()
            if (seen[current])
                continue
            seen[current] = true
            for (var o = 0; o < outgoing[current].length; ++o) {
                var target = outgoing[current][o]
                nodes[target].rank = Math.max(nodes[target].rank,
                                              nodes[current].rank + 1,
                                              nodes[target].requestedRank)
                indegree[target] -= 1
                if (indegree[target] === 0)
                    queue.push(target)
            }
        }
        var maxRank = 0
        for (i = 0; i < nodes.length; ++i)
            maxRank = Math.max(maxRank, nodes[i].rank)
        for (i = 0; i < nodes.length; ++i) {
            if (seen[i])
                continue
            nodes[i].cycle = true
            nodes[i].rank = nodes[i].requestedRank >= 0
                            ? nodes[i].requestedRank : ++maxRank
            maxRank = Math.max(maxRank, nodes[i].rank)
        }

        var layers = []
        for (i = 0; i <= maxRank; ++i)
            layers.push([])
        for (i = 0; i < nodes.length; ++i)
            layers[nodes[i].rank].push(nodes[i])
        var maxLanes = 1
        for (i = 0; i < layers.length; ++i) {
            layers[i].sort(function(a, b) {
                return a.order === b.order ? a.sourceIndex - b.sourceIndex
                                           : a.order - b.order
            })
            maxLanes = Math.max(maxLanes, layers[i].length)
        }

        var crossSize = root.horizontalLayout ? root.cardHeight : root.cardWidth
        var crossGap = root.laneGap
        var crossSpan = maxLanes * crossSize +
                        Math.max(0, maxLanes - 1) * crossGap
        for (var rank = 0; rank < layers.length; ++rank) {
            var layer = layers[rank]
            var layerSpan = layer.length * crossSize +
                            Math.max(0, layer.length - 1) * crossGap
            var offset = root.pad + Math.round((crossSpan - layerSpan) / 2)
            for (var lane = 0; lane < layer.length; ++lane) {
                if (root.horizontalLayout) {
                    layer[lane].x = root.pad + rank *
                                    (root.cardWidth + root.rankGap)
                    layer[lane].y = offset + lane *
                                    (root.cardHeight + root.laneGap)
                } else {
                    layer[lane].x = offset + lane *
                                    (root.cardWidth + root.laneGap)
                    layer[lane].y = root.pad + rank *
                                    (root.cardHeight + root.rankGap)
                }
            }
        }

        var laidOutById = ({})
        for (i = 0; i < nodes.length; ++i)
            laidOutById[nodes[i].id] = nodes[i]
        for (i = 0; i < edges.length; ++i) {
            edges[i].fromNode = laidOutById[edges[i].from]
            edges[i].toNode = laidOutById[edges[i].to]
            edges[i].backEdge = edges[i].toNode.rank <= edges[i].fromNode.rank
        }

        var width = root.horizontalLayout
                    ? root.pad * 2 + layers.length * root.cardWidth +
                      Math.max(0, layers.length - 1) * root.rankGap
                    : root.pad * 2 + crossSpan
        var height = root.horizontalLayout
                     ? root.pad * 2 + crossSpan
                     : root.pad * 2 + layers.length * root.cardHeight +
                       Math.max(0, layers.length - 1) * root.rankGap
        // Back-edges travel around the far cross-axis edge.
        var hasBackEdge = false
        for (i = 0; i < edges.length; ++i)
            hasBackEdge = hasBackEdge || edges[i].backEdge
        if (hasBackEdge) {
            if (root.horizontalLayout)
                height += root.laneGap
            else
                width += root.laneGap
        }
        return ({ nodes: nodes, edges: edges,
                  width: Math.max(1, width), height: Math.max(1, height) })
    }

    onHorizontalLayoutChanged: graphPan.contentX = 0
    onGraphSpecChanged: {
        graphPan.contentX = 0
        dismissInspector()
    }
    onLayoutDataChanged: edgeCanvas.requestPaint()

    Timer {
        id: hoverDelay
        interval: 180
        repeat: false
        onTriggered: {
            if (root.pendingNode && root.pendingAnchor)
                root.inspectNode(root.pendingNode, root.pendingAnchor, false)
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.inspectorPinned
        onActivated: root.dismissInspector()
    }

    Flickable {
        id: graphPan
        objectName: "omaflowGraphPan"
        anchors.fill: parent
        contentWidth: graphSurface.width
        contentHeight: graphSurface.height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        onContentXChanged: ++root.inspectorPositionRevision
        onContentYChanged: ++root.inspectorPositionRevision

        Item {
            id: graphSurface
            width: root.graphWidth
            height: root.graphHeight

            Canvas {
                id: edgeCanvas
                objectName: "omaflowGraphEdges"
                anchors.fill: parent
                renderStrategy: Canvas.Threaded
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.lineWidth = 1
                    var edges = root.layoutData.edges
                    for (var i = 0; i < edges.length; ++i) {
                        var edge = edges[i]
                        var from = edge.fromNode
                        var to = edge.toNode
                        if (!from || !to)
                            continue
                        ctx.strokeStyle = root.edgeColor(edge.tone)
                        ctx.fillStyle = root.edgeColor(edge.tone)
                        ctx.beginPath()
                        var sx, sy, ex, ey, mid
                        if (root.horizontalLayout) {
                            sx = from.x + root.cardWidth
                            sy = from.y + root.cardHeight / 2
                            ex = to.x
                            ey = to.y + root.cardHeight / 2
                            if (edge.backEdge) {
                                var routeY = root.graphHeight - root.pad / 2
                                ctx.moveTo(sx, sy)
                                ctx.lineTo(sx + root.rankGap / 3, sy)
                                ctx.lineTo(sx + root.rankGap / 3, routeY)
                                ctx.lineTo(ex - root.rankGap / 3, routeY)
                                ctx.lineTo(ex - root.rankGap / 3, ey)
                                ctx.lineTo(ex, ey)
                            } else {
                                mid = (sx + ex) / 2
                                ctx.moveTo(sx, sy)
                                ctx.lineTo(mid, sy)
                                ctx.lineTo(mid, ey)
                                ctx.lineTo(ex, ey)
                            }
                            ctx.stroke()
                            ctx.beginPath()
                            ctx.moveTo(ex, ey)
                            ctx.lineTo(ex - Theme.space(5), ey - Theme.space(3))
                            ctx.lineTo(ex - Theme.space(5), ey + Theme.space(3))
                        } else {
                            sx = from.x + root.cardWidth / 2
                            sy = from.y + root.cardHeight
                            ex = to.x + root.cardWidth / 2
                            ey = to.y
                            if (edge.backEdge) {
                                var routeX = root.graphWidth - root.pad / 2
                                ctx.moveTo(sx, sy)
                                ctx.lineTo(sx, sy + root.rankGap / 3)
                                ctx.lineTo(routeX, sy + root.rankGap / 3)
                                ctx.lineTo(routeX, ey - root.rankGap / 3)
                                ctx.lineTo(ex, ey - root.rankGap / 3)
                                ctx.lineTo(ex, ey)
                            } else {
                                mid = (sy + ey) / 2
                                ctx.moveTo(sx, sy)
                                ctx.lineTo(sx, mid)
                                ctx.lineTo(ex, mid)
                                ctx.lineTo(ex, ey)
                            }
                            ctx.stroke()
                            ctx.beginPath()
                            ctx.moveTo(ex, ey)
                            ctx.lineTo(ex - Theme.space(3), ey - Theme.space(5))
                            ctx.lineTo(ex + Theme.space(3), ey - Theme.space(5))
                        }
                        ctx.closePath()
                        ctx.fill()
                    }
                }
            }

            Repeater {
                model: root.layoutData.edges

                Rectangle {
                    id: edgeChip
                    objectName: "omaflowGraphEdgeLabel"
                    required property var modelData
                    readonly property var fromNode: modelData.fromNode || ({})
                    readonly property var toNode: modelData.toNode || ({})
                    visible: String(modelData.label || "").length > 0
                    x: root.horizontalLayout
                       ? Math.round((fromNode.x + root.cardWidth + toNode.x) / 2 - width / 2)
                       : (modelData.backEdge
                          ? root.graphWidth - root.pad - width
                          : Math.round((fromNode.x + toNode.x) / 2 + root.cardWidth / 2 - width / 2))
                    y: root.horizontalLayout
                       ? (modelData.backEdge
                          ? root.graphHeight - root.pad - height
                          : Math.round((fromNode.y + toNode.y) / 2 + root.cardHeight / 2 - height / 2))
                       : Math.round((fromNode.y + root.cardHeight + toNode.y) / 2 - height / 2)
                    width: edgeText.implicitWidth + Theme.space(8)
                    height: edgeText.implicitHeight + Theme.space(3)
                    radius: height / 2
                    color: Theme.opaqueBackground
                    border.color: root.edgeColor(String(modelData.tone || ""))
                    border.width: 1

                    Text {
                        id: edgeText
                        anchors.centerIn: parent
                        text: String(edgeChip.modelData.label || "")
                        color: root.edgeColor(String(edgeChip.modelData.tone || ""))
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        font.letterSpacing: 0.6
                    }
                }
            }

            Repeater {
                model: root.layoutData.nodes

                Rectangle {
                    id: nodeCard
                    objectName: "omaflowGraphNode"
                    required property var modelData
                    required property int index
                    readonly property string kind: String(modelData.kind || "action")
                    readonly property string tone: String(modelData.tone || "")
                    readonly property color signal: root.nodeColor(kind, tone)
                    x: modelData.x
                    y: modelData.y
                    width: root.cardWidth
                    height: root.cardHeight
                    radius: kind === "trigger" || kind === "terminal"
                            ? Theme.radius * 2 : Theme.radius
                    color: Theme.alpha(signal,
                        kind === "condition" || kind === "fork" || kind === "join"
                        ? 0.055 : 0.075)
                    border.color: Theme.alpha(signal, 0.52)
                    border.width: 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spaceLG
                        anchors.top: parent.top
                        anchors.topMargin: Theme.spaceLG
                        width: Theme.space(9)
                        height: width
                        radius: nodeCard.kind === "trigger" ||
                                nodeCard.kind === "terminal" ? width / 2 : 0
                        rotation: nodeCard.kind === "condition" ? 45 : 0
                        color: nodeCard.signal
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space(25)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceLG
                        anchors.top: parent.top
                        anchors.topMargin: Theme.spaceMD
                        text: root.kindLabel(nodeCard.index, nodeCard.kind)
                        color: nodeCard.signal
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                        font.letterSpacing: 0.8
                    }

                    Text {
                        id: nodeLabel
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spaceLG
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceLG
                        anchors.top: parent.top
                        anchors.topMargin: Theme.space(25)
                        text: String(nodeCard.modelData.label || "Step")
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spaceLG
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceLG
                        anchors.top: nodeLabel.bottom
                        anchors.topMargin: Theme.spaceXS
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.spaceMD
                        text: String(nodeCard.modelData.detail || "")
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        visible: nodeCard.kind === "fork" || nodeCard.kind === "join"
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spaceLG
                        anchors.top: parent.top
                        anchors.topMargin: Theme.spaceLG
                        width: Theme.space(16)
                        height: 1
                        color: nodeCard.signal

                        Rectangle {
                            anchors.centerIn: parent
                            width: 1
                            height: Theme.space(10)
                            color: parent.color
                        }
                    }

                    MouseArea {
                        objectName: "omaflowGraphNodePointer"
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        preventStealing: false
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.previewNode(nodeCard.modelData, nodeCard)
                        onExited: root.leaveNode(nodeCard.modelData)
                        onClicked: root.toggleInspector(nodeCard.modelData,
                                                        nodeCard)
                    }
                }
            }
        }
    }

    FlowInspector {
        id: stepInspector
        objectName: "omaflowNodeInspector"
        anchorItem: root.inspectedAnchor
        node: root.inspectedNode || ({})
        incoming: root.inspectedNode
                  ? root.routesFor(String(root.inspectedNode.id || ""), true)
                  : []
        outgoing: root.inspectedNode
                  ? root.routesFor(String(root.inspectedNode.id || ""), false)
                  : []
        shown: root.inspectorShown
        pinned: root.inspectorPinned
        accentColor: root.inspectedNode
                     ? root.nodeColor(String(root.inspectedNode.kind || "action"),
                                      String(root.inspectedNode.tone || ""))
                     : Theme.accent
        positionRevision: root.inspectorPositionRevision
    }

    Rectangle {
        visible: graphPan.contentWidth > graphPan.width
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.alpha(Theme.normalBorder, 0.45)

        Rectangle {
            height: 2
            y: -1
            width: parent.width * Math.min(1, graphPan.width / graphPan.contentWidth)
            x: (parent.width - width) *
               (graphPan.contentWidth > graphPan.width
                ? graphPan.contentX / (graphPan.contentWidth - graphPan.width) : 0)
            color: Theme.alpha(Theme.accent, 0.72)
        }
    }
}
