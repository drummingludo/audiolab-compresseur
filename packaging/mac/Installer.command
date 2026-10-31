#!/bin/bash
# Installe « Audio Lab Compresseur » (VST3 + AU) pour l'utilisateur de ce Mac.
cd "$(dirname "$0")" || exit 1
DEST="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$DEST/VST3" "$DEST/Components"
rm -rf "$DEST/VST3/Audio Lab Compresseur.vst3" "$DEST/Components/Audio Lab Compresseur.component"
cp -R "Audio Lab Compresseur.vst3" "$DEST/VST3/"
cp -R "Audio Lab Compresseur.component" "$DEST/Components/"
# Le plugin vient d'Internet : on lève le blocage de macOS sur ces deux fichiers
xattr -dr com.apple.quarantine "$DEST/VST3/Audio Lab Compresseur.vst3" "$DEST/Components/Audio Lab Compresseur.component" 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Audio Lab Compresseur est installé."
echo "Relancez votre logiciel (Logic Pro, Ableton Live, FL Studio, Cubase, Reaper…)."
echo ""
read -n 1 -s -r -p "Appuyez sur une touche pour fermer cette fenêtre."
