import QtQuick

Rectangle {
    id: root
    objectName: "tooltipOverlay"

    property string label: ""
    property bool shown: false
    property Item anchorItem: null
    // mapToItem() is a function call, so a binding cannot observe movement of
    // the anchor's ancestors (for example when the app dock changes edge).
    // Re-evaluate the scene mapping each time a tooltip is revealed.
    property int positionRevision: 0
    readonly property int edgeGap: Theme.spaceSM

    onShownChanged: if (shown) ++positionRevision
    onAnchorItemChanged: ++positionRevision

    function anchorOrigin() {
        if (!anchorItem || !parent)
            return Qt.point(0, 0)
        var scenePoint = anchorItem.mapToItem(null, 0, 0)
        return parent.mapFromItem(null, scenePoint.x, scenePoint.y)
    }

    parent: anchorItem && anchorItem.Window.window
            ? anchorItem.Window.window.contentItem : anchorItem
    visible: shown && label.length > 0
    enabled: false
    z: 10000
    color: Theme.alpha(Theme.tooltipBackground,
                       Theme.number("tooltip.background-alpha", 0.97))
    border.color: Theme.tooltipBorder
    border.width: 1
    radius: Theme.radius
    implicitWidth: tip.implicitWidth + Theme.spaceXL * 2
    implicitHeight: Math.max(Theme.controlHeight, tip.implicitHeight + Theme.spaceMD * 2)
    width: implicitWidth
    height: implicitHeight
    x: {
        var revision = root.positionRevision
        if (!anchorItem || !parent)
            return 0
        var p = anchorOrigin()
        return Math.max(edgeGap,
                        Math.min(parent.width - width - edgeGap,
                                 p.x + (anchorItem.width - width) / 2))
    }
    y: {
        var revision = root.positionRevision
        if (!anchorItem || !parent)
            return 0
        var p = anchorOrigin()
        var below = p.y + anchorItem.height + edgeGap
        if (below + height + edgeGap <= parent.height)
            return below
        return Math.max(edgeGap, p.y - height - edgeGap)
    }

    Text {
        id: tip
        anchors.centerIn: parent
        text: root.label
        color: Theme.tooltipText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption
    }
}
