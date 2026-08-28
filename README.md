# Synchro

Omarchy-native file OS: a Qt Quick browser, an opt-in OS file picker, and a
handler contract that works like Omarchy / Quickshell plugins — without living
inside `omarchy-shell`.

Nautilus stays the packaged folder app. This repo does not edit
`/usr/share/omarchy/`.

**Status: work in progress.** The window is daily-driveable on Omarchy 4
(Hyprland + Quickshell chrome + Qt 6.11). Peek, the do-layer, first-party
handlers, trash, thumbs, and an opt-in FileChooser portal are in-tree. Folder
MIME, Super+Shift+F, and session-default picker are **not** stolen yet.

## Why it exists

Omarchy's desktop chrome is one long-running Quickshell process
(`omarchy-shell`). Plugins for the bar, panels, and overlays install with
`omarchy plugin add <git-url>` — a `manifest.json`, reserved `omarchy.*` ids,
and an unsandboxed-code warning.

Synchro copies that **ritual**, not the process:

- Same instinct: a git repo, a manifest, kinds + entry points, `synchro handler add`.
- Different address space: handlers load in Synchro (in-process QML) or as a
  detached peer (`omawrite %f`, `omacut %f`). They never join `omarchy-shell`.
- A file-manager crash must not take the bar, lock screen, or polkit with it.

If you already write Omarchy plugins or Qt Quick apps, you already know how to
extend Synchro.

## Use it

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/synchro
./build/synchro ~/Pictures
```

`--new-window` is accepted and is the default (new process).

Optional develop install (binary + launchers on `~/.local/bin`, portal/D-Bus
files under `~/.local/share`):

```bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

`packaging/omarchy/` is a **user overlay**. CMake never writes `/usr/share/omarchy/`.

### Keys

The list is focused on launch. The command field is always visible; it is not
an always-focused omnibar.

| Key | What it does |
|---|---|
| `j`/`k` · `WASD` | Move. Shift+WASD leaps 5. |
| `h` / `Q` / Backspace | Up a directory |
| `l` / `E` / Right | Into a folder, or peek a file |
| `Enter` | Open the file, or enter the folder |
| `Space` | Toggle the lightweight Look panel. |
| `Shift+Space` | Open full Peek. Esc / Q / Space leave. |
| `Ctrl+Enter` / right-click | Do-layer (sticky actions + params) |
| `/` · `Ctrl+K` | Filter the listing |
| `:` | Command palette (`:trash` `:home` `:volumes` `:sql` `:flow` `:help`) |
| `Ctrl+L` | Jump (current path selected) |
| `?name` | Name search via `fd` |
| `v` | List / grid (or leave a 3D view). `V` is visual select. |
| `Ctrl+M` | Enter/leave StrataV; `M` switches StrataV / MapV while there. |
| Middle-drag | Pan the StrataV / MapV camera; left-drag orbits and the wheel zooms. |
| `y` `x` `p` | Copy / cut / paste |
| `Delete` | Trash. `u` undoes. |
| `t` | Terminal in this folder |
| `F1` / `:?` | Key reference |

Look is the ambient preview. Peek is deliberate deep inspection. Enter is
**commit**. The do-layer is **do**.

**StrataV / MapV.** `:fsv`, or Ctrl+M, turns the browser canvas into a spatial
3D view without giving up selection, Look, Miller browsing, or actions. An
indexed scene is assembled breadth-first on a separate cancellable worker.
StrataV omits the current-directory monument: its folders become bounded
MapV buildings in a deterministic road-connected neighborhood, and its files
occupy the open lots as smaller log-height structures. Indexed folder
buildings use exact recursive byte totals; footprint, height, and a restrained
brightness shift use a sibling-relative logarithmic p10–p90 scale, while the
exact total remains beneath the label. This makes meaningful differences
visible without allowing one backup tree to flatten the neighborhood. Each
district roof contains a MapV miniature of up to 96 immediate children plus an
explicit folded-remainder block. Rooftop geometry is explanatory rather than a
nested selection surface: every click, action, Look update, and camera flight
resolves to the owning folder. `E` / Right lifts the 96-cell fold for the
selected district; `C` / Left restores the bounded miniature. Rooftop color
uses the active Omarchy image, video, audio, code, data, and archive hues, with
a quiet HUD key; brightness still encodes recency. Every immediate root child
remains represented, and an uncataloged folder falls back to the same
structural live-filesystem renderer.

**Volumes.** `:volumes` or the `volumes` chip is a listing of user-facing mounts — not a sidebar, not GVFS. `/` is always a row (the system disk, even when home lives on it). Each row shows free / total / filesystem. `/` and extra disks also get a compact chip with free space (`/ 180G`, `KINGSTON 18G`); USB chips vanish when you unplug. Enter an extra disk and it is a **root tree**: crumbs start at `volumes / KINGSTON`, and Q / h at the mount root returns to the volumes listing instead of `/run/media`. Status line shows `KINGSTON  18G free / 64G` while you are inside. Do-layer **Eject** unmounts a removable volume (`udisksctl`). `/proc`, snaps, and portal mounts stay hidden.

**Look and Peek.** Space shows or hides the lightweight Look panel without
changing its saved startup preference. Shift+Space promotes the selection into
the full preview overlay. `A`/`D` hop the index ↔ the file; `W`/`S` then scroll
the preview (sqlite / duckdb / text / archives are interactive). Enter /
double-click in Peek is the same commit as the root listing.

**Do-layer.** Ctrl+Enter and right-click open the same sticky surface. Actions
on the left, look box on the right (file preview, or a large folder mosaic).
If the action has QML params, they mount under the look box — format picks,
open-with list, whatever the handler shipped. `W`/`S` change verbs. `A`/`D`
hop verbs ↔ params (or the preview). Enter runs. Esc / Q leave.

## Extend it

This is the product. Almost everything that *interprets* a file is a handler.
The core owns listing, navigation, selection, search, thumbnails, trash, and
the registry.

### Kinds

| Kind | When it runs | Typical runtime |
|---|---|---|
| `preview` | Peek (Space) and the do-layer look box | In-process QML |
| `open` | Enter / double-click | `exec` of a peer (`omawrite %f`) |
| `action` | Do-layer, `:` palette, or a key | `exec`, core verb, or QML params |
| `folder` | Entering a matching directory | Banner QML only (v1) |
| `location` | Jump chip / `:name` | Path, core adapter, or chrome QML |
| `thumbnail` | Listing / mosaic tiles | Core verb or system `.thumbnailer` |
| `panel` | Dockable sidecar surface | In-process QML |

A pack may declare more than one kind (preview + action is common).

**SQL files.** `:sql` opens a read-only DuckDB workbench over Synchro's
metadata catalog. SQLite remains the live source of truth; queries use the
latest complete native DuckDB shadow when one is available. `here` follows the
current folder, `selection`
is an execution-time snapshot of selected rows, and the recursive `tree`
relation indexes itself the first time a query needs it. `:sql scan` forces a
refresh without adding a permanent toolbar control. Tree scans are recursive,
uncapped, and incremental: committed batches are immediately queryable while
the dedicated scan worker continues, and active tree results refresh at a
controlled cadence. Completed scan roots and row counts are persisted alongside
the catalog, so relaunching reuses the existing tree instead of rebuilding it;
an indexed parent also covers queries rooted in its subfolders. Real
directories including `.git` and `node_modules` are indexed; directory
symlinks are listed but not followed to avoid loops.
Progress appears in the browser's bottom status rail. All
three expose raw byte
`size`, 1024-based floating-point `kb`,
`mb`, and `gb` rounded to two decimal places, plus boolean `hidden` (with
`is_hidden` retained for compatibility). Cheap navigation fields require no
content crawl or catalog rebuild: `kind` maps extensions into practical file
families, `stem` removes the final extension, `depth` counts path levels,
`age_days` drives `age_bucket`, `size_bucket` bands bytes, and
`modified_date` / `modified_month` expose calendar groupings. `root` identifies
the persisted scan root. Deterministic derived fields are materialized in each
DuckDB generation while time-relative age fields remain live. New or touched
rows also receive a stable local `file_id` from
device/inode identity, allowing deterministic facts to follow a rename without
turning the path into identity.

`facts` exposes the versioned key/value evidence table, while `image_facts`
pivots facts already harvested from thumbnail pixels: dimensions, orientation,
aspect ratio, average color, color family, brightness, saturation, blue share,
and a deterministic visual hash. The **blue** and **wide** lenses run against
that relation. They show analyzed/total coverage in the lens rail and request a
bounded, low-priority batch when opened; ordinary thumbnail generation fills
the same cache at essentially no extra decode cost. Facts are accepted only
when analyzer version, source size, and source mtime match the current file.
`projects` is another relation made entirely from catalog marker names and
returns real folders with a deterministic `project_type` such as `python`,
`node`, `rust`, or `go`. Existing catalogs migrate in place—none of these
features requires rebuilding the recursive tree.

Results containing `path` are live:
click to reveal and
double-click to enter/open. Query rows always replace the main listing while
keeping the real folder as their context. Aggregate rows
without a path become temporary, drillable relation folders; a simple
`GROUP BY extension` therefore browses like a folder of file types instead of
leaving you in a detached result grid. Those folders use stable representative
content for formats with useful previews (images, video, text, archives, and
Parquet); opaque formats keep a clear extension identity card instead of a
misleading generic-file mosaic. Browser Back returns from a drilled group to
its parent aggregate query. SQL and Terminal declare the same
`panel.group` and appear as modes of one workspace dock; third-party panels can
opt into another group without hard-coded UI changes. The disposable source
catalog lives at `~/.local/share/synchro/catalog.sqlite`; browsing never depends
on it. A background user service periodically checks whether a new DuckDB
generation is eligible: indexing must be finished, catalog writes must have
been quiet for ten minutes, and the active generation must be at least an hour
old. Eligible builds use two low-priority worker threads, build
`catalog.duckdb` beside the active generation, validate it, and atomically
promote it. Queries already in flight retain the previous inode; new queries
receive the promoted generation. A cross-process lock keeps multiple Synchro
windows from duplicating the build, and query failures fall back to the SQLite
bridge. Inspect or force this cache with
`synchro catalog shadow status` and `synchro catalog shadow rebuild --force`.

**Omaflow.** `:flow` opens the optional Omaflow companion as another workspace
panel. Synchro discovers the installed Omarchy plugin from its manifest, then
watches Omaflow's structured index, rule files, staging state, and activity log.
The selected rule is rendered as an adaptive flow graph with branching routes
and inspectable steps. **New** or **Revise** sends a plain-language request to
Omarchy's default agent; its staged rule, warnings, and full graph remain a
draft until explicitly installed or discarded. Dry runs are one step; a real
manual run and draft installation must each be armed and confirmed. Every
operation invokes the discovered Omaflow executable directly with fixed
arguments—never through a shell. Rules with an Omaflow `accepts` contract are
also exposed in Synchro's **do** layer only while the explicit selection
matches their MIME, suffix, path, project-marker, kind, and count constraints.
The mounted flow picker defaults to a dry run; a real run must be armed, then
receives the selection through a private bounded JSON context file.

Use **save** in the SQL panel to name a query (for example, “big webp files”);
it becomes a persistent location beside pinned folders and reopens against the
folder context it was saved from. The compact **lens** rail supplies useful
starting queries without hiding their SQL: kind, size, and age become
drillable pseudo-folders; largest is a direct ranked file view; projects finds
real directories from common repository/build markers; blue and wide use
locally derived image evidence. Selecting a lens writes
the complete query into the editor, where it can be changed or saved normally.

Location handlers may also ship **listing chrome** — `entryPoints.row` and
`entryPoints.thumb` — QML that paints on each visible row or grid thumb.
The core still owns the model (used / total / percent / detail roles). If no
chrome is mounted, FileList/FileGrid draw a first-party bar from `percent`.
`synchro.location.volumes` is the first client. Do not use this to restyle
every file in `$HOME`.

### Install (same shape as `omarchy plugin add`)

```bash
synchro handler add <git-url>          # lands disabled
synchro handler add <git-url> --enable
synchro handler list
synchro handler enable <id>
synchro handler disable <id>
synchro handler update [id]
synchro handler remove <id>
synchro handler validate <dir>
```

`--yes` skips the warning and the confirm prompts.

Third-party trees land in `~/.config/synchro/handlers/<id>/`. First-party
handlers ship in this repo under `handlers/` and load by default.

**Reserved namespaces:** `synchro.*` and `omarchy.*` are first-party only.
`synchro handler add` will reject them.

**Threat model:** handlers are unsandboxed user code — in-process QML or a
detached exec — same as Omarchy plugins. Review the repo before you enable it.

### Anatomy of a handler

A handler is a directory:

```
my.preview.notes/
  manifest.json
  Preview.qml          # only if the kind needs QML
```

`manifest.json` is the same idea as an Omarchy plugin manifest:

```json
{
  "schemaVersion": 1,
  "id": "acme.preview.notes",
  "name": "Notes peek",
  "version": "1.0.0",
  "author": "you",
  "license": "MIT",
  "description": "Peek a .note file as text.",
  "kinds": ["preview"],
  "priority": 70,
  "match": {
    "suffix": [".note"],
    "minItems": 1,
    "maxItems": 1,
    "host": "posix-local"
  },
  "entryPoints": {
    "preview": "Preview.qml"
  },
  "preview": {
    "runtime": "inprocess"
  }
}
```

Rules that match `omarchy-plugin-validate`:

- Entry points are relative, no leading `/`, no `..`, and must exist.
- No symlinks that escape the handler folder.
- `folder.replaceListing` is rejected (the host owns the listing model).
- `preview` / QML `action` need an entry point. `open` and exec `action` may
  be manifest-only.

Validate before you push:

```bash
synchro handler validate ./my.preview.notes
```

### In-process QML

Preview and action QML root on `HandlerSurface`:

```qml
import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root
    // injected: file, selection, host, manifest

    Text {
        anchors.fill: parent
        anchors.margins: Theme.space(8)
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
        text: {
            var p = root.host.readPreview(root.file, 65536)
            return p.text || p.error || ""
        }
    }
}
```

The list keeps Qt focus. Esc / Space / Q stay with Synchro.

- **Peek:** implement `peekKey(key, modifiers)` (return true if consumed)
  and/or set `peekFlickable`. `A`/`D` hop index ↔ file; `W`/`S` then call
  `peekKey`.
- **Do-layer params:** implement `commit()` (Enter runs it) and optional
  `actionKey(key, modifiers)`. The QML mounts on the right, under the look
  box. See `handlers/synchro.action.copy-as/`.

Useful `host` calls (read-only first-party policy — do not execute the file):

| Call | Use |
|---|---|
| `host.readPreview(file, maxBytes)` | First N bytes as text |
| `host.readParquet(file, maxRows)` | Footer schema + sample |
| `host.readDatabase(file, "sqlite"\|"duckdb", table, offset, limit)` | Table browser |
| `host.requestParquet(...)` / `parquetReady` | Non-blocking tagged Parquet read |
| `host.requestDatabase(...)` / `databaseReady` | Non-blocking tagged database read |
| `host.readArchive(file, maxEntries)` | Zip / tar / gzip members |
| `host.rasterUrl(file)` | Image URL Qt can paint (WebP rasterized) |
| `host.copyText(text)` | Clipboard |
| `host.runOpen(handlerId)` | Run a registered `open` handler |
| `host.closeAction()` | Leave the do-layer |
| `host.navigate(url)` / `host.reveal(url)` / `host.openExternal(url)` | Leave this file |

Theme tokens (`Theme.foreground`, `Theme.accent`, `Theme.space(8)`, …) follow
the active Omarchy theme. Do not import `qs.Commons` — that module belongs to
the shell.

Preview handlers can opt into the ambient Look panel by declaring
`"preview": { "runtime": "inprocess", "inline": "quick-app" }`. Synchro
mounts the handler's `HandlerSurface` directly in Look without panel chrome.
Use `host.requestParquet(...)` or `host.requestDatabase(...)` and their tagged
ready signals for expensive reads so stale work is ignored when selection
changes. Quick apps are opt-in because they instantiate while browsing.

### Exec peers

Omawrite does not need QML. The first-party wrapper is a manifest:

```json
{
  "kinds": ["open"],
  "entryPoints": {},
  "open": {
    "runtime": "exec",
    "exec": "omawrite %f",
    "tryExec": "omawrite"
  }
}
```

`tryExec` hides the handler when the binary is missing. Field codes follow the
usual desktop-entry shape (`%f` `%F` `%d`). This is how a future Quickshell
photo surface, or any Qt Quick peer, attaches: ship a `.desktop`-style exec
and a Synchro manifest, not a Nautilus Python extension.

### First-party handlers (in-tree)

| Id | Kind | Role |
|---|---|---|
| `synchro.preview.image` | preview | Raster peek |
| `synchro.preview.text` | preview | Highlighted text; unknown text-ish files fall through here |
| `synchro.preview.markdown` | preview | Rendered markdown (Enter still opens Omawrite) |
| `synchro.preview.pdf` | preview | PDF peek |
| `synchro.preview.video` | preview | Video peek |
| `synchro.preview.parquet` | preview | Schema + sample rows |
| `synchro.preview.sqlite` | preview | Interactive table / schema browser |
| `synchro.preview.duckdb` | preview | Same browser; needs `duckdb` on PATH |
| `synchro.preview.archive` | preview | Zip / tar / gzip member list (no extract) |
| `synchro.preview.folder` | preview | Folder peek listing |
| `synchro.open.xdg` | open | `xdg-open` fallback |
| `synchro.open.omawrite` | open | Markdown → Omawrite |
| `synchro.open.omacut` | open | Video → Omacut |
| `synchro.action.open-with` | action | App picker, mounted as do-layer params |
| `synchro.action.copy-as` | action | Copy path / URI / name (QML params) |
| `synchro.action.trash` | action | Core trash verb |
| `synchro.action.terminal` | action | `xdg-terminal-exec --dir=%d` |
| `synchro.action.agent` | action | Prompt Omarchy's default agent with this folder/selection + catalog access |
| `synchro.location.home` | location | `$HOME` |
| `synchro.location.recent` | location | Recents |
| `synchro.location.trash` | location | XDG trash |
| `synchro.location.volumes` | location | Disks / USB as a listing + root trees |
| `synchro.action.eject` | action | Unmount / power-off a removable volume |
| `synchro.panel.sql` | panel | `:sql` read-only DuckDB over `here`, `tree`, and `selection` |
| `synchro.panel.omaflow` | panel | `:flow` Omaflow rules, dry runs, and recent activity |

Office docs, audio, 7z, fonts, and a few more previews are still on the
[preview backlog](PREVIEW-BACKLOG.md).

## Omarchy agent bridge

Synchro works with Omarchy's configured system agent without requiring MCP.
The normal browser startup installs one shared `synchro` skill using Omarchy's
cross-harness convention (`~/.agents/skills`, Codex, Claude, and Pi). Existing
user-owned skill paths are preserved and reported rather than overwritten:

```bash
synchro agent doctor
synchro agent install
```

An Action Deck handoff inherits `SYNCHRO_SELECTION`, `SYNCHRO_CWD`, and the exact
running executable in `SYNCHRO_BIN` (so uninstalled dogfood builds work too).
Its first command is `"$SYNCHRO_BIN" agent context --compact`, which returns one
self-describing JSON document containing the complete selection, working
folder, catalog relations and fields, examples, result-safety rules, and
available hand-back commands. The same command is useful to agent scripts
outside the UI when those environment variables are present.

Synchro's persisted catalog is available without starting the QML UI. Agents
use `synchro agent query`, which runs the validated read-only query surface
through a narrow transient Omarchy user service; this lets SQLite use its WAL
files without granting the agent broad access outside its sandbox. Ordinary
shell scripts can continue to use `synchro query` directly. Output is always
JSON and includes the query engine (`duckdb-shadow` or `duckdb-sqlite`), shadow
revision gap, query scope, complete/incomplete index coverage, scan/catalog
timestamps, elapsed time, and truncation state:

```bash
synchro query --cwd "$PWD" --sql \
  "select name, kind, mb, modified_date, path from tree where kind = 'image' order by size desc" \
  --limit 100

# SQL can come from stdin; selection can be repeated.
printf '%s\n' 'select * from selection' | \
  synchro query --sql - --selection ./one.txt --selection ./two.txt

# Hand a result back as a navigable Synchro pseudo-folder.
synchro agent show --cwd "$PWD" --label "large images" --sql \
  "select name,path,kind,mb from tree where kind = 'image' order by size desc"
```

For extended integrations, `synchro mcp --stdio` exposes the same read-only
engine to MCP clients. MCP is optional: ordinary system-agent handoffs and
catalog work never depend on its registration. It
provides typed tools for name/path search, SQL, project discovery, file facts,
saved queries, and opening a result as a navigable SQL pseudo-folder in
Synchro. A local Codex registration looks like:

```toml
[mcp_servers.synchro]
command = "/usr/bin/synchro"
args = ["mcp", "--stdio"]
```

Use the actual installed path when dogfooding an uninstalled build. Raw SQL is
restricted to `SELECT`/`WITH`; the catalog database is attached read-only and
extension autoload/install are disabled. Agents should check
`catalog.coverageComplete` and `truncated` before treating a result as
exhaustive.

The built-in **Agent** action runs `omarchy agent prompt`, so it always follows
Omarchy's current default agent. It starts in the selected folder (or a selected
file's parent), passes the complete selection as a short-lived private JSON
manifest, and points the agent at the stable context contract above. This keeps
the handoff useful for Codex, Claude, Gemini, or whichever agent Omarchy selects,
without hard-coding an agent-specific launcher or MCP configuration.

## Opt-in FileChooser

Synchro can implement `org.freedesktop.impl.portal.FileChooser` as
`synchro --portal`. This is **opt-in**. gtk stays the fallback. Do **not** set
`default=synchro` (we do not implement Screenshot / ScreenCast).

1. Install the `.portal` + D-Bus service (`cmake --install` above), **or**
   dogfood from the build dir:

```bash
mkdir -p ~/.local/share/xdg-desktop-portal/portals ~/.local/share/dbus-1/services
cp packaging/portals/synchro.portal ~/.local/share/xdg-desktop-portal/portals/
printf '%s\n' \
  '[D-BUS Service]' \
  'Name=org.freedesktop.impl.portal.desktop.synchro' \
  "Exec=$PWD/build/synchro --portal" \
  > ~/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.synchro.service
```

2. Write a **replace-not-merge** user portals.conf (must restate `default=`).
   Copy `packaging/xdg-desktop-portal/hyprland-portals.conf.example` to
   `~/.config/xdg-desktop-portal/hyprland-portals.conf`:

```ini
[preferred]
default=hyprland;gtk
org.freedesktop.impl.portal.FileChooser=synchro;gtk
```

3. `systemctl --user restart xdg-desktop-portal.service`.

4. Optional: keep `--portal` up as a user service. Copy
   `packaging/systemd/synchro-portal.service` to `~/.config/systemd/user/`,
   then `systemctl --user daemon-reload && systemctl --user enable --now synchro-portal`.
   After a rebuild: `systemctl --user restart synchro-portal`.
   Logs: `journalctl --user -u synchro-portal -f`.

`omarchy file select` already talks to the frontend portal; it is not forked.
Chooser windows float via `packaging/omarchy/synchro.lua`.

## Super+Shift+F overlay

Omarchy still binds Super+Shift+F to Nautilus. To try Synchro locally, append
`packaging/omarchy/bindings-overlay.lua` to `~/.config/hypr/bindings.lua` and
reload Hyprland:

```lua
hl.unbind("SUPER + SHIFT + F")
hl.unbind("SUPER + ALT + SHIFT + F")
o.bind("SUPER + SHIFT + F", "Synchro", { launch = "synchro --new-window" })
o.bind("SUPER + ALT + SHIFT + F", "Synchro (cwd)",
  "synchro --new-window \"$(omarchy-cmd-terminal-cwd)\"")
```

`synchro` must be on `PATH`. After `cmake --install`, you can instead bind
`{ omarchy = "synchro" }` / `{ omarchy = "synchro-cwd" }`.

Window rules: copy `packaging/omarchy/synchro.lua` to
`~/.config/hypr/apps/synchro.lua` and `require("hypr.apps.synchro")` from
`~/.config/hypr/hyprland.lua`. Browser windows stay tiled; Open/Save/Select
titles float.

## What this does not take over

- **Folder MIME.** Nautilus remains `xdg-mime query default` for folders.
  Super+Shift+F stays Nautilus until you overlay it.
- **`/usr/share/omarchy/`.** Launchers, window rules, and the keybind live
  under `packaging/omarchy/` as a user overlay.
- **Session-default FileChooser.** User `hyprland-portals.conf` only.
- **`org.freedesktop.FileManager1`.** Still Nautilus.
- **`omarchy-shell`.** Synchro is not a Quickshell plugin and will not become
  one.

## Slotting

| In this stack | Later |
|---|---|
| Tiled browser, path bar, chips, command field | Tabs / split |
| Enter → handler `open` (Omawrite, xdg-open) | FileManager1 |
| Peek, do-layer, trash, recents, `?` name search | `??` / `rg` content search |
| Opt-in FileChooser (`synchro --portal`) | Session-default picker |
| `synchro handler add` | `folder.replaceListing` |
| Optional `synchro index pull` | Vendored GGUF, LLM chat, Snapper UI |

The long-form product spec is [DESIGN.md](DESIGN.md). Preview gaps:
[PREVIEW-BACKLOG.md](PREVIEW-BACKLOG.md).

## License

MIT. See [LICENSE](LICENSE).
