#!/usr/bin/env bash
# Installs Axell Signature Dojo for this user: the app, its icon and a menu entry. No administrator rights needed.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="build/AxellSignatureDojo_artefacts/Release/Axell Signature Dojo"
[ -x "$BIN" ] || { echo "Build first: scripts/build.sh"; exit 1; }
DEST="$HOME/.local/share/AxellSignature/Dojo/app"
mkdir -p "$DEST" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/512x512/apps"
cp "$BIN" "$DEST/axell-signature-dojo"
cp Resources/images/icon.png "$HOME/.local/share/icons/hicolor/512x512/apps/axell-signature-dojo.png"
# every size, so no older small icon of the Dojo can win over the new one
for f in Resources/images/icons/axell-signature-dojo-*.png; do
  sz="${f##*-}"; sz="${sz%.png}"; mkdir -p "$HOME/.local/share/icons/hicolor/${sz}x${sz}/apps"
  cp "$f" "$HOME/.local/share/icons/hicolor/${sz}x${sz}/apps/axell-signature-dojo.png"
done
touch "$HOME/.local/share/icons/hicolor"
cat > "$HOME/.local/share/applications/axell-signature-dojo.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Axell Signature Dojo
Comment=Install, tune and set up the Axell Signature plugin suite
Exec=$DEST/axell-signature-dojo
Icon=axell-signature-dojo
Categories=AudioVideo;Audio;
Terminal=false
DESKTOP
echo "Installed: $DEST/axell-signature-dojo (menu: Axell Signature Dojo)"
