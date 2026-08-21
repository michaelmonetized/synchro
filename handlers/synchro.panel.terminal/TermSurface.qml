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
    // A cd we sent but the shell has not executed yet. While it is in
    // flight the poll must not treat the stale cwd as user intent, or the
    // two sync directions ping-pong forever (worse with slow prompts).
    property string pendingShellCwd: ""
    property double pendingSince: 0
    property bool shellDead: false
    property bool started: false
    property string schemeName: "Linux"
    property color termBg: "#000000"

    function refreshScheme() {
        if (!surface.host)
            return
        var s = surface.host.terminalColorScheme()
        if (s.length)
            surface.schemeName = s
        var bg = surface.host.terminalBackground()
        if (bg.length)
            surface.termBg = bg
    }

    // The dock injects host/fileModel after creation; the shell must not
    // spawn until they exist (else it launches as the fallback shell in the
    // wrong directory — or not at all when chdir fails).
    function maybeStart() {
        if (surface.started || !surface.host || !surface.fileModel)
            return
        surface.started = true
        surface.refreshScheme()
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
        surface.pendingShellCwd = p
        surface.pendingSince = Date.now()
        session.sendText(" cd '" + p.replace(/'/g, "'\\''") + "'\n")
    }

    Rectangle {
        anchors.fill: parent
        color: surface.termBg
    }

    QMLTermWidget {
        id: term
        anchors.fill: parent
        anchors.margins: Theme.space(8)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody + 1
        colorScheme: surface.schemeName
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

    // live theme switches re-theme the shell
    Connections {
        target: Theme.impl
        function onThemeChanged() { surface.refreshScheme() }
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
            if (!cwd)
                return
            if (surface.pendingShellCwd.length) {
                if (cwd === surface.pendingShellCwd) {
                    surface.pendingShellCwd = ""
                    surface.lastSeenCwd = cwd
                    return
                }
                if (Date.now() - surface.pendingSince < 4000)
                    return // our cd is still in flight; cwd is stale
                surface.pendingShellCwd = "" // cd failed or was overridden
            }
            if (cwd === surface.lastSeenCwd)
                return
            surface.lastSeenCwd = cwd
            var browsing = surface.browsePath()
            if (cwd !== browsing && cwd !== surface.lastSyncedPath)
                surface.navStack.navigate(cwd)
        }
    }
}
