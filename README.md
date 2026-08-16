# Synchro

Omarchy file OS: a themed Qt Quick browser, an opt-in OS file picker, and a
handler contract. Nautilus stays the packaged folder app. This repo does not
edit `/usr/share/omarchy/`.

## Build and run

```bash
cmake -S . -B build -G Ninja && cmake --build build && ./build/synchro
```

`--new-window` is accepted and is the default (new process). A positional
`[path]` opens that directory.

Optional develop install (puts `synchro` and the launchers on `~/.local/bin`,
and the portal/D-Bus files under `~/.local/share`):

```bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

`packaging/omarchy/` is a **user overlay**. CMake never installs those files
into `/usr/share/omarchy/`.

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

`omarchy file select` already talks to the frontend portal; it is not forked.
Chooser windows float via `packaging/omarchy/synchro.lua` (see below).

## Super+Shift+F overlay

Omarchy still binds Super+Shift+F to Nautilus. To use Synchro locally, append
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

## Handlers

Third-party handlers are a git repo with a `manifest.json`, same ritual as
`omarchy plugin add`. They run unsandboxed (in-process QML or a detached
exec). Review the code. Reserved `synchro.*` / `omarchy.*` ids are first-party
only.

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

`--yes` skips the warning and confirm prompts.

## Optional: semantic index

The GUI runs if `synchro-index` is missing. No auto-pull. Opt-in roots live in
`~/.config/synchro/index.json`.

```bash
synchro index pull --dry-run   # prints: ollama pull nomic-embed-text
synchro index pull             # explicit; never runs on first launch
synchro index start            # systemctl --user start synchro-index.service
synchro index status
synchro index reindex [root]
```

`'` in the command field queries the sidecar. `--portal` never starts it.

## What this does not take over

- **Folder MIME.** Nautilus remains `xdg-mime query default` for folders.
  Super+Shift+F stays Nautilus until you overlay it.
- **`/usr/share/omarchy/`.** Launchers, window rules, and the keybind live
  under `packaging/omarchy/` as a user overlay.
- **Session-default FileChooser.** User `hyprland-portals.conf` only.
- **`org.freedesktop.FileManager1`.** Still Nautilus.

## Slotting

| In this stack | Later |
|---|---|
| Tiled browser, path bar, chips, command field | Tabs / split |
| Enter → handler `open` (Omawrite, xdg-open) | FileManager1 |
| Peek, trash, recents, `?` name search | `??` / `rg` content search |
| Opt-in FileChooser (`synchro --portal`) | Session-default picker |
| `synchro handler add` | `folder.replaceListing` |
| Optional `synchro index pull` | Vendored GGUF, LLM chat, Snapper UI |
