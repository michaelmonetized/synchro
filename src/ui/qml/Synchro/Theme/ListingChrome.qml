import QtQuick

// Slot for location/folder decorate QML (entryPoints.row / thumb).
// If no handler is mounted, paint a first-party used/total bar.
Item {
    id: root

    property var host: null
    property string mode: "row"
    property url file
    property string name: ""
    property string path: ""
    property string detail: ""
    property real used: -1
    property real total: -1
    property int percent: -1

    readonly property url chromeUrl: {
        if (!root.host)
            return ""
        if (root.mode === "thumb")
            return root.host.listingThumbUrl ? root.host.listingThumbUrl : ""
        return root.host.listingRowUrl ? root.host.listingRowUrl : ""
    }
    readonly property bool hasBar: root.percent >= 0 && root.total > 0
    readonly property bool hasChrome: plug.status === Loader.Ready && plug.item

    implicitWidth: root.mode === "thumb" ? parent.width : Theme.space(200)
    implicitHeight: root.mode === "thumb" ? Theme.space(5)
                                          : Math.max(Theme.fontBody, Theme.space(16))
    visible: root.hasChrome || root.hasBar ||
             (root.mode === "row" && root.detail.length > 0)

    Loader {
        id: plug
        anchors.fill: parent
        active: root.chromeUrl.toString().length > 0
        source: root.chromeUrl
        visible: status === Loader.Ready
        onLoaded: root.apply(item)
    }

    function apply(item) {
        if (!item)
            return
        if (item.file !== undefined)
            item.file = root.file
        if (item.host !== undefined)
            item.host = root.host
        if (item.name !== undefined)
            item.name = root.name
        if (item.path !== undefined)
            item.path = root.path
        if (item.detail !== undefined)
            item.detail = root.detail
        if (item.used !== undefined)
            item.used = root.used
        if (item.total !== undefined)
            item.total = root.total
        if (item.percent !== undefined)
            item.percent = root.percent
    }

    onFileChanged: if (plug.item) root.apply(plug.item)
    onHostChanged: if (plug.item) root.apply(plug.item)
    onNameChanged: if (plug.item) root.apply(plug.item)
    onPathChanged: if (plug.item) root.apply(plug.item)
    onDetailChanged: if (plug.item) root.apply(plug.item)
    onUsedChanged: if (plug.item) root.apply(plug.item)
    onTotalChanged: if (plug.item) root.apply(plug.item)
    onPercentChanged: if (plug.item) root.apply(plug.item)

    Row {
        id: fallback
        visible: !root.hasChrome && (root.hasBar ||
                                     (root.mode === "row" && root.detail.length > 0))
        anchors.fill: parent
        spacing: Theme.space(8)
        layoutDirection: Qt.RightToLeft

        Text {
            visible: root.mode === "row" && root.detail.length > 0
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.width * 0.55)
            text: root.detail
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideLeft
            horizontalAlignment: Text.AlignRight
        }

        Item {
            visible: root.hasBar
            width: root.mode === "thumb" ? parent.width : Theme.space(72)
            height: root.mode === "thumb" ? Theme.space(4) : Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                color: Theme.normalBorder
                opacity: 0.45
                radius: 1
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.round(parent.width *
                                  Math.max(0, Math.min(100, root.percent)) / 100)
                color: root.percent >= 90 ? Theme.urgent : Theme.accent
                radius: 1
            }
        }
    }
}
