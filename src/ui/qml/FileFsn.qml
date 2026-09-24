import QtQuick
import Synchro 1.0
import Synchro.Theme

// Synchro's StrataV / MapV 3D browser presentation. MapV keeps the fsv-style
// size map; StrataV turns sibling folders into a navigable neighborhood.
// See src/core/FsnLayout.cpp for the geometry.
Item {
    id: fsn

    required property var fileModel
    property var filterProxy
    property var navStack
    property var keyMachine
    property var selection
    property var catalog: null
    property var host: null
    property var fileOps: null
    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property bool showCursorChrome: !keyMachine || keyMachine.listFocused
    // Keys belong to a panel: keep the cursor visible (panel apps target
    // the selected file) but dimmed so focus stays legible.
    readonly property bool cursorDim: keyMachine && keyMachine.panelFocused
    readonly property bool treeView: !keyMachine || keyMachine.fsnTreeView
    readonly property int themeEpoch: Theme.epoch

    signal viewToggleRequested()
    signal doRequested(real sceneX, real sceneY)

    focus: true
    activeFocusOnTab: true
    clip: true

    // ------------------------------------------------------------- camera
    property real yaw: 0
    property real pitch: 0.42
    property real dist: 60
    property real tx: 0
    property real ty: 0
    property real tz: 32
    property bool camAnim: true
    property string focusPath: ""
    property string scenePath: ""
    property var catalogBoxes: []
    property int catalogRequest: 0
    property bool catalogScene: false
    property bool sceneLoading: false
    property string sceneSource: "filesystem"
    property var sceneMeta: ({})
    property string requestedRoot: ""
    property string expansionRoot: ""
    property var expandedPaths: ({})
    readonly property bool catalogOwned: catalogScene ||
                                         (sceneLoading &&
                                          sceneSource === "catalog")
    readonly property var sceneBoxes: catalogOwned
                                      ? catalogBoxes
                                      : (fileModel && fileModel.fsnBoxes
                                         ? fileModel.fsnBoxes : [])
    readonly property real nearPlane: 0.6
    property var visibleLabels: []

    Behavior on tx { enabled: fsn.camAnim; NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    Behavior on ty { enabled: fsn.camAnim; NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    Behavior on tz { enabled: fsn.camAnim; NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    Behavior on dist { enabled: fsn.camAnim; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }
    Behavior on pitch { enabled: fsn.camAnim; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }
    Behavior on yaw { enabled: fsn.camAnim; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop {
                position: 0
                color: Theme.alpha(Theme.darkerBackground, 0.76)
            }
            GradientStop {
                position: 0.48
                color: Theme.alpha(Theme.background, 0.62)
            }
            GradientStop {
                position: 1
                color: Theme.alpha(Theme.darkerBackground, 0.72)
            }
        }
    }

    // ------------------------------------------------------------- scene
    property var scene: null

    function rebuildScene() {
        var raw = fsn.sceneBoxes || []
        var solids = []
        var flats = []
        var isTree = false
        var minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9
        var minY = 1e9, maxY = -1e9
        var rootPrim = null
        var platByPath = ({})
        for (var i = 0; i < raw.length; ++i) {
            var b = raw[i]
            var pts = b.base
            var top = b.top
            var n = pts.length / 2
            var cx = 0, cz = 0
            for (var k = 0; k < n; ++k) {
                var px = Number(pts[2 * k])
                var pz = Number(pts[2 * k + 1])
                cx += px; cz += pz
                if (px < minX) minX = px
                if (px > maxX) maxX = px
                if (pz < minZ) minZ = pz
                if (pz > maxZ) maxZ = pz
            }
            var p = {
                name: b.name || "",
                path: b.path || "",
                kind: Number(b.kind),
                ntype: Number(b.ntype),
                isDir: !!b.isDir,
                root: !!b.root,
                bytes: Number(b.bytes),
                sourceParent: b.sourceParent || "",
                extension: b.extension || "",
                mime: b.mime || "",
                category: b.category || "",
                ageBucket: b.ageBucket || "",
                mtime: Number(b.mtime || 0),
                childCount: Number(b.childCount || 0),
                fileCount: Number(b.fileCount || 0),
                dirCount: Number(b.dirCount || 0),
                hidden: !!b.hidden,
                isLink: !!b.isLink,
                aggregate: !!b.aggregate,
                expanded: !!b.expanded,
                depthLevel: Number(b.depthLevel || 0),
                sizeScore: Number(b.sizeScore === undefined ? 0.5
                                                            : b.sizeScore),
                base: pts,
                top: top,
                n: n,
                y0: Number(b.y0),
                h: Number(b.h),
                cx: cx / n,
                cz: cz / n,
                hasLabel: b.labelSize !== undefined,
                labelX: Number(b.labelX || 0),
                labelZ: Number(b.labelZ || 0),
                labelAngle: Number(b.labelAngle || 0),
                labelSize: Number(b.labelSize || 0),
                hasRoads: b.gateX !== undefined,
                gateX: Number(b.gateX || 0),
                gateZ: Number(b.gateZ || 0),
                exitX: Number(b.exitX || 0),
                exitZ: Number(b.exitZ || 0),
                parentPath: b.parentPath !== undefined ? b.parentPath : null,
                neighbors: b.neighbors || [],
                ownerPath: b.ownerPath || "",
                kids: []
            }
            var yTop = p.y0 + p.h
            if (p.y0 < minY) minY = p.y0
            if (yTop > maxY) maxY = yTop
            if (p.kind === 3) { // road
                flats.push(p)
                isTree = true
            } else {
                solids.push(p)
                if (p.kind === 0 && p.path.length)
                    platByPath[p.path] = p
            }
            if (p.hasLabel)
                isTree = true
            if (p.root)
                rootPrim = p
        }
        if (!solids.length) {
            fsn.scene = null
            view.requestPaint()
            return
        }
        // Parent/child links (via path parentage) drive a hierarchical
        // painter: a parent always draws before its descendants, siblings
        // sort by depth. A flat global depth sort mis-stacks huge parent
        // slabs against the small boxes standing on them, so geometry
        // popped in and out as the camera orbited.
        var topLevel = []
        for (i = 0; i < solids.length; ++i) {
            var sp = solids[i]
            if (sp === rootPrim) {
                topLevel.push(sp)
                continue
            }
            var parent = null
            var slash = sp.path ? sp.path.lastIndexOf("/") : -1
            if (slash > 0)
                parent = platByPath[sp.path.substring(0, slash)] || null
            if (!parent || parent === sp)
                parent = rootPrim
            if (parent && parent !== sp)
                parent.kids.push(sp)
            else
                topLevel.push(sp)
        }
        var ctrX = (minX + maxX) / 2
        var ctrZ = (minZ + maxZ) / 2
        var radius = Math.max(4, Math.hypot(maxX - minX, maxZ - minZ) / 2)
        fsn.scene = {
            solids: solids,
            flats: flats,
            isTree: isTree,
            rootPrim: rootPrim,
            platByPath: platByPath,
            topLevel: topLevel,
            ctrX: ctrX, ctrZ: ctrZ, radius: radius,
            spanX: maxX - minX, spanZ: maxZ - minZ,
            minY: minY, maxY: maxY,
            count: solids.length
        }
        var freshPath = fsn.fileModel ? fsn.fileModel.path : ""
        var fly = freshPath !== fsn.scenePath
        fsn.scenePath = freshPath
        fsn.cancelFlight()
        // Let the establishing shot land before cursor-follow glides kick in.
        fsn.glideBlockUntil = Date.now() + (fly ? 1600 : 400)
        fsn.homeCamera(fly)
        view.requestPaint()
    }

    function homeCamera(flyIn) {
        var s = fsn.scene
        if (!s)
            return
        var wantTx, wantTy, wantTz, wantPitch, wantYaw, wantDist
        if (s.isTree) {
            // StrataV has no root monument: frame the neighborhood itself.
            wantTx = s.ctrX
            wantTz = s.ctrZ
            var spanY = Math.max(1, s.maxY - s.minY)
            wantTy = s.minY + spanY * 0.38
            // A shallow yaw keeps the street graph legible while exposing the
            // MapV contents of an expanded building roof.
            var aspect = fsn.height > 0 ? fsn.width / fsn.height : 1
            wantYaw = aspect >= 1.0 ? 0.16 : 0.05
            wantPitch = aspect >= 1.0 ? 0.62 : 0.57
            var focal = Math.min(fsn.width, fsn.height) * 1.15
            var fitX = s.spanX * focal / Math.max(1, fsn.width * 0.86)
            // Ground depth is foreshortened at this pitch, so fitting the raw
            // z span against viewport height left the neighborhood tiny.
            var fitZ = s.spanZ * focal / Math.max(1, fsn.height * 1.10)
            var fitY = spanY * focal / Math.max(1, fsn.height * 0.72)
            wantDist = Math.max(18, fitX, fitZ, fitY) + 5
        } else {
            wantTx = s.ctrX
            wantTz = s.ctrZ
            wantTy = 1
            wantYaw = 0.30
            wantPitch = 0.72
            wantDist = s.radius * 1.7 + 8
        }
        if (flyIn) {
            fsn.camAnim = false
            fsn.tx = wantTx
            fsn.ty = wantTy + 26
            fsn.tz = wantTz
            fsn.yaw = wantYaw + 0.5
            fsn.pitch = Math.min(1.25, wantPitch + 0.45)
            fsn.dist = wantDist * 2.4
            fsn.camAnim = true
            Qt.callLater(function () {
                fsn.ty = wantTy
                fsn.yaw = wantYaw
                fsn.pitch = wantPitch
                fsn.dist = wantDist
            })
        } else {
            fsn.tx = wantTx
            fsn.ty = wantTy
            fsn.tz = wantTz
            fsn.yaw = wantYaw
            fsn.pitch = wantPitch
            fsn.dist = wantDist
        }
    }

    // --------------------------------------------------------- projection
    function toView(wx, wy, wz) {
        var dx = wx - fsn.tx
        var dy = wy - fsn.ty
        var dz = wz - fsn.tz
        var cy = Math.cos(fsn.yaw)
        var sy = Math.sin(fsn.yaw)
        var x1 = dx * cy - dz * sy
        var z1 = dx * sy + dz * cy
        var cp = Math.cos(fsn.pitch)
        var sp = Math.sin(fsn.pitch)
        return {
            // QMatrix4x4::lookAt uses cross(forward, up) for camera-right.
            // With StrataV's +Z-facing camera that is the negative of x1.
            // Keep this identical to FsnRhiRenderer's view matrix so labels
            // and picking stay attached to geometry during orbit and pan.
            x: -x1,
            y: dy * cp + z1 * sp,
            d: z1 * cp - dy * sp + fsn.dist
        }
    }

    function vProject(v) {
        var f = Math.min(fsn.width, fsn.height) * 1.15
        return {
            x: fsn.width * 0.5 + v.x * f / v.d,
            y: fsn.height * 0.5 - v.y * f / v.d,
            d: v.d
        }
    }

    function project(wx, wy, wz) {
        var projected = rhiView.projectPoint(wx, wy, wz)
        return projected && projected.d !== undefined ? projected : null
    }

    // Track-style camera pan in the current screen plane. Scaling by camera
    // distance keeps the gesture useful both over the whole landscape and
    // after a close bridge flight.
    function panByPixels(dx, dy) {
        if ((!dx && !dy) || fsn.width <= 0 || fsn.height <= 0)
            return
        fsn.cancelFlight()
        var focal = Math.min(fsn.width, fsn.height) * 1.15
        var worldPerPixel = Math.max(0.002, fsn.dist / Math.max(1, focal))
        var cy = Math.cos(fsn.yaw)
        var sy = Math.sin(fsn.yaw)
        var cp = Math.cos(fsn.pitch)
        var sp = Math.sin(fsn.pitch)
        // Camera right = (cy, 0, -sy); camera up = (sy*sp, cp, cy*sp).
        // Middle-drag moves the camera target in the drag direction, matching
        // spatial-viewer pan conventions rather than content-drag scrolling.
        fsn.camAnim = false
        fsn.tx += (dx * cy - dy * sy * sp) * worldPerPixel
        fsn.ty -= dy * cp * worldPerPixel
        fsn.tz += (-dx * sy - dy * cy * sp) * worldPerPixel
        fsn.camAnim = true
        view.requestPaint()
    }

    function viewDepth(wx, wy, wz) {
        return fsn.toView(wx, wy, wz).d
    }

    // Clip a view-space polygon against the near plane so geometry that is
    // partly behind the camera (long arc roads, close platforms) still draws
    // instead of vanishing.
    function clipViewPoly(pts) {
        var near = fsn.nearPlane
        var out = []
        for (var i = 0; i < pts.length; ++i) {
            var a = pts[i]
            var b = pts[(i + 1) % pts.length]
            var ain = a.d >= near
            var bin = b.d >= near
            if (ain)
                out.push(a)
            if (ain !== bin) {
                var t = (near - a.d) / (b.d - a.d)
                out.push({ x: a.x + (b.x - a.x) * t,
                           y: a.y + (b.y - a.y) * t,
                           d: near })
            }
        }
        return out
    }

    // Transform, clip, cull, and fill one world-space polygon.
    function drawWorldPoly(ctx, wpts, fill, stroke) {
        var v = []
        var anyFront = false
        for (var i = 0; i < wpts.length; ++i) {
            var vv = fsn.toView(wpts[i][0], wpts[i][1], wpts[i][2])
            if (vv.d >= fsn.nearPlane)
                anyFront = true
            v.push(vv)
        }
        if (!anyFront)
            return false
        var c = fsn.clipViewPoly(v)
        if (c.length < 3)
            return false
        var s = []
        for (i = 0; i < c.length; ++i)
            s.push(fsn.vProject(c[i]))
        if (fsn.shoelace(s) >= 0)
            return false
        fsn.fillPoly(ctx, s, fill, stroke)
        return true
    }

    function rgb(color) {
        return [Number(color.r), Number(color.g), Number(color.b)]
    }

    function rgba(color, alpha) {
        return "rgba(" + Math.round(color.r * 255) + "," +
                Math.round(color.g * 255) + "," +
                Math.round(color.b * 255) + "," + alpha + ")"
    }

    function canvasFont(weight, family) {
        return weight + ' 64px "' + String(family).replace(/"/g, "") + '"'
    }

    // Theme-derived node palette. Platforms remain distinct from folders,
    // while files inherit the same foreground/accent relationship as tiles.
    function fillColor(p) {
        if (p.kind === 0) {
            var platform = fsn.rgb(Theme.fsnPlatform)
            var scale = 0.80 + 0.28 * Math.max(0, Math.min(1, p.sizeScore))
            return [platform[0] * scale, platform[1] * scale,
                    platform[2] * scale]
        }
        if (p.ntype === 1)
            return fsn.rgb(Theme.fsnDirectory)
        if (p.ntype === 3)
            return fsn.rgb(Theme.fsnSymlink)
        var color = Theme.fsnFile
        if (p.category === "image")
            color = Theme.fsnImage
        else if (p.category === "video")
            color = Theme.fsnVideo
        else if (p.category === "audio")
            color = Theme.fsnAudio
        else if (p.category === "code")
            color = Theme.fsnCode
        else if (p.category === "data")
            color = Theme.fsnData
        else if (p.category === "archive")
            color = Theme.fsnArchive
        var rgb = fsn.rgb(color)
        var age = p.ageBucket === "today" ? 1.08
                : p.ageBucket === "week" ? 1.04
                : p.ageBucket === "older" ? 0.88 : 1.0
        if (p.hidden)
            age *= 0.76
        return [rgb[0] * age, rgb[1] * age, rgb[2] * age]
    }

    function css(rgb, k) {
        var r = Math.max(0, Math.min(1, rgb[0] * k))
        var g = Math.max(0, Math.min(1, rgb[1] * k))
        var b = Math.max(0, Math.min(1, rgb[2] * k))
        return "rgb(" + Math.round(r * 255) + "," + Math.round(g * 255) + ","
                + Math.round(b * 255) + ")"
    }

    function humanBytes(value) {
        var bytes = Math.max(0, Number(value || 0))
        var units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
        var unit = 0
        while (bytes >= 1024 && unit < units.length - 1) {
            bytes /= 1024
            ++unit
        }
        var digits = unit === 0 ? 0 : (bytes >= 100 ? 0 : bytes >= 10 ? 1 : 2)
        return bytes.toFixed(digits) + " " + units[unit]
    }

    function shoelace(pts) {
        var a = 0
        for (var i = 0; i < pts.length; ++i) {
            var j = (i + 1) % pts.length
            a += pts[i].x * pts[j].y - pts[j].x * pts[i].y
        }
        return a
    }

    function fillPoly(ctx, pts, color, stroke) {
        ctx.beginPath()
        ctx.moveTo(pts[0].x, pts[0].y)
        for (var i = 1; i < pts.length; ++i)
            ctx.lineTo(pts[i].x, pts[i].y)
        ctx.closePath()
        ctx.fillStyle = color
        ctx.fill()
        if (stroke) {
            ctx.strokeStyle = stroke
            ctx.lineWidth = 1
            ctx.stroke()
        }
    }

    // Directional light, fsv-ish: top bright, camera-facing sides lit.
    function shade(nx, ny, nz) {
        var cy = Math.cos(fsn.yaw)
        var sy = Math.sin(fsn.yaw)
        // Light rides with the camera heading so the front stays lit.
        var lx = 0.30 * cy - (-0.42) * sy
        var lz = 0.30 * sy + (-0.42) * cy
        var dot = nx * lx + ny * 0.85 + nz * lz
        return 0.36 + 0.62 * Math.max(0, dot)
    }

    // Strict projection of a whole prim; used for picking, where a prim
    // partly behind the camera is fine to ignore.
    function projectPrim(p) {
        var pb = [], pt = []
        var yTop = p.y0 + p.h
        for (var i = 0; i < p.n; ++i) {
            var bx = Number(p.base[2 * i]), bz = Number(p.base[2 * i + 1])
            var tx2 = Number(p.top[2 * i]), tz2 = Number(p.top[2 * i + 1])
            var a = fsn.project(bx, p.y0, bz)
            var b = fsn.project(tx2, yTop, tz2)
            if (!a || !b)
                return null
            pb.push(a)
            pt.push(b)
        }
        return { pb: pb, pt: pt }
    }

    function drawSolid(ctx, p) {
        var yTop = p.y0 + p.h
        var isCursor = fsn.showCursorChrome && fsn.cursorPath.length &&
                       p.path === fsn.cursorPath && !p.root
        var rgb = isCursor ? fsn.rgb(Theme.fsnSelection) : fsn.fillColor(p)
        var sideRgb = rgb
        if (!isCursor && p.ownerPath) {
            var platformRgb = fsn.rgb(Theme.fsnPlatform)
            sideRgb = [platformRgb[0] * 0.40 + rgb[0] * 0.60,
                       platformRgb[1] * 0.40 + rgb[1] * 0.60,
                       platformRgb[2] * 0.40 + rgb[2] * 0.60]
        }
        var edge = fsn.rgba(Theme.fsnEdge, isCursor ? 0.72 : 0.42)
        var drew = false
        var i, j
        for (i = 0; i < p.n; ++i) {
            j = (i + 1) % p.n
            var bix = Number(p.base[2 * i]), biz = Number(p.base[2 * i + 1])
            var bjx = Number(p.base[2 * j]), bjz = Number(p.base[2 * j + 1])
            var tjx = Number(p.top[2 * j]), tjz = Number(p.top[2 * j + 1])
            var tix = Number(p.top[2 * i]), tiz = Number(p.top[2 * i + 1])
            var ex = bjx - bix, ez = bjz - biz
            var len = Math.hypot(ex, ez) || 1
            var k = fsn.shade(ez / len, 0, -ex / len)
            if (fsn.drawWorldPoly(ctx,
                                  [[bix, p.y0, biz], [bjx, p.y0, bjz],
                                   [tjx, yTop, tjz], [tix, yTop, tiz]],
                                  fsn.css(sideRgb, k), edge))
                drew = true
        }
        var topW = []
        for (i = 0; i < p.n; ++i)
            topW.push([Number(p.top[2 * i]), yTop, Number(p.top[2 * i + 1])])
        var topShade = p.ownerPath ? 1.18 : fsn.shade(0, 1, 0)
        if (fsn.drawWorldPoly(ctx, topW, fsn.css(rgb, topShade),
                              edge))
            drew = true
        return drew
    }

    // Text drawn onto a world-space plane via an affine approximation:
    // anchor is the baseline center, u the baseline direction, v glyph-up.
    function planeText(ctx, text, ax, ay, az, ux, uy, uz, vx, vy, vz, size,
                       color, font, minPx) {
        var p0 = fsn.project(ax, ay, az)
        var pu = fsn.project(ax + ux * size, ay + uy * size, az + uz * size)
        var pv = fsn.project(ax + vx * size, ay + vy * size, az + vz * size)
        if (!p0 || !pu || !pv)
            return
        var hPx = Math.hypot(pv.x - p0.x, pv.y - p0.y)
        if (hPx < minPx || hPx > fsn.height * 2)
            return
        var F = 64
        ctx.save()
        ctx.setTransform((pu.x - p0.x) / F, (pu.y - p0.y) / F,
                         -(pv.x - p0.x) / F, -(pv.y - p0.y) / F,
                         p0.x, p0.y)
        ctx.font = font
        ctx.textAlign = "center"
        ctx.textBaseline = "alphabetic"
        ctx.fillStyle = color
        ctx.fillText(text, 0, 0)
        ctx.restore()
        ctx.setTransform(1, 0, 0, 1, 0, 0)
    }

    function drawGroundLabel(ctx, p) {
        var ux = Math.cos(p.labelAngle)
        var uz = Math.sin(p.labelAngle)
        // Glyph-up points radially outward: rotate baseline by +90 in xz.
        var label = p.name
        if (p.aggregate && p.childCount > 0)
            label += "  ·  " + p.childCount
        fsn.planeText(ctx, label, p.labelX, 0.02, p.labelZ,
                      ux, 0, uz, -uz, 0, ux, p.labelSize,
                      fsn.rgba(Theme.fsnLabel, 0.92),
                      fsn.canvasFont("700", Theme.sansFontFamily),
                      3)
    }

    function drawLeafLabel(ctx, p) {
        // Surface labels keep file names technical but use the configured
        // Omarchy mono face instead of a baked-in font.
        var b0x = Number(p.base[0]), b0z = Number(p.base[1])
        var b1x = Number(p.base[2]), b1z = Number(p.base[3])
        var ex = b1x - b0x, ez = b1z - b0z
        var w = Math.hypot(ex, ez) || 1
        var ux = ex / w, uz = ez / w
        var len = Math.max(1, p.name.length)
        var font = fsn.canvasFont("700", Theme.monoFontFamily)
        var surfaceText = fsn.rgba(Theme.fsnSurfaceText, 0.94)
        if (fsn.scene && fsn.scene.isTree && p.h > 0.3) {
            var size = Math.min(0.30, p.h * 0.5, w * 1.6 / len)
            fsn.planeText(ctx, p.name,
                          (b0x + b1x) / 2, p.y0 + p.h - size * 0.55,
                          (b0z + b1z) / 2,
                          ux, 0, uz, 0, 1, 0, size, surfaceText, font, 5)
        } else {
            // Top-face label; glyph-up points to the rear (+ perpendicular).
            var t0x = Number(p.top[0]), t0z = Number(p.top[1])
            var t1x = Number(p.top[2]), t1z = Number(p.top[3])
            var t3x = Number(p.top[6]), t3z = Number(p.top[7])
            var dW = Math.hypot(t1x - t0x, t1z - t0z)
            var dD = Math.hypot(t3x - t0x, t3z - t0z)
            var size2 = Math.min(dD * 0.55, dW * 1.6 / len)
            if (size2 < 0.03)
                return
            var cx2, cz2
            if (p.kind === 0) { // platform: label on the front strip
                cx2 = (t0x + t1x) / 2 + (t3x - t0x) * 0.10
                cz2 = (t0z + t1z) / 2 + (t3z - t0z) * 0.10
                size2 = Math.min(size2, dD * 0.16)
            } else {
                cx2 = (t0x + t1x) / 2 + (t3x - t0x) * 0.5
                cz2 = (t0z + t1z) / 2 + (t3z - t0z) * 0.5
            }
            var vx = (t3x - t0x) / (dD || 1), vz = (t3z - t0z) / (dD || 1)
            fsn.planeText(ctx, p.name, cx2 + vx * size2 * -0.5,
                          p.y0 + p.h + 0.01, cz2 + vz * size2 * -0.5,
                          ux, 0, uz, vx, 0, vz, size2, surfaceText, font, 3.5)
        }
    }

    function drawCursor(ctx, p) {
        var minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9
        for (var i = 0; i < p.n; ++i) {
            var x = Number(p.base[2 * i]), z = Number(p.base[2 * i + 1])
            if (x < minX) minX = x
            if (x > maxX) maxX = x
            if (z < minZ) minZ = z
            if (z > maxZ) maxZ = z
        }
        var pad = 0.10 + 0.04 * (maxX - minX)
        minX -= pad; maxX += pad; minZ -= pad; maxZ += pad
        var y0 = p.y0 - 0.05, y1 = p.y0 + p.h + 0.08
        var cs = [
            [minX, y0, minZ], [maxX, y0, minZ], [maxX, y0, maxZ], [minX, y0, maxZ],
            [minX, y1, minZ], [maxX, y1, minZ], [maxX, y1, maxZ], [minX, y1, maxZ]
        ]
        var pp = []
        for (i = 0; i < 8; ++i) {
            var pr = fsn.project(cs[i][0], cs[i][1], cs[i][2])
            if (!pr)
                return
            pp.push(pr)
        }
        var edges = [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],
                     [0,4],[1,5],[2,6],[3,7]]
        ctx.strokeStyle = fsn.rgba(Theme.fsnSelection, 1)
        ctx.globalAlpha = fsn.cursorDim ? 0.5 : 1
        ctx.lineWidth = 1.5
        ctx.beginPath()
        for (i = 0; i < edges.length; ++i) {
            var a = pp[edges[i][0]], b = pp[edges[i][1]]
            // fsv-style corner brackets: draw only the ends of each edge.
            var f = 0.22
            ctx.moveTo(a.x, a.y)
            ctx.lineTo(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f)
            ctx.moveTo(b.x, b.y)
            ctx.lineTo(b.x + (a.x - b.x) * f, b.y + (a.y - b.y) * f)
        }
        ctx.stroke()
        ctx.globalAlpha = 1
    }

    // ------------------------------------------------------------- flight
    // 1990s-VR navigation: fly the camera down the roads to a clicked node.
    property var flight: null
    property real flightT: 0

    NumberAnimation {
        id: flightAnim
        target: fsn
        property: "flightT"
        from: 0
        to: 1
        easing.type: Easing.InOutSine
        onStopped: {
            fsn.flight = null
            fsn.camAnim = true
        }
    }

    onFlightTChanged: fsn.applyFlight()

    function cancelFlight() {
        flightDelay.stop()
        fsn.pendingHit = null
        if (flightAnim.running)
            flightAnim.stop()
        fsn.flight = null
        fsn.cancelDive()
        fsn.camAnim = true
    }

    function lerp(a, b, t) { return a + (b - a) * t }

    function applyFlight() {
        var f = fsn.flight
        if (!f)
            return
        var d = f.total * fsn.flightT
        var k = 0
        while (k < f.cum.length - 2 && f.cum[k + 1] < d)
            ++k
        var segLen = f.cum[k + 1] - f.cum[k]
        var t = segLen > 1e-6 ? (d - f.cum[k]) / segLen : 0
        var a = f.pts[k], b = f.pts[k + 1]
        fsn.camAnim = false
        fsn.tx = fsn.lerp(a[0], b[0], t)
        fsn.ty = fsn.lerp(a[1], b[1], t)
        fsn.tz = fsn.lerp(a[2], b[2], t)
        // Heading follows the road; ease across segment joints.
        var yawHere = fsn.lerp(f.yaws[k], f.yaws[k + 1], t)
        var p = fsn.flightT
        if (p < 0.18) {
            var e = p / 0.18
            fsn.yaw = fsn.lerp(f.startYaw, yawHere, e)
            fsn.pitch = fsn.lerp(f.startPitch, f.drivePitch, e)
            fsn.dist = fsn.lerp(f.startDist, f.driveDist, e)
        } else if (p > 0.82) {
            var e2 = (p - 0.82) / 0.18
            fsn.yaw = fsn.lerp(yawHere, f.endYaw, e2)
            fsn.pitch = fsn.lerp(f.drivePitch, f.endPitch, e2)
            fsn.dist = fsn.lerp(f.driveDist, f.endDist, e2)
        } else {
            fsn.yaw = yawHere
            fsn.pitch = f.drivePitch
            fsn.dist = f.driveDist
        }
    }

    function platformOf(prim) {
        var s = fsn.scene
        if (!s || !prim)
            return null
        if (prim.kind === 0)
            return prim
        var slash = prim.path.lastIndexOf("/")
        if (slash <= 0)
            return null
        return s.platByPath[prim.path.substring(0, slash)] || null
    }

    function platformChain(plat) {
        var s = fsn.scene
        var chain = []
        var guard = 0
        while (plat && guard++ < 16) {
            chain.unshift(plat)
            plat = plat.parentPath !== null && plat.parentPath.length
                   ? s.platByPath[plat.parentPath] : null
        }
        return chain
    }

    function nearestPlatform() {
        return fsn.nearestPlatformTo(fsn.tx, fsn.tz)
    }

    function nearestPlatformTo(x, z) {
        var s = fsn.scene
        var best = null
        var bestD = 1e18
        for (var path in s.platByPath) {
            var p = s.platByPath[path]
            var d = Math.hypot(p.cx - x, p.cz - z)
            if (d < bestD) {
                bestD = d
                best = p
            }
        }
        return best
    }

    function streetAncestor(plat) {
        var s = fsn.scene
        var guard = 0
        while (plat && plat.parentPath && guard++ < 16) {
            var parent = s.platByPath[plat.parentPath]
            if (!parent)
                break
            plat = parent
        }
        return plat
    }

    function streetRoute(start, target) {
        var s = fsn.scene
        if (!start || !target)
            return []
        if (start.path === target.path)
            return [start]
        var queue = [start.path]
        var previous = ({})
        previous[start.path] = ""
        for (var head = 0; head < queue.length; ++head) {
            var path = queue[head]
            var node = s.platByPath[path]
            var links = node ? node.neighbors : []
            for (var i = 0; i < links.length; ++i) {
                var next = String(links[i])
                if (previous[next] !== undefined)
                    continue
                previous[next] = path
                if (next === target.path) {
                    var paths = [next]
                    var cursor = next
                    while (previous[cursor]) {
                        cursor = previous[cursor]
                        paths.unshift(cursor)
                    }
                    var route = []
                    for (var r = 0; r < paths.length; ++r) {
                        if (s.platByPath[paths[r]])
                            route.push(s.platByPath[paths[r]])
                    }
                    return route
                }
                queue.push(next)
            }
        }
        return []
    }

    // Waypoints for one hop between a parent terrace and its child. The camera
    // rides above the stepped bridge while descending with the hierarchy.
    function hopPoints(parent, child) {
        var py = parent.y0 + parent.h + 0.78
        var cy = child.y0 + child.h + 0.78
        var branchZ = parent.exitZ + (child.gateZ - parent.exitZ) * 0.46
        var branchY = (py + cy) * 0.5 + 0.45
        return [
            [parent.exitX, py, parent.exitZ],
            [parent.exitX, branchY, branchZ],
            [child.gateX, branchY, branchZ],
            [child.gateX, cy, child.gateZ],
            [child.cx, cy, child.cz]
        ]
    }

    function flyAlongRoads(hit) {
        var s = fsn.scene
        if (!s || !s.isTree)
            return false
        var target = fsn.platformOf(hit)
        if (!target && hit)
            target = fsn.nearestPlatformTo(hit.cx, hit.cz)
        if (!target)
            return false
        var start = fsn.nearestPlatform()
        if (!start)
            return false
        var startStreet = fsn.streetAncestor(start)
        var targetStreet = fsn.streetAncestor(target)
        var street = fsn.streetRoute(startStreet, targetStreet)
        var pts = [[fsn.tx, fsn.ty, fsn.tz]]
        var i
        if (street.length) {
            for (i = 0; i < street.length; ++i) {
                var stop = street[i]
                pts.push([stop.cx, stop.y0 + stop.h + 0.82, stop.cz])
            }
            if (target.path !== targetStreet.path)
                pts.push([target.cx, target.y0 + target.h + 0.78, target.cz])
        } else {
            var a = fsn.platformChain(start)
            var b = fsn.platformChain(target)
            var cp = 0
            while (cp < a.length && cp < b.length &&
                   a[cp].path === b[cp].path)
                ++cp
            if (cp === 0) {
                // An isolated building or rooftop: fly direct.
                pts.push([target.cx, target.y0 + target.h + 0.78, target.cz])
            } else {
                // Climb from the start platform back toward the common ancestor…
                for (i = a.length - 1; i >= cp; --i) {
                    var hop = fsn.hopPoints(a[i - 1], a[i])
                    pts.push([a[i].cx,
                              a[i].y0 + a[i].h + 0.78, a[i].cz])
                    for (var h = hop.length - 2; h >= 0; --h)
                        pts.push(hop[h])
                }
                pts.push([a[cp - 1].cx,
                          a[cp - 1].y0 + a[cp - 1].h + 0.78,
                          a[cp - 1].cz])
                // …then ride the roads out to the target.
                for (i = cp; i < b.length; ++i) {
                    var hop2 = fsn.hopPoints(b[i - 1], b[i])
                    for (var h2 = 0; h2 < hop2.length; ++h2)
                        pts.push(hop2[h2])
                }
            }
        }
        // Final approach: hover before the clicked node itself.
        var endDist
        if (hit.kind === 0) {
            var bb = fsn.primBounds(hit)
            endDist = Math.max(8, Math.hypot(bb.maxX - bb.minX,
                                             bb.maxZ - bb.minZ) * 1.1 + 3)
        } else {
            pts.push([hit.cx, hit.y0 + hit.h * 0.5, hit.cz])
            endDist = Math.max(3.5, hit.h * 2.2 + 3)
        }
        // Drop zero-length segments, then precompute lengths and headings.
        var clean = [pts[0]]
        for (i = 1; i < pts.length; ++i) {
            var prev = clean[clean.length - 1]
            if (Math.hypot(pts[i][0] - prev[0], pts[i][2] - prev[2]) +
                    Math.abs(pts[i][1] - prev[1]) > 0.05)
                clean.push(pts[i])
        }
        if (clean.length < 2)
            return false
        var cum = [0]
        var total = 0
        for (i = 1; i < clean.length; ++i) {
            total += Math.hypot(clean[i][0] - clean[i - 1][0],
                                clean[i][2] - clean[i - 1][2])
            cum.push(total)
        }
        if (total < 1.0)
            return false
        // Per-vertex headings, unwrapped so interpolation never spins the
        // long way around.
        var yaws = []
        for (i = 0; i < clean.length; ++i) {
            var i0 = Math.max(0, i - 1)
            var i1 = Math.min(clean.length - 1, i + 1)
            var hy = Math.atan2(clean[i1][0] - clean[i0][0],
                                clean[i1][2] - clean[i0][2])
            if (yaws.length) {
                while (hy - yaws[yaws.length - 1] > Math.PI)
                    hy -= 2 * Math.PI
                while (hy - yaws[yaws.length - 1] < -Math.PI)
                    hy += 2 * Math.PI
            }
            yaws.push(hy)
        }
        var startYaw = fsn.yaw
        while (yaws[0] - startYaw > Math.PI)
            startYaw += 2 * Math.PI
        while (yaws[0] - startYaw < -Math.PI)
            startYaw -= 2 * Math.PI
        fsn.cancelFlight()
        fsn.flight = {
            pts: clean, cum: cum, total: total, yaws: yaws,
            startYaw: startYaw, startPitch: fsn.pitch, startDist: fsn.dist,
            drivePitch: 0.30, driveDist: 8,
            endYaw: yaws[yaws.length - 1], endPitch: 0.42, endDist: endDist
        }
        fsn.glideBlockUntil = Date.now() + 20000
        fsn.flightT = 0
        flightAnim.duration = Math.max(1100, Math.min(6500, total * 60))
        flightAnim.start()
        return true
    }

    function primBounds(p) {
        var minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9
        for (var i = 0; i < p.n; ++i) {
            var x = Number(p.base[2 * i]), z = Number(p.base[2 * i + 1])
            if (x < minX) minX = x
            if (x > maxX) maxX = x
            if (z < minZ) minZ = z
            if (z > maxZ) maxZ = z
        }
        return { minX: minX, maxX: maxX, minZ: minZ, maxZ: maxZ }
    }

    // --------------------------------------------------------------- dive
    // Double-clicking a folder punches the camera into its platform first;
    // navigation (and the new scene's establishing fly-in) follows.
    property var dive: null
    property real diveT: 0

    NumberAnimation {
        id: diveAnim
        target: fsn
        property: "diveT"
        from: 0
        to: 1
        duration: 420
        easing.type: Easing.InCubic
        onStopped: fsn.finishDive()
    }

    onDiveTChanged: fsn.applyDive()

    function startDive(prim, index) {
        if (flightAnim.running)
            flightAnim.stop()
        fsn.flight = null
        fsn.cancelDive()
        fsn.dive = {
            path: prim.path, index: index,
            fromTx: fsn.tx, fromTy: fsn.ty, fromTz: fsn.tz,
            fromDist: fsn.dist, fromPitch: fsn.pitch,
            toTx: prim.cx, toTy: prim.y0 + prim.h, toTz: prim.cz,
            toDist: 2.0, toPitch: Math.max(0.30, fsn.pitch * 0.75)
        }
        fsn.glideBlockUntil = Date.now() + 3000
        fsn.diveT = 0
        diveAnim.start()
    }

    function applyDive() {
        var d = fsn.dive
        if (!d)
            return
        var t = fsn.diveT
        fsn.camAnim = false
        fsn.tx = fsn.lerp(d.fromTx, d.toTx, t)
        fsn.ty = fsn.lerp(d.fromTy, d.toTy, t)
        fsn.tz = fsn.lerp(d.fromTz, d.toTz, t)
        fsn.dist = fsn.lerp(d.fromDist, d.toDist, t)
        fsn.pitch = fsn.lerp(d.fromPitch, d.toPitch, t)
        fsn.camAnim = true
    }

    function finishDive() {
        var d = fsn.dive
        fsn.dive = null
        fsn.camAnim = true
        if (!d || fsn.diveT < 1)
            return // cancelled mid-zoom
        if (d.index >= 0)
            fsn.activate()
        else if (fsn.navStack)
            fsn.navStack.navigate(d.path)
    }

    function cancelDive() {
        if (diveAnim.running)
            diveAnim.stop()
        fsn.dive = null
    }

    // ------------------------------------------------------------ cursor
    property string cursorPath: ""
    property double glideBlockUntil: 0

    function recAt(i) {
        if (fsn.rows && fsn.rows.rowMap)
            return fsn.rows.rowMap(i)
        return ({})
    }

    function syncCursor(glide) {
        var p = ""
        if (fsn.selection && fsn.selection.cursorPath)
            p = fsn.selection.cursorPath()
        var i = fsn.rows ? fsn.rows.currentIndex : -1
        if (!p.length && i >= 0) {
            var rec = fsn.recAt(i)
            p = rec && rec.path ? rec.path : ""
        }
        if (!p.length)
            p = fsn.focusPath
        if (fsn.cursorPath === p)
            return
        fsn.cursorPath = p
        if (glide && Date.now() < fsn.glideBlockUntil)
            glide = false
        if (glide && fsn.scene && !flightAnim.running) {
            var prim = fsn.findPrim(p)
            if (prim) {
                fsn.tx = prim.cx
                fsn.ty = prim.y0
                fsn.tz = prim.cz
            }
        }
        view.requestPaint()
    }

    function findPrim(path) {
        if (!fsn.scene || !path)
            return null
        var s = fsn.scene.solids
        for (var i = 0; i < s.length; ++i) {
            if (s[i].path === path)
                return s[i]
        }
        return null
    }

    function listingIndexFor(path) {
        if (!path || !fsn.rows)
            return -1
        var n = fsn.rows.count
        for (var i = 0; i < n; ++i) {
            var rec = fsn.recAt(i)
            if (rec && rec.path === path)
                return i
        }
        return -1
    }

    // Geometry is uploaded only when the scene, theme, or selection changes.
    // Camera motion updates one matrix; the GPU owns projection and depth.
    FsnRhiView {
        id: rhiView
        objectName: "fsnRhiView"
        anchors.fill: parent
        primitives: fsn.sceneBoxes || []
        selectedPath: fsn.showCursorChrome ? fsn.cursorPath : ""
        cameraX: fsn.tx
        cameraY: fsn.ty
        cameraZ: fsn.tz
        yaw: fsn.yaw
        pitch: fsn.pitch
        distance: fsn.dist
        scenePalette: ({
            platform: Theme.fsnPlatform,
            directory: Theme.fsnDirectory,
            file: Theme.fsnFile,
            image: Theme.fsnImage,
            video: Theme.fsnVideo,
            audio: Theme.fsnAudio,
            code: Theme.fsnCode,
            data: Theme.fsnData,
            archive: Theme.fsnArchive,
            symlink: Theme.fsnSymlink,
            road: Theme.fsnRoad,
            roadEdge: Theme.fsnRoadEdge,
            edge: Theme.fsnEdge,
            selection: Theme.fsnSelection
        })
    }

    function refreshVisibleLabels() {
        var s = fsn.scene
        if (!s) {
            fsn.visibleLabels = []
            return
        }
        var candidates = []
        for (var i = 0; i < s.solids.length; ++i) {
            var p = s.solids[i]
            if (!p.name.length || p.root)
                continue
            // Rooftop MapV geometry explains its district but is deliberately
            // not a second selectable browser nested inside StrataV.
            if (s.isTree && p.ownerPath.length)
                continue
            var anchor = fsn.labelAnchor(p)
            var top = fsn.project(anchor.x, anchor.y, anchor.z)
            var bottom = fsn.project(p.cx, p.y0, p.cz)
            if (!top || !bottom || top.x < -80 || top.x > fsn.width + 80 ||
                    top.y < -40 || top.y > fsn.height + 40)
                continue
            var pixelHeight = Math.abs(bottom.y - top.y)
            var chosen = p.path === fsn.cursorPath || p.isDir || p.hasLabel ||
                         pixelHeight >= 12
            if (!chosen)
                continue
            var priority = p.path === fsn.cursorPath ? -1000000
                         : p.isDir ? -10000
                         : p.hasLabel ? -5000 : 0
            candidates.push({ prim: p, screen: top,
                              score: priority + top.d })
        }
        candidates.sort(function(a, b) { return a.score - b.score })
        var labels = []
        var occupied = []
        var rows = Math.max(1, Math.floor(fsn.height / 42))
        var cols = Math.max(1, Math.floor(fsn.width / 150))
        var limit = Math.min(72, Math.max(14, rows * cols), candidates.length)
        for (i = 0; i < candidates.length && labels.length < limit; ++i) {
            var candidate = candidates[i]
            var prim = candidate.prim
            var selected = prim.path === fsn.cursorPath
            var px = selected ? Theme.fontBody : Theme.fontCaption
            var labelWidth = Math.min(fsn.width * 0.36,
                                      Math.max(36, prim.name.length * px * 0.62))
            var labelHeight = px + (prim.hasLabel && prim.isDir
                                    ? Theme.fontCaption + 4 : 0) + 8
            var rect = {
                x0: candidate.screen.x - labelWidth / 2 - 5,
                x1: candidate.screen.x + labelWidth / 2 + 5,
                y0: candidate.screen.y - labelHeight - 5,
                y1: candidate.screen.y + 5
            }
            var collides = false
            if (!selected) {
                for (var j = 0; j < occupied.length; ++j) {
                    var other = occupied[j]
                    if (rect.x0 < other.x1 && rect.x1 > other.x0 &&
                            rect.y0 < other.y1 && rect.y1 > other.y0) {
                        collides = true
                        break
                    }
                }
            }
            if (!collides) {
                labels.push(prim)
                occupied.push(rect)
            }
        }
        fsn.visibleLabels = labels
    }

    function labelAnchor(p) {
        if (p && p.hasLabel)
            return { x: p.labelX, y: p.y0 + p.h + 0.08, z: p.labelZ }
        return { x: p.cx, y: p.y0 + p.h + 0.18, z: p.cz }
    }

    property bool labelRefreshPending: false

    function scheduleLabels() {
        fsn.labelRefreshPending = true
        if (!labelRefresh.running)
            labelRefresh.start()
    }

    Timer {
        id: labelRefresh
        interval: 16
        repeat: true
        onTriggered: {
            if (!fsn.labelRefreshPending) {
                stop()
                return
            }
            fsn.labelRefreshPending = false
            fsn.refreshVisibleLabels()
        }
    }

    // Bounded glyph nodes replace the old full-window Canvas texture. Their
    // positions and collision selection follow the same frame cadence as the
    // camera, favoring folders and the cursor when the scene gets dense.
    Item {
        id: view
        objectName: "fileFsnView"
        anchors.fill: parent
        function requestPaint() { fsn.scheduleLabels() }

        Repeater {
            model: fsn.visibleLabels
            delegate: Item {
                required property var modelData
                readonly property var anchor: fsn.labelAnchor(modelData)
                readonly property var screen: fsn.project(
                    anchor.x, anchor.y, anchor.z)
                readonly property bool selected:
                    fsn.showCursorChrome && modelData.path === fsn.cursorPath
                implicitWidth: Math.max(nameLabel.implicitWidth,
                                        sizeLabel.implicitWidth)
                implicitHeight: labelColumn.implicitHeight
                visible: screen !== null
                x: screen ? screen.x - implicitWidth / 2 : -10000
                y: screen ? screen.y - implicitHeight : -10000
                z: selected ? 2 : 1

                Column {
                    id: labelColumn
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 1

                    Text {
                        id: nameLabel
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.name
                        color: selected ? Theme.fsnSelection : Theme.fsnLabel
                        font.family: modelData.hasLabel
                                     ? Theme.sansFontFamily
                                     : Theme.monoFontFamily
                        font.pixelSize: selected ? Theme.fontBody
                                                 : Theme.fontCaption
                        font.bold: selected || modelData.isDir
                        style: Text.Outline
                        styleColor: Theme.alpha(Theme.darkerBackground, 0.92)
                    }

                    Text {
                        id: sizeLabel
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: modelData.hasLabel && modelData.isDir
                        text: fsn.humanBytes(modelData.bytes)
                        color: selected ? Theme.fsnSelection : Theme.muted
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontCaption
                        style: Text.Outline
                        styleColor: Theme.alpha(Theme.darkerBackground, 0.92)
                    }
                }
            }
        }
    }

    function depthCompare(a, b) {
        return fsn.viewDepth(b.cx, b.y0 + b.h * 0.5, b.cz) -
               fsn.viewDepth(a.cx, a.y0 + a.h * 0.5, a.cz)
    }

    function drawNode(ctx, p, s, found) {
        if (fsn.drawSolid(ctx, p) && p.name.length &&
                (p.kind === 1 || p.kind === 2 || (!s.isTree && p.kind === 0)))
            fsn.drawLeafLabel(ctx, p)
        if (fsn.cursorPath.length && p.path === fsn.cursorPath && !p.root)
            found.cursor = p
        if (p.kids.length) {
            var order = p.kids.slice()
            order.sort(fsn.depthCompare)
            for (var i = 0; i < order.length; ++i)
                fsn.drawNode(ctx, order[i], s, found)
        }
    }

    // -------------------------------------------------------------- input
    function paintOrderList() {
        var s = fsn.scene
        var out = []
        if (!s)
            return out
        var rec = function (p) {
            out.push(p)
            var order = p.kids.slice()
            order.sort(fsn.depthCompare)
            for (var k = 0; k < order.length; ++k)
                rec(order[k])
        }
        var top = s.topLevel.slice()
        top.sort(fsn.depthCompare)
        for (var i = 0; i < top.length; ++i)
            rec(top[i])
        return out
    }

    // Walk the paint order back to front so the visually topmost prim wins,
    // exactly matching what the renderer put on screen.
    function pickAt(mx, my) {
        var order = fsn.paintOrderList()
        var pt = { x: mx, y: my }
        for (var i = order.length - 1; i >= 0; --i) {
            var p = order[i]
            var pr = fsn.projectPrim(p)
            if (!pr)
                continue
            if (fsn.pointInPoly(pt, pr.pt))
                return p
            // Walls: check each front-facing side quad.
            var pb = pr.pb, tp = pr.pt
            var n = pb.length
            for (var e = 0; e < n; ++e) {
                var j = (e + 1) % n
                var quad = [pb[e], pb[j], tp[j], tp[e]]
                if (fsn.shoelace(quad) < 0 && fsn.pointInPoly(pt, quad))
                    return p
            }
        }
        return null
    }

    function interactionTarget(prim) {
        if (!prim || !fsn.scene || !fsn.scene.isTree || !prim.ownerPath)
            return prim
        return fsn.findPrim(prim.ownerPath) || prim
    }

    function pointInPoly(p, poly) {
        var inside = false
        for (var i = 0, j = poly.length - 1; i < poly.length; j = i++) {
            var a = poly[i], b = poly[j]
            if (((a.y > p.y) !== (b.y > p.y)) &&
                    (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) || 1e-9) + a.x))
                inside = !inside
        }
        return inside
    }

    function selectIndex(i, mods, right) {
        if (i < 0)
            return
        fsn.forceActiveFocus()
        if (fsn.keyMachine && fsn.keyMachine.mode === "field-search")
            fsn.keyMachine.focusList()
        if (fsn.selection) {
            if (right) {
                if (!fsn.selection.isSelected(i))
                    fsn.selection.click(i)
            } else if (mods & Qt.ControlModifier)
                fsn.selection.ctrlClick(i)
            else if (mods & Qt.ShiftModifier)
                fsn.selection.shiftClick(i)
            else
                fsn.selection.leftClick(i)
            return
        }
        if (fsn.filterProxy)
            fsn.filterProxy.selectRow(i)
        else if (fsn.fileModel)
            fsn.fileModel.currentIndex = i
    }

    function activate() {
        if (fsn.filterProxy)
            fsn.filterProxy.activateCurrent()
        else if (fsn.fileModel)
            fsn.fileModel.activateCurrent()
    }

    function selectPath(path, isDir, mods, right, name, bytes) {
        if (!path)
            return
        fsn.focusPath = path
        var i = fsn.listingIndexFor(path)
        if (i >= 0) {
            fsn.selectIndex(i, mods, right)
            fsn.syncCursor(false)
            return
        }
        fsn.cursorPath = path
        // Recursive StrataV/MapV descendants do not have a row in the current
        // directory proxy. Publish them as a real browser selection anyway so
        // Look, Action Deck, and file operations target the visible object.
        if (fsn.selection && fsn.selection.selectPath)
            fsn.selection.selectPath(path, name || "", !!isDir,
                                     bytes === undefined ? -1 : Number(bytes))
        if (isDir && fsn.navStack && (mods & Qt.ShiftModifier))
            fsn.navStack.navigate(path)
        view.requestPaint()
    }

    function activatePath(path, isDir) {
        if (!path)
            return
        var i = fsn.listingIndexFor(path)
        // StrataV folders get a quick punch-in before the actual navigation;
        // the new scene's establishing fly-in picks up from there.
        if (isDir && fsn.scene && fsn.scene.isTree) {
            var prim = fsn.findPrim(path)
            if (prim) {
                if (i >= 0)
                    fsn.selectIndex(i, 0, false)
                fsn.startDive(prim, i)
                return
            }
        }
        if (i >= 0) {
            fsn.selectIndex(i, 0, false)
            fsn.activate()
            return
        }
        if (isDir && fsn.navStack)
            fsn.navStack.navigate(path)
        else if (fsn.host && fsn.host.openFile)
            fsn.host.openFile(path, "")
    }

    function expandedPathList() {
        var paths = []
        for (var path in fsn.expandedPaths) {
            if (fsn.expandedPaths[path])
                paths.push(path)
        }
        paths.sort()
        return paths
    }

    function setExpanded(path, on) {
        if (!path || path === fsn.requestedRoot)
            return false
        var next = ({})
        var prefix = path + "/"
        for (var oldPath in fsn.expandedPaths) {
            if (!on && (oldPath === path || oldPath.indexOf(prefix) === 0))
                continue
            if (fsn.expandedPaths[oldPath])
                next[oldPath] = true
        }
        if (on)
            next[path] = true
        if (!!fsn.expandedPaths[path] === on &&
                Object.keys(next).length === Object.keys(fsn.expandedPaths).length)
            return false
        fsn.expandedPaths = next
        fsn.kickScan()
        return true
    }

    function expandCursor(on) {
        var prim = fsn.findPrim(fsn.cursorPath)
        if (!prim || !prim.isDir || prim.root)
            return false
        return fsn.setExpanded(prim.path, on)
    }

    function kickScan() {
        var root = fsn.fileModel ? fsn.fileModel.path : ""
        if (fsn.expansionRoot !== root) {
            fsn.expansionRoot = root
            fsn.expandedPaths = ({})
        }
        fsn.requestedRoot = root
        fsn.catalogScene = false
        fsn.catalogBoxes = []
        fsn.sceneMeta = ({})
        if (fsn.catalog && root && fsn.catalog.coversTree &&
                fsn.catalog.coversTree(root) && fsn.catalog.sceneExpanded) {
            fsn.sceneLoading = true
            fsn.sceneSource = "catalog"
            fsn.scene = null
            fsn.catalogRequest = fsn.catalog.sceneExpanded(
                        root, fsn.treeView ? "tree" : "map",
                        fsn.fileModel ? fsn.fileModel.showHidden : false,
                        fsn.expandedPathList())
            view.requestPaint()
            return
        }
        fsn.sceneLoading = false
        fsn.sceneSource = "filesystem"
        if (fsn.fileModel && fsn.fileModel.refreshFsnExpanded)
            fsn.fileModel.refreshFsnExpanded(
                        fsn.treeView ? "tree" : "map",
                        fsn.expandedPathList())
    }

    // A plain click waits out the double-click window before its road
    // flight starts, so a double-click dives into exactly what was first
    // clicked (the flight would otherwise move the camera between clicks
    // and re-pick a neighbor).
    property var pendingHit: null

    Timer {
        id: flightDelay
        interval: 260
        onTriggered: {
            var h = fsn.pendingHit
            fsn.pendingHit = null
            if (!h)
                return
            if (!fsn.flyAlongRoads(h) && fsn.scene) {
                fsn.tx = h.cx
                fsn.ty = h.y0
                fsn.tz = h.cz
            }
            view.requestPaint()
        }
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        property real lastX: 0
        property real lastY: 0
        property bool didDrag: false

        onPressed: function (mouse) {
            didDrag = false
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: function (mouse) {
            var orbiting = !!(pressedButtons & Qt.LeftButton)
            var panning = !!(pressedButtons & Qt.MiddleButton)
            if (!orbiting && !panning)
                return
            var dx = mouse.x - lastX
            var dy = mouse.y - lastY
            if (Math.abs(dx) + Math.abs(dy) > 3)
                didDrag = true
            if (didDrag)
                fsn.cancelFlight()
            if (panning) {
                fsn.panByPixels(dx, dy)
            } else {
                fsn.camAnim = false
                fsn.yaw += dx * 0.008
                fsn.pitch = Math.max(0.10,
                                     Math.min(1.35,
                                              fsn.pitch + dy * 0.006))
                fsn.camAnim = true
            }
            lastX = mouse.x
            lastY = mouse.y
            view.requestPaint()
        }
        onWheel: function (wheel) {
            fsn.cancelFlight()
            var steps = wheel.angleDelta.y / 120
            fsn.dist = Math.max(3, Math.min(420, fsn.dist * Math.pow(0.78, steps)))
        }
        onClicked: function (mouse) {
            if (didDrag)
                return
            if (mouse.button === Qt.MiddleButton) {
                return
            }
            var hit = fsn.interactionTarget(fsn.pickAt(mouse.x, mouse.y))
            if (!hit) {
                fsn.cancelFlight()
                return
            }
            fsn.selectPath(hit.path, hit.isDir, mouse.modifiers,
                           mouse.button === Qt.RightButton,
                           hit.name, hit.bytes)
            if (mouse.button === Qt.RightButton) {
                var point = drag.mapToItem(null, mouse.x, mouse.y)
                fsn.doRequested(point.x, point.y)
            } else if (mouse.modifiers === Qt.NoModifier) {
                // Plain click: navigate there VR-style, riding the roads —
                // but only once the double-click window has passed.
                fsn.pendingHit = hit
                flightDelay.restart()
            }
            view.requestPaint()
        }
        onDoubleClicked: function (mouse) {
            if (didDrag || mouse.button === Qt.MiddleButton)
                return
            flightDelay.stop()
            var hit = fsn.pendingHit ||
                      fsn.interactionTarget(fsn.pickAt(mouse.x, mouse.y))
            fsn.pendingHit = null
            fsn.cancelFlight()
            if (hit)
                fsn.activatePath(hit.path, hit.isDir)
            else
                fsn.activate()
        }
    }

    Rectangle {
        id: sceneHud
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceLG
        width: Math.min(parent.width - Theme.spaceLG * 2,
                        hudRow.implicitWidth + Theme.controlPaddingX * 2)
        height: Theme.controlHeight
        radius: Theme.radius
        clip: true
        color: Theme.alpha(Theme.background, 0.78)
        border.color: fsn.cursorDim ? Theme.normalBorder : Theme.focusBorder
        border.width: 1
        z: 2

        Row {
            id: hudRow
            anchors.centerIn: parent
            spacing: Theme.controlGap

            Text {
                text: fsn.treeView ? "StrataV" : "MapV"
                color: Theme.accent
                font.family: Theme.sansFontFamily
                font.pixelSize: Theme.fontCaption
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }

            Rectangle {
                width: 1
                height: Theme.fontBody
                color: Theme.normalBorder
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: {
                    var bits = []
                    if (fsn.sceneLoading ||
                            (!fsn.catalogScene && fsn.fileModel &&
                             fsn.fileModel.fsnListing))
                        bits.push("scanning…")
                    else if (fsn.scene)
                        bits.push(fsn.scene.count + " nodes")
                    if (fsn.sceneSource === "catalog") {
                        bits.push("indexed")
                        if (fsn.sceneMeta &&
                                Number(fsn.sceneMeta.rootChildCount || 0) > 0)
                            bits.push(Number(fsn.sceneMeta.rootChildCount) +
                                      " here")
                        if (fsn.sceneMeta &&
                                Number(fsn.sceneMeta.expandedCount || 0) > 0)
                            bits.push(Number(fsn.sceneMeta.expandedCount) +
                                      " open")
                    }
                    bits.push("WASD")
                    bits.push("drag")
                    bits.push("middle pan")
                    bits.push("wheel")
                    bits.push("M switch")
                    bits.push("E/→ expand")
                    bits.push("C/← collapse")
                    bits.push("Enter")
                    return bits.join("  ·  ")
                }
                color: Theme.muted
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontCaption
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Row {
        id: typeKey
        objectName: "fsnTypeKey"
        anchors.left: sceneHud.left
        anchors.bottom: sceneHud.top
        anchors.bottomMargin: Theme.spaceSM
        spacing: Theme.controlGap
        visible: fsn.width >= 680
        z: 2

        Repeater {
            model: [
                { label: "IMAGE", color: Theme.fsnImage },
                { label: "VIDEO", color: Theme.fsnVideo },
                { label: "AUDIO", color: Theme.fsnAudio },
                { label: "CODE", color: Theme.fsnCode },
                { label: "DATA", color: Theme.fsnData },
                { label: "ARCHIVE", color: Theme.fsnArchive }
            ]

            delegate: Row {
                required property var modelData
                spacing: Theme.spaceXS

                Rectangle {
                    width: 5
                    height: 5
                    radius: 1
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.color
                }

                Text {
                    text: modelData.label
                    color: Theme.alpha(modelData.color, 0.78)
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Math.max(8, Theme.fontCaption - 2)
                    font.bold: true
                }
            }
        }
    }

    EmptyListing {
        anchors.centerIn: parent
        fileModel: fsn.fileModel
        filterProxy: fsn.filterProxy
        visible: !fsn.sceneLoading &&
                 !(fsn.fileModel && fsn.fileModel.fsnListing) &&
                 !(fsn.sceneBoxes && fsn.sceneBoxes.length)
        z: 2
    }

    Text {
        anchors.centerIn: parent
        visible: (fsn.sceneLoading ||
                  (fsn.fileModel && fsn.fileModel.fsnListing)) &&
                 !(fsn.sceneBoxes && fsn.sceneBoxes.length)
        text: fsn.sceneSource === "catalog"
              ? "assembling indexed landscape…" : "scanning filesystem…"
        color: Theme.muted
        font.family: Theme.sansFontFamily
        font.pixelSize: Theme.fontBody
        z: 2
    }

    onWidthChanged: view.requestPaint()
    onHeightChanged: view.requestPaint()
    onYawChanged: view.requestPaint()
    onPitchChanged: view.requestPaint()
    onDistChanged: view.requestPaint()
    onTxChanged: view.requestPaint()
    onTyChanged: view.requestPaint()
    onTzChanged: view.requestPaint()
    onTreeViewChanged: fsn.kickScan()
    onThemeEpochChanged: view.requestPaint()

    Connections {
        target: fsn.catalog
        function onSceneFinished(requestId, result) {
            if (requestId !== fsn.catalogRequest)
                return
            fsn.sceneLoading = false
            if (result.ok && result.root === fsn.requestedRoot &&
                    fsn.fileModel && result.root === fsn.fileModel.path) {
                fsn.catalogBoxes = result.boxes || []
                fsn.catalogScene = true
                fsn.sceneSource = "catalog"
                fsn.sceneMeta = result
                fsn.rebuildScene()
                fsn.syncCursor(false)
            } else {
                fsn.catalogScene = false
                fsn.sceneSource = "filesystem"
                if (fsn.fileModel && fsn.fileModel.refreshFsnExpanded)
                    fsn.fileModel.refreshFsnExpanded(
                                fsn.treeView ? "tree" : "map",
                                fsn.expandedPathList())
            }
            view.requestPaint()
        }
    }

    Connections {
        target: fsn.rows
        function onCurrentIndexChanged() { fsn.syncCursor(true) }
        function onCountChanged() { view.requestPaint() }
        function onDataChanged(tl, br, roles) { view.requestPaint() }
        function onModelReset() { view.requestPaint() }
    }

    Connections {
        target: fsn.fileModel
        function onPathChanged() { fsn.kickScan() }
        function onShowHiddenChanged() { fsn.kickScan() }
        function onFsnBoxesChanged() {
            if (!fsn.catalogOwned) {
                fsn.rebuildScene()
                fsn.syncCursor(false)
            }
        }
        function onFsnListingChanged() { view.requestPaint() }
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function (event) {
        if (event.modifiers === Qt.NoModifier &&
                (event.key === Qt.Key_Left || event.key === Qt.Key_Right ||
                 event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
            event.accepted = true
            return
        }
        if (fsn.keyMachine &&
                fsn.keyMachine.handleListKey(event.key, event.modifiers, event.text)) {
            event.accepted = true
            return
        }
        // WASD drives the camera target over the ground plane, fsn style;
        // the mouse owns orbit and zoom.
        var step = (event.modifiers & Qt.ShiftModifier) ? 3.6 : 1.2
        var fwdX = Math.sin(fsn.yaw), fwdZ = Math.cos(fsn.yaw)
        var rightX = Math.cos(fsn.yaw), rightZ = -Math.sin(fsn.yaw)
        var drove = false
        if (event.key === Qt.Key_W) {
            fsn.driveBy(fwdX * step, fwdZ * step)
            drove = true
        } else if (event.key === Qt.Key_S) {
            fsn.driveBy(-fwdX * step, -fwdZ * step)
            drove = true
        } else if (event.key === Qt.Key_A) {
            fsn.driveBy(-rightX * step, -rightZ * step)
            drove = true
        } else if (event.key === Qt.Key_D) {
            fsn.driveBy(rightX * step, rightZ * step)
            drove = true
        } else if (event.key === Qt.Key_M && event.modifiers === Qt.NoModifier) {
            fsn.viewToggleRequested()
            event.accepted = true
        }
        if (drove)
            event.accepted = true
    }

    function driveBy(dx, dz) {
        fsn.cancelFlight()
        fsn.camAnim = false
        fsn.tx += dx
        fsn.tz += dz
        fsn.camAnim = true
        view.requestPaint()
    }

    onActiveFocusChanged: {
        if (activeFocus && fsn.keyMachine && fsn.keyMachine.fieldFocused &&
                fsn.keyMachine.mode !== "field-search")
            fsn.keyMachine.focusList()
    }

    Component.onCompleted: {
        fsn.kickScan()
        fsn.rebuildScene()
        fsn.syncCursor(false)
        view.requestPaint()
    }
}
