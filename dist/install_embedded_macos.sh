#!/usr/bin/env bash
set -euo pipefail

PACKAGE="${1:-}"

if [[ -z "$PACKAGE" || ! -f "$PACKAGE" ]]; then
  echo "Package not found: $PACKAGE" >&2
  exit 1
fi

TARGET_BASE=""
CANDIDATES=(
  "$HOME/Library/Application Support/TeamSpeak 3"
  "$HOME/Library/Application Support/TS3Client"
  "$HOME/.ts3client"
)

for candidate in "${CANDIDATES[@]}"; do
  if [[ -d "$candidate" ]]; then
    TARGET_BASE="$candidate"
    break
  fi
done

if [[ -z "$TARGET_BASE" ]]; then
  TARGET_BASE="$HOME/Library/Application Support/TeamSpeak 3"
fi

PLUGIN_DIR="$TARGET_BASE/plugins"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$PLUGIN_DIR"
/usr/bin/ditto -x -k "$PACKAGE" "$TMP_DIR"

if [[ ! -d "$TMP_DIR/plugins" ]]; then
  echo "Invalid .ts3_plugin package: plugins/ folder is missing." >&2
  exit 1
fi

/usr/bin/osascript -e 'tell application "TeamSpeak 3" to quit' >/dev/null 2>&1 || true
sleep 1

# Scoped removal: only files we are about to overwrite + known legacy
# variants. Other plugins (Soundboard, etc.) ship their own lib*.dylib
# bundle in the same plugins/ folder, so we never wildcard-delete *.dylib.
INCOMING_FILES=()
while IFS= read -r f; do INCOMING_FILES+=("$(basename "$f")"); done < <(find "$TMP_DIR/plugins" -maxdepth 1 -type f -print)

# Always remove EarShield's own binary + every legacy filename we have
# ever shipped under (so updates from older versions are clean).
LEGACY=(
    "libearshield_mac.dylib"
    "earshield_mac.dylib"
    "libearshield.dylib"
    "libvolumeleveler_mac.dylib"
    "libQt5Core.dylib"
    "libQt5Gui.dylib"
    "libQt5Network.dylib"
    "libQt5Widgets.dylib"
    "libglib-2.0.0.dylib"
    "libgthread-2.0.0.dylib"
    "libintl.8.dylib"
    "libmd4c.0.dylib"
    "libpcre2-16.0.dylib"
    "libpcre2-8.0.dylib"
    "libpng16.16.dylib"
    "libzstd.1.dylib"
)
for f in "${LEGACY[@]}" "${INCOMING_FILES[@]}"; do
    [ -n "$f" ] && rm -f "$PLUGIN_DIR/$f" 2>/dev/null
done

# Wipe legacy v6.0.5 / v6.0.6 Frameworks/ subdir layout, but only the
# Qt frameworks we used to ship there - not the whole subdir, in case
# another plugin uses a Frameworks/ folder for something else.
for q in Core Gui Network Widgets DBus PrintSupport; do
    rm -rf "$PLUGIN_DIR/Frameworks/Qt${q}.framework" 2>/dev/null || true
done
[ -d "$PLUGIN_DIR/Frameworks" ] && rmdir "$PLUGIN_DIR/Frameworks" 2>/dev/null || true

cp -R "$TMP_DIR/plugins/." "$PLUGIN_DIR/"

# Clear extended attributes only on the files we just dropped, never on
# the whole plugin folder (would touch other plugins' files too).
for f in "${INCOMING_FILES[@]}"; do
    [ -f "$PLUGIN_DIR/$f" ] && xattr -c "$PLUGIN_DIR/$f" >/dev/null 2>&1 || true
done

# Re-sign every Mach-O we just installed ad-hoc so Gatekeeper has a
# fresh local signature. Restricted to incoming files.
for f in "${INCOMING_FILES[@]}"; do
    [[ "$f" == *.dylib ]] || continue
    codesign --force -s - "$PLUGIN_DIR/$f" >/dev/null 2>&1 || true
done

echo "$PLUGIN_DIR"
