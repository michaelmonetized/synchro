import QtQuick

// Placeholder folder glyph. Same Omarchy colors as C++ renderFolderCard.
Item {
    id: root

    Rectangle {
        id: tab
        x: 1
        y: 1
        width: Math.max(8, Math.round(root.width * 0.45))
        height: Math.max(3, Math.round(root.height * 0.12))
        color: Theme.accent
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: tab.bottom
        anchors.topMargin: -1
        color: Theme.background
        border.color: Theme.normalBorder
        border.width: 1
    }
}
