#!/bin/bash

set -euo pipefail

root=${1:?usage: smoke-installed-tree.sh PACKAGE_ROOT}
required=(
  usr/bin/synchro
  usr/bin/omarchy-launch-synchro
  usr/bin/omarchy-launch-synchro-cwd
  usr/share/applications/org.omarchy.synchro.desktop
  usr/share/icons/hicolor/scalable/apps/org.omarchy.synchro.svg
  usr/share/synchro/agent-skills/synchro/SKILL.md
  usr/share/synchro/handlers/synchro.open.xdg/manifest.json
  usr/share/synchro/handlers/synchro.panel.sql/Panel.qml
  usr/share/synchro/handlers/synchro.preview.folder/Preview.qml
  usr/share/xdg-desktop-portal/portals/synchro.portal
  usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.synchro.service
)

for path in "${required[@]}"; do
  if [[ ! -e "$root/$path" ]]; then
    echo "missing installed package path: $path" >&2
    exit 1
  fi
done

if ! grep -Fq 'Exec=/usr/bin/synchro --portal' \
  "$root/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.synchro.service"; then
  echo 'D-Bus service does not point at /usr/bin/synchro' >&2
  exit 1
fi

echo "installed tree smoke test passed (${#required[@]} required paths)"
