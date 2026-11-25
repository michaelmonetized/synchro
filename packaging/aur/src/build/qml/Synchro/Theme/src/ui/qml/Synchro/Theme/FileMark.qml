import QtQuick

// Generic file glyph for unknown / unread types. Omarchy colors.
Item {
    id: root

    property string suffix: ""

    readonly property int fold: Math.max(4, Math.round(Math.min(width, height) * 0.22))
    readonly property int inset: Math.max(1, Math.round(Math.min(width, height) * 0.06))

    // Page
    Canvas {
        id: page
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var x = root.inset
            var y = root.inset
            var w = width - root.inset * 2
            var h = height - root.inset * 2
            var f = Math.min(root.fold, Math.min(w, h) * 0.4)
            ctx.fillStyle = Theme.background
            ctx.strokeStyle = Theme.normalBorder
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(x, y)
            ctx.lineTo(x + w - f, y)
            ctx.lineTo(x + w, y + f)
            ctx.lineTo(x + w, y + h)
            ctx.lineTo(x, y + h)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()
            ctx.fillStyle = Theme.hoverFill
            ctx.beginPath()
            ctx.moveTo(x + w - f, y)
            ctx.lineTo(x + w, y + f)
            ctx.lineTo(x + w - f, y + f)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
    }

    readonly property color _watch: Theme.background
    on_WatchChanged: page.requestPaint()

    Rectangle {
        visible: root.suffix.length > 0 && root.height >= 22
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: root.inset + 2
        anchors.bottomMargin: root.inset + 2
        width: Math.min(root.width - root.inset * 2,
                        suffixText.implicitWidth + Theme.spaceSM * 2)
        height: suffixText.implicitHeight + Theme.spaceXS * 2
        color: Theme.alpha(Theme.darkBackground, 0.88)
        radius: Theme.radius

        Text {
            id: suffixText
            anchors.centerIn: parent
            text: root.suffix
            color: Theme.muted
            font.family: Theme.monoFontFamily
            font.pixelSize: Math.max(7, Math.round(root.height * 0.13))
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }
}
