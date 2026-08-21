import QtQuick
import Synchro.Theme 1.0

Item {
    id: root

    property var host: null
    property url file
    property alias flick: flick
    property bool findOpen: false
    property string findQuery: ""
    property var findHits: []
    property int findIndex: -1
    property int viewStart: 0
    property var preview: ({})
    readonly property int findCount: findHits ? findHits.length : 0
    readonly property bool findActive: findOpen && findQuery.length > 0

    function reload() {
        if (!root.host || !root.file)
            return
        root.preview = root.host.readPreview(root.file, 65536, 0)
        root.viewStart = root.preview && root.preview.startByte
                         ? root.preview.startByte : 0
    }

    function resetFind() {
        root.findOpen = false
        root.findQuery = ""
        root.findHits = []
        root.findIndex = -1
    }

    function maybeDeeplink() {
        if (!root.host || !root.host.peekFindQuery)
            return
        var q = root.host.peekFindQuery
        if (!q || !q.length)
            return
        root.findOpen = true
        root.findQuery = q
        root.runFind()
    }

    function openFind() {
        root.findOpen = true
        if (root.host && root.host.peekFindQuery && !root.findQuery.length)
            root.findQuery = root.host.peekFindQuery
        Qt.callLater(function () {
            if (findField)
                findField.forceActiveFocus()
        })
        return true
    }

    function closeFind() {
        if (!root.findOpen && root.findCount === 0)
            return false
        root.resetFind()
        root.reload()
        return true
    }

    function runFind() {
        if (!root.host || !root.file || !root.findQuery.length) {
            root.findHits = []
            root.findIndex = -1
            root.reload()
            return
        }
        root.findHits = root.host.findInFile(root.file, root.findQuery, 200)
        if (!root.findHits || !root.findHits.length) {
            root.findIndex = -1
            return
        }
        root.jumpTo(0)
    }

    function jumpTo(i) {
        if (!root.findHits || !root.findHits.length)
            return
        var n = root.findHits.length
        root.findIndex = ((i % n) + n) % n
        root.ensureHitVisible(root.findHits[root.findIndex])
        Qt.callLater(root.scrollToCurrent)
    }

    function ensureHitVisible(hit) {
        if (!root.host || !hit)
            return
        var win = 65536
        var start = root.viewStart
        var end = start + (root.preview && root.preview.byteLength
                           ? root.preview.byteLength : 0)
        if (hit.offset >= start && hit.offset < end && root.preview &&
                root.preview.ok)
            return
        var want = Math.max(0, hit.offset - Math.floor(win / 4))
        root.preview = root.host.readPreview(root.file, win, want)
        root.viewStart = root.preview && root.preview.startByte !== undefined
                         ? root.preview.startByte : want
    }

    function hitsBeforeWindow() {
        var n = 0
        if (!root.findHits)
            return 0
        for (var i = 0; i < root.findHits.length; ++i) {
            if (root.findHits[i].offset < root.viewStart)
                n++
            else
                break
        }
        return n
    }

    function escHtml(s) {
        return String(s).replace(/\r/g, "")
                .replace(/&/g, "&amp;").replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
    }

    function decorate(text, needle, currentLocal) {
        if (root.host && root.host.markFindHits)
            return root.host.markFindHits(
                        root.preview && root.preview.html
                        ? root.preview.html : "",
                        text, needle, currentLocal,
                        Theme.findMatchFill, Theme.findCurrentFill)
        if (!text || !needle)
            return "<pre style=\"margin:0;\">" + root.escHtml(text || "") +
                   "</pre>"
        var sensitive = /[A-Z]/.test(needle)
        var src = sensitive ? text : text.toLowerCase()
        var pat = sensitive ? needle : needle.toLowerCase()
        var out = "<pre style=\"margin:0;\">"
        var from = 0
        var n = 0
        var match = String(Theme.findMatchFill)
        var cur = String(Theme.findCurrentFill)
        while (true) {
            var i = src.indexOf(pat, from)
            if (i < 0) {
                out += root.escHtml(text.slice(from))
                break
            }
            out += root.escHtml(text.slice(from, i))
            var chunk = text.slice(i, i + needle.length)
            var bg = (n === currentLocal) ? cur : match
            out += "<span style=\"background-color:" + bg + ";\">" +
                    root.escHtml(chunk) + "</span>"
            from = i + needle.length
            n++
        }
        return out + "</pre>"
    }

    function bodyText() {
        if (!root.preview || root.preview.ok === undefined)
            return ""
        if (!root.preview.ok)
            return root.preview.error === "binary"
                   ? "(binary file)"
                   : (root.preview.error || "unreadable")
        var raw = root.preview.text || ""
        var marked = ""
        if (root.findActive && raw.length)
            marked = root.decorate(raw, root.findQuery,
                                   root.findIndex - root.hitsBeforeWindow())
        else if (root.preview.highlighted && root.preview.html)
            marked = root.preview.html
        else if (raw.length)
            marked = "<pre style=\"margin:0;\">" + root.escHtml(raw) + "</pre>"
        if (root.preview.truncatedTail) {
            if (marked.indexOf("</pre>") >= 0)
                marked = marked.replace(/<\/pre>\s*$/, "\n…</pre>")
            else
                marked += "\n…"
        }
        return marked
    }

    function scrollToCurrent() {
        if (!body || !flick || root.findIndex < 0 || !root.findHits.length)
            return
        var before = root.hitsBeforeWindow()
        var local = root.findIndex - before
        if (local < 0)
            return
        var text = root.preview && root.preview.text ? root.preview.text : ""
        var needle = root.findQuery
        if (!text.length || !needle.length)
            return
        var sensitive = /[A-Z]/.test(needle)
        var src = sensitive ? text : text.toLowerCase()
        var pat = sensitive ? needle : needle.toLowerCase()
        var from = 0
        var pos = -1
        for (var n = 0; n <= local; ++n) {
            pos = src.indexOf(pat, from)
            if (pos < 0)
                return
            from = pos + needle.length
        }
        if (typeof body.positionToRectangle !== "function")
            return
        var r = body.positionToRectangle(pos)
        var maxY = Math.max(0, flick.contentHeight - flick.height)
        flick.contentY = Math.max(0, Math.min(maxY, r.y - flick.height / 4))
    }

    function peekKey(key, modifiers) {
        var shift = modifiers & Qt.ShiftModifier
        if (key === Qt.Key_Slash && !shift)
            return root.openFind()
        if (key === Qt.Key_Escape)
            return root.closeFind()
        if (root.findCount > 0) {
            if (key === Qt.Key_N && shift) {
                root.jumpTo(root.findIndex - 1)
                return true
            }
            if (key === Qt.Key_N && !shift) {
                root.jumpTo(root.findIndex + 1)
                return true
            }
        }
        if (!flick)
            return false
        var page = Math.max(Theme.space(48), Math.round(flick.height * 0.85))
        var line = Math.max(Theme.space(24), Theme.fontBody * 4)
        var step = shift ? page : line
        if (key === Qt.Key_PageUp || key === Qt.Key_PageDown)
            step = page
        if (key === Qt.Key_W || key === Qt.Key_Up || key === Qt.Key_K ||
                key === Qt.Key_PageUp) {
            flick.contentY = Math.max(0, flick.contentY - step)
            return true
        }
        if (key === Qt.Key_S || key === Qt.Key_Down || key === Qt.Key_J ||
                key === Qt.Key_PageDown) {
            var maxY = Math.max(0, flick.contentHeight - flick.height)
            flick.contentY = Math.min(maxY, flick.contentY + step)
            return true
        }
        return false
    }

    onFileChanged: {
        root.resetFind()
        root.reload()
        root.maybeDeeplink()
    }
    Component.onCompleted: {
        root.reload()
        root.maybeDeeplink()
    }

    Column {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: findBar
            objectName: "textPeekFind"
            width: parent.width
            height: root.findOpen ? Theme.fontBody + Theme.space(18) : 0
            visible: root.findOpen
            color: Theme.findBarFill
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.normalBorder
                opacity: 0.6
            }

            Rectangle {
                id: findFieldBox
                anchors.left: parent.left
                anchors.right: findTools.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.space(8)
                anchors.rightMargin: Theme.space(8)
                height: Theme.fontBody + Theme.space(10)
                radius: Math.max(Theme.radius, Theme.space(4))
                color: Theme.findFieldFill
                border.width: findField.activeFocus ? 1 : 0
                border.color: Theme.accent

                Text {
                    id: findPlaceholder
                    anchors.fill: findField
                    text: "Find in file"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    verticalAlignment: Text.AlignVCenter
                    visible: !findField.text.length && !findField.activeFocus
                }

                TextInput {
                    id: findField
                    objectName: "textPeekFindField"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.space(8)
                    anchors.rightMargin: Theme.space(8)
                    color: Theme.foreground
                    selectedTextColor: Theme.background
                    selectionColor: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    selectByMouse: true
                    text: root.findQuery
                    onTextEdited: {
                        root.findQuery = text
                        root.runFind()
                    }
                    Keys.onReturnPressed: function (event) {
                        if (root.findCount > 0)
                            root.jumpTo(root.findIndex +
                                        (event.modifiers & Qt.ShiftModifier ? -1 : 1))
                        else
                            root.runFind()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function (event) {
                        if (root.findCount > 0)
                            root.jumpTo(root.findIndex +
                                        (event.modifiers & Qt.ShiftModifier ? -1 : 1))
                        else
                            root.runFind()
                        event.accepted = true
                    }
                    Keys.onEscapePressed: function (event) {
                        root.closeFind()
                        event.accepted = true
                    }
                }
            }

            Row {
                id: findTools
                anchors.right: parent.right
                anchors.rightMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space(6)

                Text {
                    id: findCount
                    objectName: "textPeekFindCount"
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        if (!root.findQuery.length)
                            return ""
                        if (!root.findCount)
                            return "No matches"
                        return (root.findIndex + 1) + " of " + root.findCount
                    }
                    color: root.findCount > 0 ? Theme.foreground : Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Rectangle {
                    width: Theme.fontBody + Theme.space(10)
                    height: Theme.fontBody + Theme.space(8)
                    radius: Math.max(Theme.radius, Theme.space(4))
                    color: prevHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: 1
                    border.color: Theme.normalBorder
                    opacity: root.findCount > 0 ? 1 : 0.35

                    Text {
                        anchors.centerIn: parent
                        text: "‹"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    HoverHandler { id: prevHover }
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.findCount > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.jumpTo(root.findIndex - 1)
                    }
                }

                Rectangle {
                    width: Theme.fontBody + Theme.space(10)
                    height: Theme.fontBody + Theme.space(8)
                    radius: Math.max(Theme.radius, Theme.space(4))
                    color: nextHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: 1
                    border.color: Theme.normalBorder
                    opacity: root.findCount > 0 ? 1 : 0.35

                    Text {
                        anchors.centerIn: parent
                        text: "›"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    HoverHandler { id: nextHover }
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.findCount > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.jumpTo(root.findIndex + 1)
                    }
                }
            }
        }

        Flickable {
            id: flick
            width: parent.width
            height: parent.height - findBar.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, body.implicitWidth)
            contentHeight: body.implicitHeight

            TextEdit {
                id: body
                objectName: "textPeekBody"
                readOnly: true
                selectByMouse: true
                activeFocusOnPress: false
                persistentSelection: true
                text: root.bodyText()
                textFormat: TextEdit.RichText
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: TextEdit.NoWrap
                padding: Theme.space(12)

                MouseArea {
                    anchors.fill: parent
                    enabled: root.findCount > 0
                    acceptedButtons: Qt.LeftButton
                    onClicked: root.jumpTo(root.findIndex + 1)
                }
            }
        }
    }
}
