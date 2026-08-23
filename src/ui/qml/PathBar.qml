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

    readonly property int crumbH: Theme.controlHeight + Theme.spaceMD
    readonly property int tabH: Theme.controlHeight
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

    function iconForLocation(id) {
        if (id.indexOf("home") >= 0) return "user-home-symbolic"
        if (id.indexOf("recent") >= 0) return "document-open-recent-symbolic"
        if (id.indexOf("trash") >= 0) return "user-trash-symbolic"
        if (id.indexOf("volumes") >= 0) return "drive-harddisk-symbolic"
        if (id.indexOf("volume:") === 0) return "drive-harddisk-symbolic"
        if (id.indexOf("pin:") === 0) return "emblem-favorite-symbolic"
        if (id.indexOf("sql-bookmark:") === 0) return "text-x-script-symbolic"
        return "folder-symbolic"
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.darkBackground
        z: -2
    }

    component LocTab: Item {
        id: tab
        property string tabId: ""
        property string label: ""
        property bool current: false
        property bool titleTab: false
        property bool removable: false
        property var flick: null
        signal activated()
        signal removeRequested()

        objectName: tab.tabId.length ? tab.tabId : "locationChip"
        width: visible
               ? Math.min(tabContent.implicitWidth + Theme.controlPaddingX * 2,
                          Theme.space(176))
               : 0
        height: parent ? parent.height : root.tabH

        onCurrentChanged: if (current && tab.flick)
            Qt.callLater(function () { root.ensureTabVisible(tab, tab.flick) })
        Component.onCompleted: if (tab.current && tab.flick)
            Qt.callLater(function () { root.ensureTabVisible(tab, tab.flick) })

        Rectangle {
            anchors.fill: parent
            color: tab.current ? Theme.selectedFill
                   : (tabHover.hovered ? Theme.hoverFill : Theme.normalFill)
            border.color: tab.current ? Theme.selectedBorder : Theme.normalBorder
            border.width: tab.current ? Math.max(1, Theme.selectedBorderWidth)
                                      : Theme.normalBorderWidth
            radius: Theme.radius
        }

        Row {
            id: tabContent
            anchors.centerIn: parent
            spacing: Theme.spaceSM

            AppIcon {
                width: Theme.fontIcon
                height: Theme.fontIcon
                iconSize: Theme.fontIcon
                name: root.iconForLocation(tab.tabId)
                fallback: tab.titleTab ? "▥" : "◇"
            }

            Text {
                id: tabLabel
                anchors.verticalCenter: parent.verticalCenter
                text: tab.label
                color: tab.current || tabHover.hovered ? Theme.brightForeground
                                                       : Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBodySmall
                font.bold: tab.current
                elide: Text.ElideMiddle
            }

            Text {
                id: removeGlyph
                visible: tab.removable
                opacity: tabHover.hovered ? 1 : 0
                text: "×"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                z: 2

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Theme.spaceSM
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tab.removeRequested()
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: Theme.accent
            visible: tab.current
        }

        HoverHandler {
            id: tabHover
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: function (mouse) {
                if (tab.removable && mouse.button === Qt.MiddleButton)
                    tab.removeRequested()
                else
                    tab.activated()
            }
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
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

        Row {
            id: brand
            anchors.left: parent.left
            anchors.leftMargin: Theme.spaceLG
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spaceSM

            ChromeButton {
                width: Theme.controlHeight
                height: Theme.controlHeight
                compact: true
                iconName: "go-previous-symbolic"
                fallbackGlyph: "‹"
                enabled: root.navStack && root.navStack.canGoBack
                toolTip: "Back  ·  Alt+Left"
                onTriggered: if (root.navStack) root.navStack.goBack()
            }

            ChromeButton {
                width: Theme.controlHeight
                height: Theme.controlHeight
                compact: true
                iconName: "go-next-symbolic"
                fallbackGlyph: "›"
                enabled: root.navStack && root.navStack.canGoForward
                toolTip: "Forward  ·  Alt+Right"
                onTriggered: if (root.navStack) root.navStack.goForward()
            }

            ChromeButton {
                width: Theme.controlHeight
                height: Theme.controlHeight
                compact: true
                iconName: "go-up-symbolic"
                fallbackGlyph: "↑"
                enabled: !!root.navStack
                toolTip: "Parent folder  ·  Alt+Up"
                onTriggered: if (root.navStack) root.navStack.goUp()
            }
        }

        Flickable {
            id: crumbFlick
            anchors.left: brand.right
            anchors.leftMargin: Theme.spaceLG
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
                            text: "  ›  "
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }

                        Item {
                            id: crumbButton
                            width: Math.min(crumbLabel.implicitWidth + Theme.controlPaddingX * 2,
                                            Theme.space(240))
                            height: crumbRow.height - Theme.spaceSM * 2
                            anchors.verticalCenter: parent.verticalCenter

                            Rectangle {
                                anchors.fill: parent
                                color: crumbHover.hovered ? Theme.hoverFill : Theme.normalFill
                                border.color: crumbHover.hovered ? Theme.hoverBorder
                                                                      : Theme.normalBorder
                                border.width: crumbHover.hovered ? Theme.hoverBorderWidth
                                                                : Theme.normalBorderWidth
                                radius: Theme.radius
                            }

                            Text {
                            id: crumbLabel
                            anchors.fill: parent
                            anchors.leftMargin: Theme.controlPaddingX
                            anchors.rightMargin: Theme.controlPaddingX
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideMiddle
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
                                                          : Theme.brightForeground
                            }
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBodySmall
                            }

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
                spacing: Theme.spaceSM

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
            anchors.leftMargin: Theme.spaceLG
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
                spacing: Theme.spaceSM

                Repeater {
                    model: root.placeChips

                    LocTab {
                        required property var modelData
                        tabId: modelData && modelData.id ? modelData.id : ""
                        label: modelData && modelData.label ? modelData.label : ""
                        current: !!(modelData && modelData.active)
                        removable: !!(modelData && modelData.runtime === "sql")
                        flick: tabFlick
                        onActivated: if (root.locationChips && tabId)
                            root.locationChips.activate(tabId)
                        onRemoveRequested: if (root.locationChips && tabId)
                            root.locationChips.removeSqlBookmark(tabId)
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
