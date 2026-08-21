import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var fileModel
    required property var navStack
    property var locationChips

    readonly property string path: fileModel ? fileModel.path : ""
    readonly property var segments: navStack ? navStack.segmentsFor(root.path) : []
    readonly property bool atHome: navStack && root.path === navStack.homePath
    readonly property var placeChips: root.locationChips ? root.locationChips.placeChips : []
    readonly property var diskChips: root.locationChips ? root.locationChips.diskChips : []
    readonly property var volumesChip: root.locationChips ? root.locationChips.volumesChip : ({})
    readonly property bool hasDisks: (root.diskChips && root.diskChips.length > 0) ||
                                     !!(root.volumesChip && root.volumesChip.id)

    readonly property int crumbH: Math.max(Theme.fontBody + Theme.space(8), 22)
    readonly property int tabH: Math.max(Theme.fontBody + Theme.space(6), 20)
    readonly property bool disksInline: {
        if (!root.hasDisks || diskRow.width <= 0 || crumbLine.width <= 0)
            return false
        var need = brand.implicitWidth + Theme.space(20) + crumbRow.width +
                   Theme.space(24) + diskRow.width
        return crumbLine.width >= need
    }

    implicitHeight: crumbLine.height +
                    (root.hasDisks && !root.disksInline ? diskStrip.height : 0) +
                    tabLine.height

    function scrollCrumbsToEnd() {
        crumbFlick.contentX = Math.max(0, crumbRow.width - crumbFlick.width)
    }

    function ensureTabVisible(item, flick) {
        if (!item || !flick || flick.width <= 0)
            return
        var pad = Theme.space(8)
        var left = item.x
        var right = item.x + item.width
        var viewL = flick.contentX
        var viewR = flick.contentX + flick.width
        if (right - left >= flick.width) {
            flick.contentX = Math.max(0, left)
            return
        }
        if (left < viewL)
            flick.contentX = Math.max(0, left - pad)
        else if (right > viewR)
            flick.contentX = Math.min(
                        Math.max(0, flick.contentWidth - flick.width),
                        right - flick.width + pad)
    }

    function clampFlick(flick) {
        if (!flick)
            return
        if (flick.contentWidth <= flick.width)
            flick.contentX = 0
        else
            flick.contentX = Math.min(
                        flick.contentX,
                        Math.max(0, flick.contentWidth - flick.width))
    }

    component LocTab: Item {
        id: tab
        property string tabId: ""
        property string label: ""
        property bool current: false
        property bool titleTab: false
        property var flick: null
        signal activated()

        objectName: tab.tabId.length ? tab.tabId : "locationChip"
        width: visible
               ? Math.min(tabLabel.implicitWidth + Theme.space(tab.titleTab ? 12 : 16),
                          Theme.space(148))
               : 0
        height: parent ? parent.height : root.tabH

        onCurrentChanged: if (current && tab.flick)
            Qt.callLater(function () { root.ensureTabVisible(tab, tab.flick) })
        Component.onCompleted: if (tab.current && tab.flick)
            Qt.callLater(function () { root.ensureTabVisible(tab, tab.flick) })

        Rectangle {
            anchors.fill: parent
            visible: !tab.titleTab
            color: tab.current ? Theme.selectedFill
                   : (tabHover.hovered ? Theme.hoverFill : "transparent")
        }

        Text {
            id: tabLabel
            anchors.fill: parent
            anchors.leftMargin: Theme.space(tab.titleTab ? 6 : 8)
            anchors.rightMargin: Theme.space(tab.titleTab ? 6 : 8)
            text: tab.label
            color: tab.current || tabHover.hovered ? Theme.foreground
                                                   : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: tab.titleTab ? 1 : 2
            color: Theme.accent
            visible: tab.current
        }

        HoverHandler {
            id: tabHover
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: tab.activated()
        }
    }

    Item {
        id: crumbLine
        objectName: "pathCrumbs"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.crumbH
        z: 1

        Text {
            id: brand
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            text: "synchro"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }

        Flickable {
            id: crumbFlick
            anchors.left: brand.right
            anchors.leftMargin: Theme.space(12)
            anchors.right: parent.right
            anchors.rightMargin: root.disksInline
                                 ? diskStrip.width + Theme.space(8)
                                 : Theme.space(8)
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            clip: true
            contentWidth: crumbRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width

            Row {
                id: crumbRow
                height: crumbFlick.height
                spacing: 0

                Repeater {
                    model: root.segments

                    Row {
                        id: crumb
                        height: crumbRow.height
                        spacing: 0

                        required property var modelData
                        required property int index

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: crumb.index > 0
                            text: " / "
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }

                        Text {
                            id: crumbLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                var label = crumb.modelData.label
                                if (label === "search" && root.fileModel &&
                                        root.fileModel.searchQuery) {
                                    var kind = root.fileModel.isContentSearch
                                               ? "content  " : "search  "
                                    var q = root.fileModel.searchQuery
                                    var sr = root.fileModel.searchRoot || ""
                                    if (sr.length)
                                        return kind + q + "  ·  " + sr
                                    return kind + q
                                }
                                return label
                            }
                            color: {
                                if (crumb.modelData.label === "search")
                                    return Theme.accent
                                return crumbHover.hovered ? Theme.accent
                                                          : Theme.foreground
                            }
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody

                            HoverHandler {
                                id: crumbHover
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.navStack)
                                        root.navStack.navigate(crumb.modelData.path)
                                }
                            }
                        }
                    }
                }
            }

            onContentWidthChanged: Qt.callLater(root.scrollCrumbsToEnd)
        }
    }

    Item {
        id: diskStrip
        objectName: "diskTabs"
        visible: root.hasDisks
        z: 2
        implicitWidth: diskRow.width
        height: root.disksInline ? root.crumbH : root.tabH

        Flickable {
            id: diskFlick
            objectName: "diskTabFlick"
            anchors.fill: parent
            anchors.leftMargin: Theme.space(4)
            anchors.rightMargin: Theme.space(4)
            clip: true
            contentWidth: diskRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width

            Row {
                id: diskRow
                height: diskFlick.height
                spacing: 0

                LocTab {
                    visible: !!(root.volumesChip && root.volumesChip.id)
                    tabId: root.volumesChip && root.volumesChip.id
                           ? root.volumesChip.id : ""
                    label: root.volumesChip && root.volumesChip.label
                           ? root.volumesChip.label : "volumes"
                    current: !!(root.volumesChip && root.volumesChip.active)
                    titleTab: true
                    flick: diskFlick
                    onActivated: if (root.locationChips && tabId)
                        root.locationChips.activate(tabId)
                }

                Repeater {
                    model: root.diskChips

                    LocTab {
                        required property var modelData
                        tabId: modelData && modelData.id ? modelData.id : ""
                        label: modelData && modelData.label ? modelData.label : ""
                        current: !!(modelData && modelData.active)
                        flick: diskFlick
                        onActivated: if (root.locationChips && tabId)
                            root.locationChips.activate(tabId)
                    }
                }
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function (event) {
                    if (diskFlick.contentWidth <= diskFlick.width)
                        return
                    var dx = event.pixelDelta.x
                    var dy = event.pixelDelta.y
                    if (dx === 0 && dy === 0) {
                        dx = event.angleDelta.x / 4
                        dy = event.angleDelta.y / 4
                    }
                    var delta = Math.abs(dx) > Math.abs(dy) ? dx : dy
                    diskFlick.contentX = Math.max(
                                0, Math.min(
                                       diskFlick.contentWidth - diskFlick.width,
                                       diskFlick.contentX - delta))
                    event.accepted = true
                }
            }

            onContentWidthChanged: Qt.callLater(function () {
                root.clampFlick(diskFlick)
            })
            onWidthChanged: Qt.callLater(function () {
                root.clampFlick(diskFlick)
            })
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.normalBorder
            visible: !root.disksInline
        }
    }

    Item {
        id: tabLine
        objectName: "locationTabs"
        anchors.top: crumbLine.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.tabH

        Flickable {
            id: tabFlick
            objectName: "locationTabFlick"
            anchors.fill: parent
            anchors.leftMargin: Theme.space(4)
            anchors.rightMargin: Theme.space(4)
            clip: true
            contentWidth: tabRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width

            Row {
                id: tabRow
                height: tabFlick.height
                spacing: 0

                Repeater {
                    model: root.placeChips

                    LocTab {
                        required property var modelData
                        tabId: modelData && modelData.id ? modelData.id : ""
                        label: modelData && modelData.label ? modelData.label : ""
                        current: !!(modelData && modelData.active)
                        flick: tabFlick
                        onActivated: if (root.locationChips && tabId)
                            root.locationChips.activate(tabId)
                    }
                }

                LocTab {
                    objectName: "homeChip"
                    visible: !root.locationChips
                    tabId: "homeChip"
                    label: "home"
                    current: root.atHome
                    onActivated: if (root.navStack)
                        root.navStack.goHome()
                }
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function (event) {
                    if (tabFlick.contentWidth <= tabFlick.width)
                        return
                    var dx = event.pixelDelta.x
                    var dy = event.pixelDelta.y
                    if (dx === 0 && dy === 0) {
                        dx = event.angleDelta.x / 4
                        dy = event.angleDelta.y / 4
                    }
                    var delta = Math.abs(dx) > Math.abs(dy) ? dx : dy
                    tabFlick.contentX = Math.max(
                                0, Math.min(
                                       tabFlick.contentWidth - tabFlick.width,
                                       tabFlick.contentX - delta))
                    event.accepted = true
                }
            }

            onContentWidthChanged: Qt.callLater(function () {
                root.clampFlick(tabFlick)
            })
            onWidthChanged: Qt.callLater(function () {
                root.clampFlick(tabFlick)
            })
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.normalBorder
        }
    }

    states: [
        State {
            name: "disksInline"
            when: root.hasDisks && root.disksInline
            PropertyChanges {
                target: diskStrip
                width: diskRow.width
            }
            AnchorChanges {
                target: diskStrip
                anchors.top: crumbLine.top
                anchors.bottom: crumbLine.bottom
                anchors.right: root.right
            }
            AnchorChanges {
                target: tabLine
                anchors.top: crumbLine.bottom
            }
        },
        State {
            name: "disksStacked"
            when: root.hasDisks && !root.disksInline
            AnchorChanges {
                target: diskStrip
                anchors.top: crumbLine.bottom
                anchors.left: root.left
                anchors.right: root.right
            }
            AnchorChanges {
                target: tabLine
                anchors.top: diskStrip.bottom
            }
        },
        State {
            name: "noDisks"
            when: !root.hasDisks
            AnchorChanges {
                target: tabLine
                anchors.top: crumbLine.bottom
            }
        }
    ]

    onSegmentsChanged: Qt.callLater(root.scrollCrumbsToEnd)
    onPlaceChipsChanged: Qt.callLater(function () { root.clampFlick(tabFlick) })
    onDiskChipsChanged: Qt.callLater(function () { root.clampFlick(diskFlick) })
}
