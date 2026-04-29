#!/usr/bin/env bash
set -u

echo "============================================"
echo "  EarShield - Uninstaller (macOS)"
echo "============================================"
echo

read -r -p "Delete EarShield.ini config too? [N=keep, Y=full wipe] (default N): " WIPE
read -r -p "Create a backup on Desktop before deleting? [Y=yes, N=no] (default Y): " BACKUP
WIPE="${WIPE:-N}"
BACKUP="${BACKUP:-Y}"

echo
echo "Closing TeamSpeak 3 (if running)..."
osascript -e 'tell application "TeamSpeak 3" to quit' 2>/dev/null || true
sleep 1
pkill -9 -x ts3client            2>/dev/null || true
pkill -9 -if 'TeamSpeak 3'       2>/dev/null || true
sleep 1

TS3_BASE=""
for cand in \
    "$HOME/Library/Application Support/TeamSpeak 3" \
    "$HOME/Library/Application Support/TS3Client" \
    "$HOME/.ts3client"; do
    if [ -d "$cand" ]; then TS3_BASE="$cand"; break; fi
done
[ -z "$TS3_BASE" ] && TS3_BASE="$HOME/Library/Application Support/TeamSpeak 3"

PLUGINS_DIR="$TS3_BASE/plugins"
CONFIG_INI="$PLUGINS_DIR/EarShield.ini"
SETTINGS_DB="$TS3_BASE/settings.db"

if [[ "$(echo "$BACKUP" | tr '[:lower:]' '[:upper:]')" == "Y" ]]; then
    STAMP="$(date +%Y%m%d_%H%M%S)"
    BACKUP_DIR="$HOME/Desktop/EarShield_Backup_$STAMP"
    [ -d "$HOME/Desktop" ] || BACKUP_DIR="$HOME/EarShield_Backup_$STAMP"
    echo "Backing up to $BACKUP_DIR"
    mkdir -p "$BACKUP_DIR"
    [ -d "$PLUGINS_DIR" ]  && cp -R "$PLUGINS_DIR"  "$BACKUP_DIR/plugins"      2>/dev/null || true
    [ -f "$SETTINGS_DB" ]  && cp    "$SETTINGS_DB"  "$BACKUP_DIR/settings.db"  2>/dev/null || true
    [ -f "$CONFIG_INI" ]   && cp    "$CONFIG_INI"   "$BACKUP_DIR/EarShield.ini" 2>/dev/null || true
fi

echo "Removing EarShield binaries..."
rm -f "$PLUGINS_DIR/libearshield_mac.dylib"     2>/dev/null
rm -f "$PLUGINS_DIR/earshield_mac.dylib"        2>/dev/null
rm -f "$PLUGINS_DIR/libearshield.dylib"         2>/dev/null
rm -f "$PLUGINS_DIR/libvolumeleveler_mac.dylib" 2>/dev/null

echo "Removing bundled Qt frameworks..."
for q in Core Gui Network Widgets DBus PrintSupport; do
    rm -rf "$PLUGINS_DIR/Frameworks/Qt${q}.framework" 2>/dev/null
done
rmdir "$PLUGINS_DIR/Frameworks" 2>/dev/null || true

echo "Removing log files..."
rm -f "$TS3_BASE"/earshield*.log 2>/dev/null

if command -v sqlite3 >/dev/null 2>&1; then
    echo "Cleaning settings.db Plugins entries..."
    sqlite3 "$SETTINGS_DB" "DELETE FROM Plugins WHERE value LIKE '%EarShield%' OR value LIKE '%volumeleveler%'; VACUUM;" 2>/dev/null || true
else
    echo "[warning] sqlite3 not found in PATH - skipping settings.db cleanup."
fi

if [[ "$(echo "$WIPE" | tr '[:lower:]' '[:upper:]')" == "Y" ]]; then
    echo "Removing EarShield.ini..."
    rm -f "$CONFIG_INI" 2>/dev/null
else
    echo "Keeping EarShield.ini."
fi

echo
echo "Done."
read -r -p "Press Enter to close..." _
