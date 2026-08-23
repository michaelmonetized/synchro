import QtQuick

Item {
    id: root

    property var fileModel: null
    property var filterProxy: null

    readonly property bool listing: root.fileModel && root.fileModel.listing
    readonly property int count: root.filterProxy ? root.filterProxy.count
                                 : (root.fileModel ? root.fileModel.count : 0)
    readonly property string filter: root.filterProxy && root.filterProxy.filter
                                     ? root.filterProxy.filter : ""
    readonly property string kindFilter: root.filterProxy &&
                                         root.filterProxy.kindFilter
                                         ? root.filterProxy.kindFilter : "all"
    readonly property int totalCount: root.fileModel ? root.fileModel.count : 0
    readonly property string text: {
        if (root.listing || !root.fileModel)
            return "Opening folder…"
        if (root.fileModel.isSql)
            return "0 query rows"
        if (root.kindFilter !== "all" && root.totalCount > 0) {
            var hidden = root.totalCount
            if (root.kindFilter === "files")
                return hidden + (hidden === 1 ? " folder" : " folders") +
                       " hidden — files only"
            return hidden + (hidden === 1 ? " file" : " files") +
                   " hidden — folders only"
        }
        if (root.filter.length > 0 || (root.fileModel && root.fileModel.isSearch))
            return "No matches"
        return "empty folder"
    }
    readonly property string iconName: root.listing
                                       ? "content-loading-symbolic"
                                       : (root.fileModel && root.fileModel.isSql
                                          ? "view-list-details-symbolic"
                                          : "folder-open-symbolic")

    objectName: "emptyListing"
    visible: root.count <= 0
    implicitWidth: Math.max(Theme.space(260), copy.implicitWidth)
    implicitHeight: Theme.space(104)

    Column {
        anchors.centerIn: parent
        spacing: Theme.spaceLG

        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.space(32)
            height: Theme.space(32)
            iconSize: Theme.space(32)
            name: root.iconName
            fallback: root.listing ? "↻" : "◇"
            opacity: 0.72
        }

        Text {
            id: copy
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.text
            color: Theme.darkForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
