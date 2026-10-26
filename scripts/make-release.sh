#!/usr/bin/env bash
# Builds the release folder ~/axell-signature-dojo-release: <name>.tar.gz (app, icon, install.sh, LICENSE, README,
# CHANGELOG), the extracted folder, SHA256SUMS.txt and release-notes.md. Run after scripts/build.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
V="$(grep -m1 -oP 'project\(AxellSignatureDojo VERSION \K[0-9.]+' CMakeLists.txt)"
N="AxellSignatureDojo-$V-linux-x86_64"
R="$HOME/axell-signature-dojo-release"
BIN="build/AxellSignatureDojo_artefacts/Release/Axell Signature Dojo"
[ -x "$BIN" ] || { echo "Build first: scripts/build.sh"; exit 1; }
"$BIN" --version | grep -q "$V" || { echo "The built app is not version $V"; exit 1; }
mkdir -p "$R"; rm -rf "$R/$N" "$R/$N.tar.gz"; mkdir "$R/$N"
cp "$BIN" "$R/$N/axell-signature-dojo"
cp Resources/images/icon.png "$R/$N/axell-signature-dojo.png"
mkdir -p "$R/$N/icons"; cp Resources/images/icons/*.png "$R/$N/icons/"
cp LICENSE README.md CHANGELOG.md "$R/$N/"
cat > "$R/$N/install.sh" <<'INSTALL'
#!/usr/bin/env bash
# Installs Axell Signature Dojo for this user (app, icon, menu entry). No administrator rights needed.
set -euo pipefail
cd "$(dirname "$0")"
DEST="$HOME/.local/share/AxellSignature/Dojo/app"
mkdir -p "$DEST" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/512x512/apps"
install -m 755 axell-signature-dojo "$DEST/axell-signature-dojo"
install -m 644 axell-signature-dojo.png "$HOME/.local/share/icons/hicolor/512x512/apps/axell-signature-dojo.png"
# every size, so no older small icon of the Dojo can win over the new one
for f in icons/axell-signature-dojo-*.png; do
  sz="${f##*-}"; sz="${sz%.png}"; mkdir -p "$HOME/.local/share/icons/hicolor/${sz}x${sz}/apps"
  install -m 644 "$f" "$HOME/.local/share/icons/hicolor/${sz}x${sz}/apps/axell-signature-dojo.png"
done
touch "$HOME/.local/share/icons/hicolor"
printf '[Desktop Entry]\nType=Application\nName=Axell Signature Dojo\nComment=Install, tune and set up the Axell Signature plugin suite\nExec=%s\nIcon=axell-signature-dojo\nCategories=AudioVideo;Audio;\nTerminal=false\n' "$DEST/axell-signature-dojo" > "$HOME/.local/share/applications/axell-signature-dojo.desktop"
echo "Installed. Open \"Axell Signature Dojo\" from your app menu."
INSTALL
chmod +x "$R/$N/install.sh"
( cd "$R" && tar --owner=0 --group=0 --numeric-owner --sort=name -cf - "$N" | gzip -n -9 > "$N.tar.gz" && sha256sum "$N.tar.gz" > SHA256SUMS.txt )
awk "/^## $V/{f=1;next} /^## /{f=0} f" CHANGELOG.md > "$R/release-notes.md"
printf '\nInstall: download %s.tar.gz, check it against SHA256SUMS.txt, extract, run ./install.sh. Linux x86_64, glibc 2.39+.\nSHA-256: %s\n' "$N" "$(cut -d' ' -f1 "$R/SHA256SUMS.txt")" >> "$R/release-notes.md"
echo "$R/$N.tar.gz"; cat "$R/SHA256SUMS.txt"
