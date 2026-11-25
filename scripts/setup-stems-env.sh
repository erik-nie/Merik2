#!/bin/bash
# Eenmalige installatie van de Python-omgeving voor merik-stems.py (Demucs, torch, librosa, ...).
# Voortgang als regels "PROGRESS <0..1> <tekst>". Omgeving: ~/Library/Application Support/Merik/stems-env/venv
set -e
ENV="$HOME/Library/Application Support/Merik/stems-env"
VENV="$ENV/venv"
export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/Library/Python/3.9/bin:$HOME/.local/bin:$PATH"
mkdir -p "$ENV"
echo "PROGRESS 0.05 Looking for uv"
if ! command -v uv >/dev/null 2>&1; then
  echo "PROGRESS 0.10 Installing uv"
  python3 -m pip install --user --quiet uv
  export PATH="$(python3 -m site --user-base)/bin:$PATH"
fi
echo "PROGRESS 0.20 Creating Python 3.11 environment"
uv venv --python 3.11 "$VENV" >/dev/null
echo "PROGRESS 0.30 Installing torch, demucs, librosa (about 1 GB)"
uv pip install --python "$VENV/bin/python" torch torchaudio demucs librosa soundfile numpy scipy mido >/dev/null
command -v ffmpeg >/dev/null 2>&1 || { echo "ERROR ffmpeg not found (install with: brew install ffmpeg)"; exit 1; }
echo "PROGRESS 1.0 Done"
echo "DONE"
