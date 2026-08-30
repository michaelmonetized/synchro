#!/bin/bash

set -euo pipefail

fail() {
  echo "synchro-omarchy-setup: $*" >&2
  exit 1
}

with_menu=false
for arg in "$@"; do
  case "$arg" in
    --menu) with_menu=true ;;
    -h|--help)
      cat <<'EOF'
Usage: synchro-omarchy-setup [--menu]

Enable Synchro's per-user catalog service and install its system-agent skill.
Use --menu to also install the opt-in Super+Space search provider.
EOF
      exit 0
      ;;
    *) fail "unknown option: $arg" ;;
  esac
done

command -v systemctl >/dev/null || fail "systemctl is unavailable"
synchro_bin=${SYNCHRO_BIN:-$(command -v synchro || true)}
[[ -n $synchro_bin && -x $synchro_bin ]] || fail "synchro is not installed"

systemctl --user daemon-reload
systemctl --user enable --now synchro-indexd.service
systemctl --user is-active --quiet synchro-indexd.service \
  || fail "synchro-indexd.service did not start"

if ! "$synchro_bin" agent install --json >/dev/null; then
  # Development installs may already expose the same Synchro skill from a
  # source checkout. Preserve those user-owned links rather than replacing
  # them merely because the packaged source lives at a different path.
  for skill in \
    "$HOME/.agents/skills/synchro/SKILL.md" \
    "$HOME/.claude/skills/synchro/SKILL.md" \
    "$HOME/.codex/skills/synchro/SKILL.md" \
    "$HOME/.pi/agent/skills/synchro/SKILL.md"; do
    [[ -f $skill ]] || fail "the Synchro system-agent skill could not be installed"
    grep -Eq '^name:[[:space:]]*synchro[[:space:]]*$' "$skill" \
      || fail "an unrelated skill occupies ${skill%/SKILL.md}"
  done
fi

if $with_menu; then
  menu_installer=$(command -v synchro-omarchy-menu-install || true)
  [[ -n $menu_installer ]] || fail "the Omarchy menu installer is unavailable"
  "$menu_installer"
fi

echo "Synchro is ready: indexer enabled, catalog owner running, agent skill installed."
if ! $with_menu; then
  echo "Optional: run 'synchro-omarchy-setup --menu' for Super+Space file search."
fi
