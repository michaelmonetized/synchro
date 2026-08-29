import QtQuick
import Synchro.Theme

// A quiet scrollbar with a generous invisible hit target. Mouse-wheel notches
// move decisively; pixel-precise touchpad input remains owned by Flickable.
Item {
    id: chrome

    property Flickable flick: parent
    property int itemCount: -1
    property real wheelStep: 0

    readonly property real minimumContentY: {
        const f = chrome.flick
        return f ? f.originY - f.topMargin : 0
    }
    readonly property real maximumContentY: {
        const f = chrome.flick
        if (!f)
            return 0
        return Math.max(chrome.minimumContentY,
                        f.originY + f.contentHeight - f.height + f.bottomMargin)
    }
    readonly property real scrollRange: Math.max(
                                                   0,
                                                   chrome.maximumContentY -
                                                   chrome.minimumContentY)
    readonly property bool scrollable: !!chrome.flick &&
                                       chrome.scrollRange > 1
    readonly property real position: {
        const f = chrome.flick
        if (!f || !chrome.scrollable)
            return 0
        return Math.max(0, Math.min(
                            1, (f.contentY - chrome.minimumContentY) /
                               chrome.scrollRange))
    }

    anchors.fill: parent
    z: 30
    enabled: true

    function setContentY(value) {
        const f = chrome.flick
        if (!f || !chrome.scrollable)
            return
        f.contentY = Math.max(chrome.minimumContentY,
                              Math.min(chrome.maximumContentY, value))
    }

    function scrollBy(delta) {
        if (!chrome.flick)
            return
        chrome.setContentY(chrome.flick.contentY + delta)
    }

    function effectiveWheelStep() {
        if (chrome.wheelStep > 0)
            return chrome.wheelStep
        const f = chrome.flick
        return f ? Math.max(Theme.space(72), Math.min(Theme.space(160),
                                                     f.height * 0.18))
                 : Theme.space(96)
    }

    function positionLabel() {
        if (chrome.itemCount > 0) {
            const row = Math.max(1, Math.min(
                                     chrome.itemCount,
                                     Math.round(chrome.position *
                                                (chrome.itemCount - 1)) + 1))
            return row + " / " + chrome.itemCount
        }
        return Math.round(chrome.position * 100) + "%"
    }

    // Shift/Ctrl+wheel retains the existing page-scroll affordance.
    WheelHandler {
        acceptedModifiers: Qt.ShiftModifier | Qt.ControlModifier
        onWheel: function (event) {
            const f = chrome.flick
            if (!f)
                return
            if (!chrome.scrollable) {
                event.accepted = false
                return
            }
            const page = Math.max(48, f.height * 0.9)
            let dy = 0
            if (event.pixelDelta.y !== 0)
                dy = event.pixelDelta.y * 3
            else
                dy = (event.angleDelta.y / 120.0) * page
            chrome.scrollBy(-dy)
            event.accepted = true
        }
    }

    // Intercept only discrete mouse-wheel notches. Touchpads generally report
    // pixelDelta and keep Qt's native kinetic, one-to-one scrolling.
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse
        acceptedModifiers: Qt.NoModifier
        onWheel: function (event) {
            if (!chrome.scrollable || event.pixelDelta.y !== 0 ||
                    event.angleDelta.y === 0) {
                event.accepted = false
                return
            }
            const ticks = event.angleDelta.y / 120.0
            chrome.scrollBy(-ticks * chrome.effectiveWheelStep())
            event.accepted = true
        }
    }

    Item {
        id: scrollTrack
        objectName: "scrollTrack"
        visible: chrome.scrollable
        width: Theme.space(16)
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        readonly property real thumbHeight: {
            const f = chrome.flick
            if (!f || f.contentHeight <= 0)
                return height
            const total = f.contentHeight + f.topMargin + f.bottomMargin
            return Math.max(Theme.space(18),
                            Math.min(height, height * f.height /
                                             Math.max(1, total)))
        }
        readonly property real thumbTravel: Math.max(0, height - thumbHeight)
        property bool dragging: false
        property real dragOffset: 0

        function moveThumbTo(pointerY) {
            if (!chrome.scrollable || scrollTrack.thumbTravel <= 0)
                return
            const thumbTop = Math.max(
                               0, Math.min(scrollTrack.thumbTravel,
                                           pointerY - scrollTrack.dragOffset))
            chrome.setContentY(chrome.minimumContentY +
                               thumbTop / scrollTrack.thumbTravel *
                               chrome.scrollRange)
        }

        Rectangle {
            anchors.fill: parent
            color: trackArea.containsMouse || scrollTrack.dragging
                   ? Theme.hoverFill : "transparent"
            opacity: 0.42
        }

        Rectangle {
            id: scrollThumb
            objectName: "scrollThumb"
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(3)
            y: chrome.position * scrollTrack.thumbTravel
            width: trackArea.containsMouse || scrollTrack.dragging
                   ? Theme.space(8) : Theme.space(3)
            height: scrollTrack.thumbHeight
            radius: width / 2
            color: scrollTrack.dragging ? Theme.accent : Theme.muted
            opacity: scrollTrack.dragging ? 0.95
                     : (trackArea.containsMouse ||
                        (chrome.flick && chrome.flick.moving)
                        ? 0.72 : 0.38)

            Behavior on width {
                NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
            }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }

        Rectangle {
            visible: scrollTrack.dragging
            anchors.right: parent.left
            anchors.rightMargin: Theme.spaceSM
            y: Math.max(0, Math.min(parent.height - height,
                                    scrollThumb.y + scrollThumb.height / 2 -
                                    height / 2))
            width: positionText.implicitWidth + Theme.space(12)
            height: positionText.implicitHeight + Theme.space(6)
            radius: Theme.radius
            color: Theme.background
            border.color: Theme.accent
            border.width: 1

            Text {
                id: positionText
                anchors.centerIn: parent
                text: chrome.positionLabel()
                color: Theme.brightForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontCaption
                font.bold: true
            }
        }

        MouseArea {
            id: trackArea
            objectName: "scrollTrackHitTarget"
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeVerCursor
            acceptedButtons: Qt.LeftButton

            onPressed: function (mouse) {
                const onThumb = mouse.y >= scrollThumb.y &&
                                mouse.y <= scrollThumb.y + scrollThumb.height
                scrollTrack.dragOffset = onThumb
                                         ? mouse.y - scrollThumb.y
                                         : scrollThumb.height / 2
                scrollTrack.dragging = true
                scrollTrack.moveThumbTo(mouse.y)
            }
            onPositionChanged: function (mouse) {
                if (pressed)
                    scrollTrack.moveThumbTo(mouse.y)
            }
            onReleased: scrollTrack.dragging = false
            onCanceled: scrollTrack.dragging = false
        }
    }
}
