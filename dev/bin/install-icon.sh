#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_NAME="daia"
ICON_SRC="$REPO_ROOT/src/app/icon.png"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
DESKTOP_DIR="$HOME/.local/share/applications"
EXEC_PATH="$REPO_ROOT/build/src/app/daia"

mkdir -p "$ICON_DIR" "$DESKTOP_DIR"

cp "$ICON_SRC" "$ICON_DIR/$APP_NAME.png"

cat > "$DESKTOP_DIR/$APP_NAME.desktop" << EOF
[Desktop Entry]
Name=daia
Exec=$EXEC_PATH
Icon=$APP_NAME
Type=Application
Categories=Video;
EOF

update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

echo "Installed icon: $ICON_DIR/$APP_NAME.png"
echo "Installed desktop entry: $DESKTOP_DIR/$APP_NAME.desktop"
