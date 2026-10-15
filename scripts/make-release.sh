#!/usr/bin/env bash
#
# Maakt een macOS-release (Apple silicon) van Merik:
#
#   1. Release-build in build-release/
#   2. FluidSynth en zijn libraries in de app stoppen (dylibbundler)
#   3. Ondertekenen (standaard ad-hoc; zie SIGN_IDENTITY hieronder)
#   4. DMG maken in dist/
#
# Gebruik:
#   scripts/make-release.sh                 # versie uit CMakeLists.txt
#   scripts/make-release.sh 0.1.0-alpha.1   # naam in het DMG-bestand
#
# Omgevingsvariabelen (optioneel):
#   SIGN_IDENTITY   "Developer ID Application: Naam (TEAMID)" voor een echte
#                   handtekening. Zonder dit wordt ad-hoc ondertekend.
#   NOTARY_PROFILE  naam van een `xcrun notarytool store-credentials` profiel;
#                   met SIGN_IDENTITY wordt de DMG dan ook genotariseerd.
#
# Vereist: cmake, pkg-config, fluid-synth en dylibbundler (brew install ...).

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

fail() { echo "Fout: $*" >&2; exit 1; }
step() { printf '\n==> %s\n' "$*"; }

[[ "$(uname -s)" == "Darwin" ]] || fail "dit script werkt alleen op macOS."
[[ "$(uname -m)" == "arm64" ]]  || fail "alleen Apple silicon wordt ondersteund."

for tool in cmake pkg-config dylibbundler hdiutil codesign otool vtool shasum; do
    command -v "$tool" >/dev/null || fail "'$tool' ontbreekt (brew install cmake pkg-config fluid-synth dylibbundler)."
done

# Versie: argument, anders het nummer uit CMakeLists.txt.
CMAKE_VERSION="$(sed -n 's/^project(Merik2 VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)"
VERSION="${1:-$CMAKE_VERSION}"
[[ -n "$VERSION" ]] || fail "geen versie gevonden."

APP_NAME="Merik"
BUILD_DIR="$ROOT/build-release"
DIST_DIR="$ROOT/dist"
APP="$DIST_DIR/$APP_NAME.app"
DMG="$DIST_DIR/$APP_NAME-$VERSION-arm64.dmg"

step "Merik $VERSION bouwen (Release, arm64)"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu)"

BUILT_APP="$BUILD_DIR/${APP_NAME}_artefacts/$APP_NAME.app"
[[ -d "$BUILT_APP" ]] || BUILT_APP="$BUILD_DIR/${APP_NAME}_artefacts/Release/$APP_NAME.app"
[[ -d "$BUILT_APP" ]] || fail "de gebouwde app is niet gevonden in $BUILD_DIR."

step "App klaarzetten in dist/"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
cp -R "$BUILT_APP" "$APP"

step "FluidSynth en andere libraries in de app stoppen"
mkdir -p "$APP/Contents/Frameworks"
dylibbundler -od -b \
    -x "$APP/Contents/MacOS/$APP_NAME" \
    -d "$APP/Contents/Frameworks/" \
    -p "@executable_path/../Frameworks/" >/dev/null

# Er mag niets meer naar Homebrew of /usr/local wijzen.
LEAKS="$( { otool -L "$APP/Contents/MacOS/$APP_NAME"; for f in "$APP"/Contents/Frameworks/*.dylib; do otool -L "$f"; done; } \
          | grep -E '/opt/homebrew|/usr/local' || true)"
[[ -z "$LEAKS" ]] || fail "er staan nog verwijzingen naar Homebrew in de app:\n$LEAKS"

# Licenties in de app zelf, zodat de DMG alleen app + Applications-pijl bevat.
cp LICENSE "$APP/Contents/Resources/LICENSE.txt"
cp THIRD-PARTY-LICENSES.md "$APP/Contents/Resources/THIRD-PARTY-LICENSES.md"

# Laagste macOS-versie waarop de app draait: de hoogste eis van de app en
# alle meegeleverde libraries (Homebrew bouwt die voor de huidige macOS).
MIN_OS="$( { vtool -show-build "$APP/Contents/MacOS/$APP_NAME"; for f in "$APP"/Contents/Frameworks/*.dylib; do vtool -show-build "$f"; done; } \
           | awk '/minos/ {print $2}' | sort -V | tail -1)"
echo "Minimale macOS-versie van deze build: ${MIN_OS:-onbekend}"

step "Ondertekenen"
if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    # Eerst de libraries, dan de app zelf.
    for f in "$APP"/Contents/Frameworks/*.dylib; do
        codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" "$f"
    done
    codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP"
else
    echo "Geen SIGN_IDENTITY: ad-hoc ondertekend (gebruikers moeten de app eenmalig 'Open' kiezen)."
    for f in "$APP"/Contents/Frameworks/*.dylib; do
        codesign --force --sign - "$f"
    done
    codesign --force --sign - "$APP"
fi
codesign --verify --strict "$APP"

step "DMG maken (met achtergrond en pijl naar Applications)"

# dmgbuild (Python) zet het venster netjes op zonder Finder/AppleScript.
VENV="$BUILD_DIR/dmg-venv"
if [[ ! -x "$VENV/bin/dmgbuild" ]]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --quiet dmgbuild
fi

# Achtergrond met 1x en 2x (Retina) in één TIFF.
tiffutil -cathidpicheck assets/dmg-background.png assets/dmg-background@2x.png -out "$DIST_DIR/dmg-background.tiff" >/dev/null

cat > "$DIST_DIR/dmg-settings.py" <<'PY'
import os

app = defines["app"]
background = defines["background"]

format = "UDZO"
files = [app]
symlinks = {"Applications": "/Applications"}
background = background
window_rect = ((200, 120), (640, 400))
icon_size = 112
text_size = 13
icon_locations = {
    os.path.basename(app): (170, 200),
    "Applications": (470, 200),
}
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
PY

"$VENV/bin/dmgbuild" -s "$DIST_DIR/dmg-settings.py" \
    -D app="$APP" -D background="$DIST_DIR/dmg-background.tiff" \
    "$APP_NAME $VERSION" "$DMG" >/dev/null
rm -f "$DIST_DIR/dmg-settings.py" "$DIST_DIR/dmg-background.tiff"

if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"

    if [[ -n "${NOTARY_PROFILE:-}" ]]; then
        step "Notariseren (kan enkele minuten duren)"
        xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$DMG"
    fi
fi

step "Klaar"
echo "App : $APP"
echo "DMG : $DMG ($(du -h "$DMG" | cut -f1)), macOS ${MIN_OS:-?} of nieuwer"
echo "SHA-256:"
shasum -a 256 "$DMG"
