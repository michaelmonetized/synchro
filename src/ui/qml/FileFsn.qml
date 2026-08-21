import QtQuick
import Synchro.Theme

// fsv/fsn homage (":fsv"): software-rendered 3D filesystem, after fsv 0.9.
// TreeV: gray platforms on dark-red roads, khaki file boxes, ground labels,
// and camera flights that travel the roads. MapV: nested treemap slabs.
// See src/core/FsnLayout.cpp for the geometry.
Item {
    id: fsn

    required property var fileModel
    property var filterProxy
    property var navStack
    property var keyMachine
    property var selection
    property var host: null
    property var fileOps: null
    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property bool showCursorChrome: !keyMachine || keyMachine.listFocused
    // Keys belong to a panel: keep the cursor visible (panel apps target
    // the selected file) but dimmed so focus stays legible.
    readonly property bool cursorDim: keyMachine && keyMachine.panelFocused
    readonly property bool treeView: !keyMachine || keyMachine.fsnTreeView

    signal viewToggleRequested()
    signal doRequested()

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
    readonly property real nearPlane: 0.6

    Behavior on tx { enabled: fsn.camAnim; NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    Behavior on ty { enabled: fsn.camAnim; NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    Behavior on tz { enabled: fsn.camAnim; NumberAnimation { duration: 750; easing.type: Easing.InOutCubic } }
    Behavior on dist { enabled: fsn.camAnim; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }
    Behavior on pitch { enabled: fsn.camAnim; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }
    Behavior on yaw { enabled: fsn.camAnim; NumberAnimation { duration: 650; easing.type: Easing.InOutCubic } }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    // ------------------------------------------------------------- scene
    property var scene: null

    function rebuildScene() {
        var raw = fsn.fileModel && fsn.fileModel.fsnBoxes
                  ? fsn.fileModel.fsnBoxes : []
        var solids = []
        var flats = []
        var isTree = false
        var minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9, maxY = 0
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
                kids: []
            }
            var yTop = p.y0 + p.h
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
            ctrX: ctrX, ctrZ: ctrZ, radius: radius, maxY: maxY,
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
            var rp = s.rootPrim
            // fsn framing: root platform low in frame, children fanning away,
            // aimed between the root and the scene's center so the whole
            // tree sits centered in the viewport.
            var rx = rp ? rp.cx : s.ctrX
            var rz = rp ? rp.cz : s.ctrZ
            wantTx = (rx + s.ctrX) / 2
            wantTz = (rz + s.ctrZ) / 2
            wantTy = 0
            wantYaw = 0
            wantPitch = 0.50
            wantDist = Math.min(130, Math.max(16, s.radius * 0.85 + 8))
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
            x: x1,
            y: dy * cp + z1 * sp,
            d: z1 * cp - dy * sp + fsn.dist
        }
    }

    function vProject(v) {
        var f = Math.min(fsn.width, fsn.height) * 1.15
        return {
            x: fsn.width * 0.5 + v.x * f / v.d,
            y: fsn.height * 0.55 - v.y * f / v.d,
            d: v.d
        }
    }

    function project(wx, wy, wz) {
        var v = fsn.toView(wx, wy, wz)
        if (v.d < fsn.nearPlane)
            return null
        return fsn.vProject(v)
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

    // fsv node palette (color.c defaults)
    function fillColor(p) {
        if (p.ntype === 1)
            return [0.627, 0.627, 0.627]  // directory  #A0A0A0
        if (p.ntype === 3)
            return [1.0, 1.0, 1.0]        // symlink    #FFFFFF
        return [1.0, 1.0, 0.627]          // file       #FFFFA0
    }

    function css(rgb, k) {
        var r = Math.max(0, Math.min(1, rgb[0] * k))
        var g = Math.max(0, Math.min(1, rgb[1] * k))
        var b = Math.max(0, Math.min(1, rgb[2] * k))
        return "rgb(" + Math.round(r * 255) + "," + Math.round(g * 255) + ","
                + Math.round(b * 255) + ")"
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
        var rgb = isCursor ? [1.0, 1.0, 1.0] : fsn.fillColor(p)
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
                                  fsn.css(rgb, k), "rgba(0,0,0,0.30)"))
                drew = true
        }
        var topW = []
        for (i = 0; i < p.n; ++i)
            topW.push([Number(p.top[2 * i]), yTop, Number(p.top[2 * i + 1])])
        if (fsn.drawWorldPoly(ctx, topW, fsn.css(rgb, fsn.shade(0, 1, 0)),
                              "rgba(0,0,0,0.18)"))
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
        fsn.planeText(ctx, p.name, p.labelX, 0.02, p.labelZ,
                      ux, 0, uz, -uz, 0, ux, p.labelSize,
                      "#FFFFFF", 'bold 64px "Times New Roman", Georgia, serif',
                      3)
    }

    function drawLeafLabel(ctx, p) {
        // Black text near the top of the camera-facing wall (TreeV) or on
        // the top face (MapV), like fsv's tmaptext labels.
        var b0x = Number(p.base[0]), b0z = Number(p.base[1])
        var b1x = Number(p.base[2]), b1z = Number(p.base[3])
        var ex = b1x - b0x, ez = b1z - b0z
        var w = Math.hypot(ex, ez) || 1
        var ux = ex / w, uz = ez / w
        var len = Math.max(1, p.name.length)
        var font = 'bold 64px "DejaVu Sans Mono", monospace'
        if (fsn.scene && fsn.scene.isTree && p.h > 0.3) {
            var size = Math.min(0.30, p.h * 0.5, w * 1.6 / len)
            fsn.planeText(ctx, p.name,
                          (b0x + b1x) / 2, p.y0 + p.h - size * 0.55,
                          (b0z + b1z) / 2,
                          ux, 0, uz, 0, 1, 0, size, "#000000", font, 5)
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
                          ux, 0, uz, vx, 0, vz, size2, "#000000", font, 3.5)
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
        ctx.strokeStyle = "#FFFF33"
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
        var s = fsn.scene
        var best = null
        var bestD = 1e18
        for (var path in s.platByPath) {
            var p = s.platByPath[path]
            var d = Math.hypot(p.cx - fsn.tx, p.cz - fsn.tz)
            if (d < bestD) {
                bestD = d
                best = p
            }
        }
        return best
    }

    // Waypoints for one hop between a parent platform and its child: down
    // the trunk, along the arc junction, up the stub, onto the platform.
    function hopPoints(parent, child) {
        var gr = Math.hypot(child.gateX, child.gateZ) || 1
        var jr = Math.max(1, gr - 0.5 * 8)
        return [
            [parent.exitX, 0.25, parent.exitZ],
            [child.gateX / gr * jr, 0.25, child.gateZ / gr * jr],
            [child.gateX, 0.25, child.gateZ],
            [child.cx, 0.85, child.cz]
        ]
    }

    function flyAlongRoads(hit) {
        var s = fsn.scene
        if (!s || !s.isTree)
            return false
        var target = fsn.platformOf(hit)
        if (!target)
            return false
        var start = fsn.nearestPlatform()
        if (!start)
            return false
        var a = fsn.platformChain(start)
        var b = fsn.platformChain(target)
        var cp = 0
        while (cp < a.length && cp < b.length && a[cp].path === b[cp].path)
            ++cp
        var pts = [[fsn.tx, fsn.ty, fsn.tz]]
        var i
        if (cp === 0) {
            // Disconnected (shouldn't happen): fly direct.
            pts.push([target.cx, 0.85, target.cz])
        } else {
            // Climb from the start platform back toward the common ancestor…
            for (i = a.length - 1; i >= cp; --i) {
                var hop = fsn.hopPoints(a[i - 1], a[i])
                pts.push([a[i].cx, 0.85, a[i].cz])
                for (var h = hop.length - 2; h >= 0; --h)
                    pts.push(hop[h])
            }
            pts.push([a[cp - 1].cx, 0.85, a[cp - 1].cz])
            // …then ride the roads out to the target.
            for (i = cp; i < b.length; ++i) {
                var hop2 = fsn.hopPoints(b[i - 1], b[i])
                for (var h2 = 0; h2 < hop2.length; ++h2)
                    pts.push(hop2[h2])
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
        var i = fsn.rows ? fsn.rows.currentIndex : -1
        var p = ""
        if (i >= 0) {
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

    // ------------------------------------------------------------- canvas
    Canvas {
        id: view
        objectName: "fileFsnView"
        anchors.fill: parent
        renderStrategy: Canvas.Immediate

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#000000"
            ctx.fillRect(0, 0, width, height)
            var s = fsn.scene
            if (!s)
                return
            var i, k
            // Roads first: flat, painted onto the black ground.
            var flats = s.flats.slice()
            flats.sort(function (a, b) {
                return fsn.viewDepth(b.cx, 0, b.cz) - fsn.viewDepth(a.cx, 0, a.cz)
            })
            for (i = 0; i < flats.length; ++i) {
                var road = flats[i]
                var wpts = []
                for (k = 0; k < road.n; ++k)
                    wpts.push([Number(road.base[2 * k]), 0,
                               Number(road.base[2 * k + 1])])
                fsn.drawWorldPoly(ctx, wpts, "rgb(128,0,0)",
                                  "rgba(60,0,0,0.8)")
            }
            // Ground labels over the roads, fsn style.
            for (i = 0; i < s.solids.length; ++i) {
                if (s.solids[i].hasLabel)
                    fsn.drawGroundLabel(ctx, s.solids[i])
            }
            // Solid geometry: hierarchical painter — parents before their
            // descendants, siblings far to near.
            var found = { cursor: null }
            var top = s.topLevel.slice()
            top.sort(fsn.depthCompare)
            for (i = 0; i < top.length; ++i)
                fsn.drawNode(ctx, top[i], s, found)
            var cursorPrim = found.cursor
            if (cursorPrim && fsn.showCursorChrome) {
                fsn.drawCursor(ctx, cursorPrim)
                var above = fsn.project(cursorPrim.cx,
                                        cursorPrim.y0 + cursorPrim.h + 0.6,
                                        cursorPrim.cz)
                if (above) {
                    ctx.font = "bold " + Theme.fontBody + "px " + Theme.fontFamily
                    ctx.textAlign = "center"
                    ctx.fillStyle = "#FFFF33"
                    ctx.fillText(cursorPrim.name, above.x, above.y)
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
                fsn.selection.click(i)
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

    function selectPath(path, isDir, mods, right) {
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
        if (right)
            return
        if (isDir && fsn.navStack && (mods & Qt.ShiftModifier))
            fsn.navStack.navigate(path)
        view.requestPaint()
    }

    function activatePath(path, isDir) {
        if (!path)
            return
        var i = fsn.listingIndexFor(path)
        // TreeV folders get a quick punch-in before the actual navigation;
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

    function kickScan() {
        if (fsn.fileModel && fsn.fileModel.refreshFsn)
            fsn.fileModel.refreshFsn(fsn.treeView ? "tree" : "map")
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
            if (!(pressedButtons & Qt.LeftButton))
                return
            var dx = mouse.x - lastX
            var dy = mouse.y - lastY
            if (Math.abs(dx) + Math.abs(dy) > 3)
                didDrag = true
            if (didDrag)
                fsn.cancelFlight()
            fsn.camAnim = false
            fsn.yaw += dx * 0.008
            fsn.pitch = Math.max(0.10, Math.min(1.35, fsn.pitch + dy * 0.006))
            fsn.camAnim = true
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
                fsn.viewToggleRequested()
                return
            }
            var hit = fsn.pickAt(mouse.x, mouse.y)
            if (!hit) {
                fsn.cancelFlight()
                return
            }
            fsn.selectPath(hit.path, hit.isDir, mouse.modifiers,
                           mouse.button === Qt.RightButton)
            if (mouse.button === Qt.RightButton) {
                fsn.doRequested()
            } else if (mouse.modifiers === Qt.NoModifier) {
                // Plain click: navigate there VR-style, riding the roads —
                // but only once the double-click window has passed.
                fsn.pendingHit = hit
                flightDelay.restart()
            }
            view.requestPaint()
        }
        onDoubleClicked: function (mouse) {
            if (didDrag)
                return
            flightDelay.stop()
            var hit = fsn.pendingHit || fsn.pickAt(mouse.x, mouse.y)
            fsn.pendingHit = null
            fsn.cancelFlight()
            if (hit)
                fsn.activatePath(hit.path, hit.isDir)
            else
                fsn.activate()
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space(8)
        text: {
            var bits = ["fsv"]
            bits.push(fsn.treeView ? "treev" : "mapv")
            if (fsn.fileModel && fsn.fileModel.fsnListing)
                bits.push("scanning…")
            else if (fsn.scene)
                bits.push(fsn.scene.count + " nodes")
            bits.push("click fly")
            bits.push("WASD drive")
            bits.push("drag orbit")
            bits.push("wheel zoom")
            bits.push("M " + (fsn.treeView ? "mapv" : "treev"))
            bits.push("Enter dive")
            bits.push("Esc leave")
            return bits.join("  ·  ")
        }
        color: "#707070"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        z: 2
    }

    EmptyListing {
        anchors.centerIn: parent
        fileModel: fsn.fileModel
        filterProxy: fsn.filterProxy
        visible: !(fsn.fileModel && fsn.fileModel.fsnListing) &&
                 !(fsn.fileModel && fsn.fileModel.fsnBoxes &&
                   fsn.fileModel.fsnBoxes.length)
        z: 2
    }

    Text {
        anchors.centerIn: parent
        visible: fsn.fileModel && fsn.fileModel.fsnListing &&
                 !(fsn.fileModel.fsnBoxes && fsn.fileModel.fsnBoxes.length)
        text: "scanning filesystem…"
        color: "#707070"
        font.family: Theme.fontFamily
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
        function onFsnBoxesChanged() { fsn.rebuildScene(); fsn.syncCursor(false) }
        function onFsnListingChanged() { view.requestPaint() }
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function (event) {
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
