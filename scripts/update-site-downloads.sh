#!/usr/bin/env bash
# Zet de installers van een gepubliceerde GitHub-release in site/download/, zodat de website
# (Cloudflare) ze zelf serveert, en past de downloadlinks en het versienummer in site/index.html aan.
#
# Gebruik (na het publiceren van een release, met de GitHub CLI ingelogd):
#
#   scripts/update-site-downloads.sh v0.7.3
#   git add site && git commit -m "Website: downloads 0.7.3" && git push
#
# Zonder tag pakt het script de nieuwste gepubliceerde release.
set -euo pipefail
cd "$(dirname "$0")/.."

TAG="${1:-$(gh release view --repo erik-nie/Merik2 --json tagName -q .tagName)}"
VERSION="${TAG#v}"
OLD="$(sed -nE 's/.*id="ver"[^>]*>([^<]+)<.*/\1/p' site/index.html)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
gh release download "$TAG" --repo erik-nie/Merik2 --dir "$TMP" \
  --pattern '*arm64.dmg' --pattern '*intel.dmg' --pattern '*windows-x64-setup.exe'

for f in "$TMP"/*; do
  size=$(wc -c < "$f")
  if (( size > 25 * 1024 * 1024 )); then
    echo "$f is groter dan 25 MiB: Cloudflare accepteert dat niet als los bestand." >&2
    exit 1
  fi
done
rm -rf site/download
mkdir -p site/download
mv "$TMP"/* site/download/

if [[ -n "$OLD" && "$OLD" != "$VERSION" ]]; then
  sed -i.bak -e "s/Merik-$OLD-/Merik-$VERSION-/g" \
             -e "s/id=\"ver\"\([^>]*\)>$OLD</id=\"ver\"\1>$VERSION</" \
             -e "s#releases/tag/v$OLD#releases/tag/v$VERSION#g" site/index.html
  rm -f site/index.html.bak
  echo "Links bijgewerkt van $OLD naar $VERSION. Vergeet het blok 'What's new' niet aan te passen."
fi
ls -l site/download
