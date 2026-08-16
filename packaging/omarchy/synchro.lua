-- Reference Hyprland rules (user overlay until packaged).
-- Copy to ~/.config/hypr/apps/synchro.lua and require("hypr.apps.synchro")
-- from ~/.config/hypr/hyprland.lua. Not installed into /usr/share/omarchy/.
-- Tiled by default — peek is in-window.
o.window("org.omarchy.synchro", { tag = "+synchro" })

-- Chooser-mode portal dialogs float, same as xdg-desktop-portal-gtk today.
o.window({ class = "org.omarchy.synchro", title = "^(Open|Save|Select|Choose).*" },
         { tag = "+floating-window" })
