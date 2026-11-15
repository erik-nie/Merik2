#!/usr/bin/env bash
# Maakt het bronarchief dat op de website staat (AGPL: de broncode moet beschikbaar zijn voor wie
# Merik Studio gebruikt). Zonder de website-map, video's en schermafbeeldingen, zodat het klein blijft.
#
#   scripts/make-source-archive.sh 1.0.1          # schrijft dist/Merik-Studio-1.0.1-source.zip van HEAD
#   scripts/make-source-archive.sh 1.0.1 v1.0.1   # van een bepaalde tag
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:?versie}"
REF="${2:-HEAD}"
mkdir -p dist
git archive --format=zip --prefix="Merik-Studio-$VERSION/" -o "dist/Merik-Studio-$VERSION-source.zip" "$REF" -- . \
  ':!site' ':!docs/images' ':!docs/demo' ':!assets/dmg-background*'
ls -l "dist/Merik-Studio-$VERSION-source.zip"
