pragma Singleton
import QtQuick

QtObject {
    id: root

    readonly property ThemeBridge impl: ThemeBridge {}

    readonly property color foreground: impl.foreground
    readonly property color background: impl.background
    readonly property color accent: impl.accent
    readonly property color urgent: impl.urgent
    readonly property color muted: impl.muted
    readonly property color selectedFill: impl.selectedFill
    readonly property color hoverFill: impl.hoverFill
    readonly property color normalBorder: impl.normalBorder
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
