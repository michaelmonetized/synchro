pragma Singleton
import QtQuick

QtObject {
    id: root

    readonly property ThemeBridge impl: ThemeBridge {}
    readonly property var tokens: impl.tokens

    function raw(key, fallback) {
        var value = root.tokens ? root.tokens[key] : undefined
        return value === undefined || value === null || value === ""
               ? fallback : value
    }

    function number(key, fallback) {
        var value = Number(root.raw(key, fallback))
        return isFinite(value) ? value : fallback
    }

    function colorFor(value, fallback) {
        if (value === undefined || value === null || value === "")
            return fallback
        var token = String(value).toLowerCase()
        if (token === "foreground" || token === "text") return root.foreground
        if (token === "background") return root.background
        if (token === "accent" || token.indexOf("active-border") >= 0) return root.accent
        if (token === "urgent") return root.urgent
        if (token === "muted") return root.muted
        if (token === "transparent") return "transparent"
        return value
    }

    function themedColor(key, fallback) {
        return root.colorFor(root.raw(key, fallback), fallback)
    }

    function alpha(color, amount) {
        return Qt.rgba(color.r, color.g, color.b,
                       Math.max(0, Math.min(1, Number(amount))))
    }

    readonly property color foreground: impl.foreground
    readonly property color background: impl.background
    readonly property color opaqueBackground: Qt.rgba(
        impl.background.r, impl.background.g, impl.background.b, 1)
    readonly property color accent: impl.accent
    readonly property color urgent: impl.urgent
    readonly property color muted: impl.muted
    readonly property color darkBackground: themedColor("colors.dark_background", background)
    readonly property color darkerBackground: themedColor("colors.darker_background", darkBackground)
    readonly property color lighterBackground: themedColor("colors.lighter_background", background)
    readonly property color darkForeground: themedColor("colors.dark_foreground", muted)
    readonly property color lightForeground: themedColor("colors.light_foreground", foreground)
    readonly property color brightForeground: themedColor("colors.bright_foreground", lightForeground)

    // The browser work surface is the one atmospheric layer in Synchro.
    // Defaults stay dark and legible while allowing compositor blur to read
    // through; themes can tune each stop without restyling application chrome.
    readonly property color canvasGlassTop: alpha(
        lighterBackground, number("canvas.glass-alpha-top", 0.91))
    readonly property color canvasGlassMiddle: alpha(
        background, number("canvas.glass-alpha-middle", 0.86))
    readonly property color canvasGlassBottom: alpha(
        darkerBackground, number("canvas.glass-alpha-bottom", 0.80))
    readonly property color canvasGlassEdge: alpha(
        accent, number("canvas.glass-edge-alpha", 0.16))

    readonly property color normalFill: alpha(
        colorFor(raw("controls.normal-color", foreground), foreground),
        number("controls.normal-fill-alpha", 0.04))
    readonly property color hoverFill: alpha(
        colorFor(raw("controls.hover-cursor-color", foreground), foreground),
        number("controls.hover-cursor-fill-alpha", 0.08))
    readonly property color focusFill: alpha(
        colorFor(raw("controls.focus-color", foreground), foreground),
        number("controls.focus-fill-alpha", 0.08))
    readonly property color selectedFill: alpha(
        colorFor(raw("controls.selected-color", foreground), foreground),
        number("controls.selected-fill-alpha", 0.18))
    readonly property color pressedFill: alpha(
        foreground, number("controls.pressed-fill-alpha", 0.22))
    readonly property color selectionFill: alpha(
        foreground, number("controls.selection-fill-alpha", 0.35))

    readonly property color normalBorder: alpha(
        colorFor(raw("controls.normal-border", foreground), foreground),
        number("controls.normal-border-alpha", 0.4))
    readonly property color hoverBorder: alpha(
        colorFor(raw("controls.hover-cursor-border", foreground), foreground),
        number("controls.hover-cursor-border-alpha", 0.25))
    readonly property color focusBorder: alpha(
        colorFor(raw("controls.focus-border", accent), accent),
        number("controls.focus-border-alpha", 0.65))
    readonly property color selectedBorder: alpha(
        colorFor(raw("controls.selected-border", accent), accent),
        number("controls.selected-border-alpha", 1.0))
    readonly property int normalBorderWidth: Math.max(0, Math.round(number("controls.normal-border-width", 1)))
    readonly property int hoverBorderWidth: Math.max(0, Math.round(number("controls.hover-cursor-border-width", 1)))
    readonly property int focusBorderWidth: Math.max(0, Math.round(number("controls.focus-border-width", 1)))
    readonly property int selectedBorderWidth: Math.max(0, Math.round(number("controls.selected-border-width", 0)))

    readonly property color popupBackground: themedColor("popups.background", background)
    readonly property color popupText: themedColor("popups.text", foreground)
    readonly property color popupBorder: themedColor("popups.border", accent)
    readonly property color tooltipBackground: themedColor("tooltip.background", background)
    readonly property color tooltipText: themedColor("tooltip.text", foreground)
    readonly property color tooltipBorder: themedColor("tooltip.border", foreground)
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
    readonly property int fontBaseSize: impl.fontBaseSize
    readonly property int fontCaption: fontToken("caption", 0.833)
    readonly property int fontBodySmall: fontToken("body-small", 0.917)
    readonly property int fontBody: fontToken("body", 1.0)
    readonly property int fontSubtitle: fontToken("subtitle", 1.083)
    readonly property int fontTitle: fontToken("title", 1.167)
    readonly property int fontHeading: fontToken("heading", 1.333)
    readonly property int fontDisplay: fontToken("display", 2.0)
    readonly property int fontDisplayLarge: fontToken("display-large", 2.333)
    readonly property int fontIconSmall: fontToken("icon-small", 0.917)
    readonly property int fontIcon: fontToken("icon", 1.167)
    readonly property int fontIconLarge: fontToken("icon-large", 1.5)
    readonly property string iconTheme: impl.iconTheme
    readonly property int epoch: impl.epoch

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

    function fontToken(name, mult) {
        var fallback = Math.max(1, Math.round(root.fontBaseSize * mult))
        var value = Number(root.raw("font." + name, fallback))
        return isFinite(value) && value > 0 ? Math.round(value) : fallback
    }

    function spacingToken(name, fallback) {
        var value = Number(root.raw("spacing." + name, NaN))
        return isFinite(value) && value >= 0 ? Math.round(value) : root.space(fallback)
    }

    readonly property int spaceXXS: spacingToken("xxs", 2)
    readonly property int spaceXS: spacingToken("xs", 3)
    readonly property int spaceSM: spacingToken("sm", 4)
    readonly property int spaceMD: spacingToken("md", 6)
    readonly property int spaceLG: spacingToken("lg", 8)
    readonly property int spaceXL: spacingToken("xl", 10)
    readonly property int spaceXXL: spacingToken("xxl", 12)
    readonly property int spaceXXXL: spacingToken("xxxl", 14)
    readonly property int spaceHuge: spacingToken("huge", 18)
    readonly property int controlGap: spacingToken("control-gap", 8)
    readonly property int controlPaddingX: spacingToken("control-padding-x", 10)
    readonly property int controlPaddingY: spacingToken("control-padding-y", 6)
    readonly property int inputPaddingY: spacingToken("input-padding-y", 7)
    readonly property int controlHeight: spacingToken("control-height", 28)
    readonly property int popupRowHeight: spacingToken("popup-row-height", 28)
    readonly property int rowGap: spacingToken("row-gap", 8)
    readonly property int rowPaddingX: spacingToken("row-padding-x", 12)
    readonly property int labelGap: spacingToken("label-gap", 4)
    readonly property int panelGap: spacingToken("panel-gap", 14)
    readonly property int panelPadding: spacingToken("panel-padding", 18)
    readonly property int popupPadding: spacingToken("popup-padding", 14)

    function iconSource(name, size) {
        return "image://synchroicon/" + name + "/" + Math.max(8, size || fontIcon) +
               "?epoch=" + root.epoch
    }
}
