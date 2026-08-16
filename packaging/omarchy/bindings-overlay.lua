-- User overlay for Super+Shift+F. Append to ~/.config/hypr/bindings.lua
-- (or paste these four lines). Does not edit /usr/share/omarchy/.
-- Nautilus stays the packaged default until FileChooser is the session picker.

hl.unbind("SUPER + SHIFT + F")
hl.unbind("SUPER + ALT + SHIFT + F")
o.bind("SUPER + SHIFT + F", "Synchro", { launch = "synchro --new-window" })
o.bind("SUPER + ALT + SHIFT + F", "Synchro (cwd)",
  "synchro --new-window \"$(omarchy-cmd-terminal-cwd)\"")

-- Once omarchy-launch-synchro{,-cwd} are on PATH (cmake --install, or copy):
-- o.bind("SUPER + SHIFT + F", "Synchro", { omarchy = "synchro" })
-- o.bind("SUPER + ALT + SHIFT + F", "Synchro (cwd)", { omarchy = "synchro-cwd" })
