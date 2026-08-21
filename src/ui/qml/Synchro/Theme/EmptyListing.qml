import QtQuick

// Centered hint when a listing is loading or has nothing to show.
Text {
    id: root

    property var fileModel: null
    property var filterProxy: null

    readonly property bool listing: root.fileModel && root.fileModel.listing
    readonly property int count: root.filterProxy ? root.filterProxy.count
                                 : (root.fileModel ? root.fileModel.count : 0)
    readonly property string filter: {
        if (root.filterProxy && root.filterProxy.filter)
            return root.filterProxy.filter
        return ""
    }
    readonly property string kindFilter: root.filterProxy &&
                                         root.filterProxy.kindFilter
                                         ? root.filterProxy.kindFilter : "all"
    readonly property int totalCount: root.fileModel ? root.fileModel.count : 0

    objectName: "emptyListing"
    visible: root.count <= 0
    text: {
        if (root.listing || !root.fileModel)
            return "Opening folder…"
        if (root.kindFilter !== "all" && root.totalCount > 0) {
            var hidden = root.totalCount
            if (root.kindFilter === "files")
                return hidden + (hidden === 1 ? " folder" : " folders")
                        + " hidden — files only"
            return hidden + (hidden === 1 ? " file" : " files")
                    + " hidden — folders only"
        }
        if (root.filter.length > 0 || (root.fileModel && root.fileModel.isSearch))
            return "no matches"
        return "empty folder"
    }
    color: Theme.muted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    horizontalAlignment: Text.AlignHCenter
}
