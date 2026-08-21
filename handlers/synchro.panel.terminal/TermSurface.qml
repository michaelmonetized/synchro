import QtQuick
import QMLTermWidget
import Synchro.Theme

// The embedded shell. cd-follow both ways, Dolphin style:
//  - browser -> shell: only when the shell is idle (no foreground child),
//    with a leading space so the cd stays out of history.
//  - shell -> browser: poll the shell's /proc cwd while visible; navigate
//    when it moves. lastSynced/lastSeen guards stop the two directions from
//    ping-ponging.
Item {
    id: surface

    property var host: null
    property var fileModel: null
    property var navStack: null

    property string lastSyncedPath: ""
    property string lastSeenCwd: ""
    property bool shellDead: false
    property bool started: false

    // The dock injects host/fileModel after creation; the shell must not
    // spawn until they exist (else it launches as the fallback shell in the
    // wrong directory — or not at all when chdir fails).
    function maybeStart() {
        if (surface.started || !surface.host || !surface.fileModel)
            return
        surface.started = true
        session.shellProgram = surface.host.defaultShell()
        var p = surface.browsePath()
        if (p.length) {
            session.initialWorkingDirectory = p
            surface.lastSyncedPath = p
            surface.lastSeenCwd = p
        }
        session.startShellProgram()
        term.forceActiveFocus()
    }

    onHostChanged: maybeStart()
    onFileModelChanged: maybeStart()
    Component.onCompleted: maybeStart()

    function focusContent() {
        term.forceActiveFocus()
    }

    function browsePath() {
        var p = surface.fileModel ? surface.fileModel.path : ""
        return p && p.charAt(0) === "/" ? p : ""
    }

    function syncFromBrowser() {
        var p = surface.browsePath()
        if (!p || surface.shellDead)
            return
        if (p === surface.lastSeenCwd || p === surface.lastSyncedPath)
            return
        if (session.hasActiveProcess)
            return // a command is running; do not type into it
        surface.lastSyncedPath = p
        session.sendText(" cd '" + p.replace(/'/g, "'\\''") + "'\n")
    }

    QMLTermWidget {
        id: term
        anchors.fill: parent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody + 1
        colorScheme: "Linux"
        enableBold: true
        fullCursorHeight: true

        session: QMLTermSession {
            id: session
            shellProgram: "/bin/bash"
            onFinished: surface.shellDead = true
        }

    }

    // Focus on click without stealing the event from the terminal's own
    // mouse handling (selection, mouse-mode apps).
    MouseArea {
        anchors.fill: term
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.IBeamCursor
        onPressed: function (mouse) {
            term.forceActiveFocus()
            mouse.accepted = false
        }
    }

    QMLTermScrollbar {
        terminal: term
        width: 6
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: Theme.muted
            opacity: 0.4
        }
    }

    // shell exited: offer a restart
    Rectangle {
        anchors.fill: parent
        visible: surface.shellDead
        color: Theme.opaqueBackground

        Text {
            anchors.centerIn: parent
            text: "shell exited — click to restart"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                surface.shellDead = false
                session.startShellProgram()
                term.forceActiveFocus()
            }
        }
    }

    // browser -> shell
    Connections {
        target: surface.fileModel
        function onPathChanged() { surface.syncFromBrowser() }
    }

    // shell -> browser
    Timer {
        interval: 500
        repeat: true
        running: surface.visible && !surface.shellDead
        onTriggered: {
            if (!surface.host || !surface.navStack)
                return
            var pid = session.getShellPID()
            if (pid <= 0)
                return
            var cwd = surface.host.processCwd(pid)
            if (!cwd || cwd === surface.lastSeenCwd)
                return
            surface.lastSeenCwd = cwd
            var browsing = surface.browsePath()
            if (cwd !== browsing && cwd !== surface.lastSyncedPath)
                surface.navStack.navigate(cwd)
        }
    }
}
