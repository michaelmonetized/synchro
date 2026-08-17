import QtQuick
import Synchro.Theme

// Thin overlay scrollbar + Shift/Ctrl+wheel page scroll.
Item {
    id: chrome

    property Flickable flick: parent

    anchors.fill: parent
    z: 30
    // Let clicks pass through to the list; WheelHandler still sees wheels.
    enabled: true

    WheelHandler {
        acceptedModifiers: Qt.ShiftModifier | Qt.ControlModifier
        onWheel: function (event) {
            const f = chrome.flick
            if (!f)
                return
            const range = Math.max(0, f.contentHeight - f.height)
            if (range <= 0) {
                event.accepted = false
                return
            }
            const page = Math.max(48, f.height * 0.9)
            let dy = 0
            if (event.pixelDelta.y !== 0)
                dy = event.pixelDelta.y * 3
            else
                dy = (event.angleDelta.y / 120.0) * page
            f.contentY = Math.max(0, Math.min(range, f.contentY - dy))
            event.accepted = true
        }
    }

    Rectangle {
        visible: chrome.flick && chrome.flick.contentHeight > chrome.flick.height + 1
        width: 3
        radius: 1.5
        anchors.right: parent.right
        anchors.rightMargin: 2
        color: Theme.muted
        opacity: 0.35
        height: {
            const f = chrome.flick
            if (!f || f.contentHeight <= 0)
                return 0
            return Math.max(Theme.space(12), f.height * f.height / f.contentHeight)
        }
        y: {
            const f = chrome.flick
            if (!f || f.contentHeight <= f.height)
                return 0
            const travel = f.height - height
            return f.contentY * travel / (f.contentHeight - f.height)
        }
    }
}
