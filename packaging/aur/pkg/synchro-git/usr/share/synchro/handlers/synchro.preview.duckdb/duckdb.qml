import QtQuick
import Synchro.Handler 1.0

// Interactive DuckDB peek. Same peekKey contract as sqlite.qml.
// Tables/samples via host.readDatabase(file, "duckdb", …) when duckdb is on PATH.
HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    function peekKey(key, modifiers) {
        return browser.peekKey(key, modifiers)
    }

    DatabasePeek {
        id: browser
        objectName: "duckdbPeek"
        anchors.fill: parent
        host: root.host
        file: root.file
        engine: "duckdb"
    }
}
