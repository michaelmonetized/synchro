pragma ComponentBehavior: Bound

import QtQuick
import Synchro.Theme

Item {
    id: root

    property var metadata: ({})
    readonly property bool compact: height < Theme.space(150)
    readonly property var swatches: metadata && metadata.palette
                                    ? metadata.palette : []
    clip: true

    function humanSize(bytes) {
        var n = Number(bytes)
        if (!isFinite(n) || n < 0)
            return "—"
        if (n < 1024) return n.toLocaleString() + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024)
            return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
    }

    function humanTime(ms) {
        var value = Number(ms)
        if (!isFinite(value) || value <= 0)
            return "—"
        var date = new Date(value)
        return date.toLocaleString(Qt.locale(), "MMM d, yyyy · h:mm AP")
    }

    function kindText() {
        var m = root.metadata || ({})
        if (m.multiple)
            return "Mixed selection"
        var kind = "File"
        if (m.isDir)
            kind = "Folder"
        else if (m.typeLabel && m.typeLabel !== "application/octet-stream")
            kind = String(m.typeLabel)
        else if (m.mime)
            kind = String(m.mime)
        else if (m.extension)
            kind = String(m.extension).toUpperCase() + " file"
        var traits = [kind]
        if (m.hidden) traits.push("hidden")
        if (m.symlink) traits.push("link")
        return traits.join(" · ")
    }

    function selectionText() {
        var m = root.metadata || ({})
        var parts = []
        if (Number(m.files) > 0)
            parts.push(Number(m.files).toLocaleString() +
                       (Number(m.files) === 1 ? " file" : " files"))
        if (Number(m.folders) > 0)
            parts.push(Number(m.folders).toLocaleString() +
                       (Number(m.folders) === 1 ? " folder" : " folders"))
        return parts.join(" · ")
    }

    function imageText() {
        var m = root.metadata || ({})
        var width = Number(m.width)
        var height = Number(m.height)
        if (!(width > 0 && height > 0))
            return ""
        var text = width.toLocaleString() + " × " + height.toLocaleString()
        if (m.orientation)
            text += " · " + String(m.orientation)
        return text
    }

    function colorText() {
        var m = root.metadata || ({})
        var family = m.colorFamily ? String(m.colorFamily) : ""
        var dominant = m.dominantColor ? String(m.dominantColor).toUpperCase() : ""
        if (family && dominant)
            return family.charAt(0).toUpperCase() + family.slice(1) + " · " + dominant
        return family || dominant
    }

    function factRows() {
        var m = root.metadata || ({})
        var rows = []
        if (m.multiple) {
            rows.push({ label: "Selected", value: Number(m.count).toLocaleString() + " items" })
            var selection = root.selectionText()
            if (selection) rows.push({ label: "Contains", value: selection })
            if (Number(m.size) > 0) {
                var size = root.humanSize(m.size)
                if (m.sizeComplete === false) size += " sampled"
                rows.push({ label: "Size", value: size })
            }
            if (Number(m.mtime) > 0)
                rows.push({ label: "Newest", value: root.humanTime(m.mtime) })
            return rows
        }
        rows.push({ label: "Kind", value: root.kindText() })
        if (!m.isDir && m.size !== undefined)
            rows.push({ label: "Size", value: root.humanSize(m.size) })
        var image = root.imageText()
        if (image) rows.push({ label: "Frame", value: image })
        if (Number(m.mtime) > 0)
            rows.push({ label: "Modified", value: root.humanTime(m.mtime) })
        var color = root.colorText()
        if (color && !root.compact)
            rows.push({ label: "Color", value: color })
        if (m.permissions && !root.compact && !image)
            rows.push({ label: "Access", value: String(m.permissions) })
        return rows
    }

    function paletteTotal() {
        var total = 0
        for (var i = 0; i < root.swatches.length; ++i)
            total += Math.max(0, Number(root.swatches[i].weight) || 0)
        return total > 0 ? total : Math.max(1, root.swatches.length)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.darkerBackground, 0.22)
    }

    Row {
        id: dossierHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.space(10)
        anchors.leftMargin: Theme.space(12)
        anchors.rightMargin: Theme.space(12)
        spacing: Theme.space(6)

        Text {
            text: root.metadata && root.metadata.multiple ? "SELECTION" :
                  (Number(root.metadata.width) > 0 ? "IMAGE FACTS" : "FILE FACTS")
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            font.letterSpacing: 1
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.space(4)
            height: width
            radius: width / 2
            visible: !!(root.metadata && root.metadata.loading)
            color: Theme.accent
            opacity: 0.3 + 0.7 * Math.abs(0.5 - Theme.thumbnailPhase) * 2
        }
    }

    Column {
        id: factColumn
        anchors.top: dossierHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.space(8)
        anchors.leftMargin: Theme.space(12)
        anchors.rightMargin: Theme.space(12)
        spacing: root.compact ? Theme.space(4) : Theme.space(6)

        Repeater {
            model: root.factRows()
            delegate: Item {
                id: factRow
                required property var modelData
                width: factColumn.width
                height: Theme.space(root.compact ? 16 : 18)
                Text {
                    id: factLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.space(58)
                    text: factRow.modelData.label
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                }
                Text {
                    anchors.left: factLabel.right
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: factRow.modelData.value
                    color: Theme.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }
        }
    }

    Item {
        id: paletteGroup
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: pathText.top
        anchors.leftMargin: Theme.space(12)
        anchors.rightMargin: Theme.space(12)
        anchors.bottomMargin: Theme.space(7)
        height: root.swatches.length > 0 && !root.compact ? Theme.space(30) : 0
        visible: height > 0

        Row {
            id: paletteBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.space(12)
            spacing: Theme.space(2)
            clip: true
            Repeater {
                model: root.swatches
                delegate: Rectangle {
                    id: paletteSwatch
                    required property var modelData
                    height: paletteBar.height
                    width: Math.max(Theme.space(18),
                                    (paletteBar.width - paletteBar.spacing *
                                     Math.max(0, root.swatches.length - 1)) *
                                    (Math.max(0, Number(paletteSwatch.modelData.weight) || 0) /
                                     root.paletteTotal()))
                    color: paletteSwatch.modelData.color
                    radius: Theme.space(2)
                    border.color: Theme.alpha(Theme.foreground, 0.24)
                    border.width: 1
                }
            }
        }
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: {
                var values = []
                for (var i = 0; i < root.swatches.length; ++i)
                    values.push(String(root.swatches[i].color).toUpperCase())
                return values.join("  ·  ")
            }
            color: Theme.muted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    Text {
        id: pathText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space(12)
        anchors.rightMargin: Theme.space(12)
        anchors.bottomMargin: Theme.space(9)
        text: root.metadata && root.metadata.path ? String(root.metadata.path) : ""
        color: Theme.alpha(Theme.muted, 0.72)
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontCaption
        elide: Text.ElideMiddle
        visible: text.length > 0 && !root.compact
    }
}
