import QtQuick

FocusScope {
    id: root

    property string iconName: ""
    property string fallbackGlyph: ""
    property string label: ""
    property string toolTip: ""
    property bool checked: false
    property bool checkedOutline: false
    property bool compact: label.length === 0
    // A selected segment must remain selected-looking after a click gives it
    // keyboard focus.  Selection is the persistent state; focus is only the
    // transient input state and therefore cannot replace its outline.
    readonly property color resolvedBorderColor:
        checked && checkedOutline ? Theme.alpha(Theme.accent, 0.78)
                                  : (activeFocus ? Theme.focusBorder
                                                 : (hover.hovered
                                                    ? Theme.hoverBorder
                                                    : Theme.normalBorder))
    readonly property real resolvedBorderWidth:
        checked && checkedOutline
        ? Math.max(1, Theme.normalBorderWidth,
                   activeFocus ? Theme.focusBorderWidth : 0)
        : (activeFocus ? Math.max(1, Theme.focusBorderWidth)
                       : (hover.hovered ? Theme.hoverBorderWidth
                                        : Theme.normalBorderWidth))
    signal triggered()

    implicitHeight: Theme.controlHeight
    implicitWidth: compact ? Theme.controlHeight
                           : content.implicitWidth + Theme.controlPaddingX * 2
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.42

    Rectangle {
        anchors.fill: parent
        color: root.checked ? Theme.selectedFill
                            : (tap.pressed ? Theme.pressedFill
                                           : (hover.hovered || root.activeFocus
                                              ? Theme.hoverFill : Theme.normalFill))
        border.color: root.resolvedBorderColor
        border.width: root.resolvedBorderWidth
        radius: Theme.radius
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: root.label.length ? Theme.spaceSM : 0

        AppIcon {
            visible: root.iconName.length > 0 || root.fallbackGlyph.length > 0
            width: Theme.fontIconLarge
            height: Theme.fontIconLarge
            iconSize: Theme.fontIconLarge
            name: root.iconName
            fallback: root.fallbackGlyph
        }

        Text {
            visible: root.label.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.checked ? Theme.brightForeground : Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            font.bold: root.checked
        }
    }

    HoverHandler { id: hover }
    TapHandler {
        id: tap
        enabled: root.enabled
        onTapped: {
            root.forceActiveFocus()
            root.triggered()
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
                event.key === Qt.Key_Space) {
            root.triggered()
            event.accepted = true
        }
    }

    ToolTip {
        shown: hover.hovered
        label: root.toolTip
        anchorItem: root
    }
}
