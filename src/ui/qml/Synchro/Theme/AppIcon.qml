import QtQuick

Item {
    id: root

    property string name: ""
    property string fallback: ""
    property int iconSize: Math.min(width, height)

    implicitWidth: iconSize
    implicitHeight: iconSize

    Image {
        id: image
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: root.name.length ? Theme.iconSource(root.name, root.iconSize) : ""
        sourceSize: Qt.size(root.iconSize, root.iconSize)
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        visible: status === Image.Ready && source.toString().length > 0
    }

    Text {
        anchors.centerIn: parent
        visible: !image.visible && root.fallback.length > 0
        text: root.fallback
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontIcon
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
