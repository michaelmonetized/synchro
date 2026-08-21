import QtQuick

// Drop target for listing background or a folder row. Parses the same
// mime as the OS clipboard (uri-list / gnome-copied-files / urls).
DropArea {
    id: surface

    property var fileOps: null
    property string destPath: ""
    property bool dropEnabled: true

    readonly property bool hot: enabled && containsDrag

    keys: [
        "text/uri-list",
        "text/plain",
        "x-special/gnome-copied-files",
        "application/x-synchro-drop"
    ]
    focus: false
    activeFocusOnTab: false
    enabled: surface.dropEnabled && surface.fileOps && surface.destPath.length > 0

    function pathsFrom(drop) {
        if (!surface.fileOps)
            return []
        var urls = []
        if (drop.hasUrls) {
            for (var i = 0; i < drop.urls.length; ++i)
                urls.push(drop.urls[i])
        }
        var uriList = ""
        var gnome = ""
        var formats = drop.formats || []
        if (formats.indexOf("text/uri-list") >= 0)
            uriList = drop.getDataAsString("text/uri-list")
        if (formats.indexOf("x-special/gnome-copied-files") >= 0)
            gnome = drop.getDataAsString("x-special/gnome-copied-files")
        return surface.fileOps.pathsFromDrop(urls, uriList, gnome)
    }

    function actionFrom(drop) {
        var formats = drop.formats || []
        var synchro = formats.indexOf("application/x-synchro-drop") >= 0
        if (synchro) {
            if (drop.proposedAction === Qt.CopyAction)
                return "copy"
            return "auto"
        }
        if (drop.proposedAction === Qt.MoveAction)
            return "move"
        return "copy"
    }

    onEntered: function (drag) {
        var ok = surface.enabled && surface.fileOps &&
                 surface.fileOps.canDropOn(surface.destPath)
        drag.accepted = ok
        if (ok)
            drag.action = drag.proposedAction
    }

    onDropped: function (drop) {
        if (!surface.fileOps || !surface.enabled) {
            drop.accepted = false
            return
        }
        var srcs = surface.pathsFrom(drop)
        if (srcs.length === 0 ||
                !surface.fileOps.canAcceptDrop(srcs, surface.destPath)) {
            drop.accepted = false
            return
        }
        surface.fileOps.dropOn(srcs, surface.destPath, surface.actionFrom(drop))
        drop.acceptProposedAction()
    }
}
