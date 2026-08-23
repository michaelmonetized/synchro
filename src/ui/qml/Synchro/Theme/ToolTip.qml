import QtQuick

Rectangle {
    id: root
    objectName: "tooltipOverlay"

    property string label: ""
    property bool shown: false
    property Item anchorItem: null
    readonly property int edgeGap: Theme.spaceSM

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
        if (!anchorItem || !parent)
            return 0
        var p = anchorItem.mapToItem(parent, 0, 0)
        return Math.max(edgeGap,
                        Math.min(parent.width - width - edgeGap,
                                 p.x + (anchorItem.width - width) / 2))
    }
    y: {
        if (!anchorItem || !parent)
            return 0
        var p = anchorItem.mapToItem(parent, 0, 0)
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
