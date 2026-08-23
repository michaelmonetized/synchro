import QtQuick

// Placeholder folder surface. Folders use the raised Omarchy surface while
// file-content cards use the darker background; no decorative tab required.
Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Theme.lighterBackground
        border.color: Theme.normalBorder
        border.width: 1
    }
}
