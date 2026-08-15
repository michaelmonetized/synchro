import QtQuick

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

    Component.onCompleted: if (host && host.registerSurface)
        host.registerSurface(root)
}
