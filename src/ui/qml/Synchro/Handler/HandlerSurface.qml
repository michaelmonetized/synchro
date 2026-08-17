import QtQuick
import Synchro.Theme 1.0

Item {
    id: root

    // --- injected ---
    property url file
    property var selection: []
    property var host              // HostApi (C++)
    property var manifest: ({})

    // --- optional, handler → host ---
    property string title: ""
    property bool busy: false
    signal requestClose()
    signal requestOpen(url file)   // ask host to run `open` on another file
    signal requestReveal(url file)

    // Peek interaction hook. KeyMachine keeps Esc / Space / Q / j / k.
    // When the user hops to the file with D, W/S call peekKey instead of
    // stepping the listing. Return true if the key was consumed.
    // peekFlickable: set this to the Flickable that should scroll.
    property bool peekInteractive: true
    property Item peekFlickable: null

    // Do-layer param pane. Enter on a params verb calls commit().
    // Return true if the action finished.
    function commit() {
        return false
    }

    // Keys while A/D has hopped onto the params pane. Default: scroll.
    function actionKey(key, modifiers) {
        return root.peekKey(key, modifiers)
    }

    function peekKey(key, modifiers) {
        if (!root.peekInteractive)
            return false
        var f = root.peekFlickable
        if (!f)
            f = root.findFlickable(root)
        if (!f)
            return false
        return root.scrollFlickable(f, key, modifiers)
    }

    function findFlickable(item) {
        if (!item)
            return null
        if (item !== root && item.contentY !== undefined &&
                item.contentHeight !== undefined &&
                item.flickableDirection !== undefined)
            return item
        var kids = item.children
        for (var i = 0; i < kids.length; ++i) {
            var hit = root.findFlickable(kids[i])
            if (hit)
                return hit
        }
        return null
    }

    function scrollFlickable(f, key, modifiers) {
        if (!f)
            return false
        var page = Math.max(Theme.space(48), Math.round(f.height * 0.85))
        var line = Math.max(Theme.space(24), Theme.fontBody * 4)
        var step = (modifiers & Qt.ShiftModifier) ? page : line
        if (key === Qt.Key_PageUp || key === Qt.Key_PageDown)
            step = page
        if (key === Qt.Key_W || key === Qt.Key_Up || key === Qt.Key_K ||
                key === Qt.Key_PageUp) {
            f.contentY = Math.max(0, f.contentY - step)
            return true
        }
        if (key === Qt.Key_S || key === Qt.Key_Down || key === Qt.Key_J ||
                key === Qt.Key_PageDown) {
            var maxY = Math.max(0, f.contentHeight - f.height)
            f.contentY = Math.min(maxY, f.contentY + step)
            return true
        }
        if (key === Qt.Key_H || key === Qt.Key_Left) {
            f.contentX = Math.max(0, f.contentX - step)
            return true
        }
        if (key === Qt.Key_L || key === Qt.Key_Right || key === Qt.Key_D) {
            var maxX = Math.max(0, f.contentWidth - f.width)
            f.contentX = Math.min(maxX, f.contentX + step)
            return true
        }
        if (key === Qt.Key_Home) {
            f.contentY = 0
            return true
        }
        if (key === Qt.Key_End) {
            f.contentY = Math.max(0, f.contentHeight - f.height)
            return true
        }
        return false
    }

    Component.onCompleted: if (host && host.registerSurface)
        host.registerSurface(root)
}
