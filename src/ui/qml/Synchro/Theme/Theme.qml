pragma Singleton
import QtQuick

QtObject {
    id: root

    readonly property ThemeBridge impl: ThemeBridge {}

    readonly property color foreground: impl.foreground
    readonly property color background: impl.background
    readonly property color opaqueBackground: Qt.rgba(
        impl.background.r, impl.background.g, impl.background.b, 1)
    readonly property color accent: impl.accent
    readonly property color urgent: impl.urgent
    readonly property color muted: impl.muted
    readonly property color selectedFill: impl.selectedFill
    readonly property color hoverFill: impl.hoverFill
    readonly property color normalBorder: impl.normalBorder
    readonly property color findMatchFill: Qt.rgba(
        impl.accent.r, impl.accent.g, impl.accent.b, 0.28)
    readonly property color findCurrentFill: Qt.rgba(
        impl.accent.r, impl.accent.g, impl.accent.b, 0.55)
    readonly property color findBarFill: Qt.rgba(
        impl.foreground.r, impl.foreground.g, impl.foreground.b, 0.05)
    readonly property color findFieldFill: Qt.rgba(
        impl.foreground.r, impl.foreground.g, impl.foreground.b, 0.08)
    readonly property int radius: impl.radius
    readonly property int gapsOut: impl.gapsOut
    readonly property string fontFamily: impl.fontFamily
    readonly property int fontBody: impl.fontBody
    readonly property int fontBaseSize: impl.fontBaseSize

    readonly property real fontScale: Math.max(1 / 12, impl.fontBaseSize / 12)
    readonly property real effectiveSpacingScale: impl.spacingScale * (impl.spacingScaleWithFont ? fontScale : 1)

    function space(px) {
        var n = Number(px)
        if (!isFinite(n) || n <= 0)
            return 0
        n = n * root.effectiveSpacingScale
        if (n <= 0)
            return 0
        return Math.max(1, Math.round(n))
    }
}
