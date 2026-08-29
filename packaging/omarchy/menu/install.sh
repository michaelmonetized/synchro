#!/bin/bash

set -euo pipefail

fail() {
  echo "synchro-omarchy-menu-install: $*" >&2
  exit 1
}

command -v omarchy >/dev/null || fail "Omarchy is not installed"
command -v patch >/dev/null || fail "patch is required"

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
data_dir="$script_dir"
if [[ ! -f $data_dir/omarchy-menu.patch ]]; then
  prefix=$(cd "$script_dir/.." && pwd)
  data_dir="$prefix/share/synchro/omarchy-menu"
fi
[[ -f $data_dir/omarchy-menu.patch ]] || fail "integration patch was not found"
[[ -f $data_dir/SynchroSearch.qml ]] || fail "SynchroSearch.qml was not found"

clone_id="${USER:-$(id -un)}.menu"
plugin_dir="$HOME/.config/omarchy/plugins/$clone_id"
if [[ ! -d $plugin_dir ]]; then
  omarchy plugin clone omarchy.menu
fi

manifest="$plugin_dir/manifest.json"
[[ -f $manifest ]] || fail "$plugin_dir is not an Omarchy plugin"
jq -e '.omarchy.clonedFrom == "omarchy.menu"' "$manifest" >/dev/null \
  || fail "$clone_id is not a clone of omarchy.menu"

if ! rg -q 'SynchroSearch \{' "$plugin_dir/Menu.qml"; then
  patch --dry-run --forward -p1 -d "$plugin_dir" < "$data_dir/omarchy-menu.patch" >/dev/null \
    || fail "this Omarchy menu version does not match the Synchro provider patch"
  patch --forward -p1 -d "$plugin_dir" < "$data_dir/omarchy-menu.patch"
fi
install -m 0644 "$data_dir/SynchroSearch.qml" "$plugin_dir/SynchroSearch.qml"

omarchy-plugin-enable "$clone_id" >/dev/null
omarchy restart shell
echo "Synchro search enabled in Super+Space through $clone_id"
