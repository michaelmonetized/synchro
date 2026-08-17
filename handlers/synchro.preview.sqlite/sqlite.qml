import QtQuick
import Synchro.Handler 1.0

// Interactive SQLite peek. Plugin writers: implement peekKey and return
// true if you consumed the key. A/D hop listing ↔ this pane; Esc/Space/Q
// stay with the host. Data: host.readDatabase(file, "sqlite", table, off, n)
HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    function peekKey(key, modifiers) {
        return browser.peekKey(key, modifiers)
    }

    DatabasePeek {
        id: browser
        objectName: "sqlitePeek"
        anchors.fill: parent
        host: root.host
        file: root.file
        engine: "sqlite"
    }
}
