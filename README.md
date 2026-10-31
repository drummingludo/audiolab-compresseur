# Audio Lab Compresseur

Le compresseur d'**Audio Lab** (« Chimiste du son », Transversal Studio) en plugin **VST3 / AU** pour Logic Pro, Ableton Live, FL Studio, Cubase, Reaper, Studio One, Bitwig…

- **Même son que l'outil web** : le moteur est un portage ligne à ligne du processeur `al-comp` d'`Audio_Lab.html`. Le banc de test compare les deux versions : sortie identique au bit près à 44,1 / 48 / 96 kHz.
- **Même interface** : potentiomètres à arc, vues Transfert / Activité / Mètres (VU à aiguille), presets pédagogiques avec ce qu'il faut entendre, snapshots A/B/C, Dry/Wet, Bypass, thème clair / sombre, fenêtre redimensionnable.
- Réglages : Threshold, Ratio (jusqu'à ∞:1), Knee, Attack (0,1–200 ms), Release (10–1000 ms), Makeup, Lookahead (0–10 ms, latence compensée), détection Peak / RMS, filtre de side-chain (HPF, LPF, cloche +6 dB) avec écoute du side-chain.

## Télécharger

Chaque fabrication produit deux archives (onglet **Actions** > dernière fabrication > *Artifacts*) :
- `AudioLabCompresseur-Mac.zip` : VST3 + AU, Apple Silicon et Intel, avec un installateur en un double-clic.
- `AudioLabCompresseur-Windows.zip` : VST3 64 bits.

Pour une version durable sur la page **Releases**, créer un tag `v…` : la fabrication y dépose alors les deux archives.
Mode d'emploi : fichier `LISEZ-MOI.txt` dans chaque archive. Pro Tools n'est pas compatible (format AAX).

## Fabrication

Chaque envoi sur `main` déclenche la fabrication sur des machines Mac et Windows de GitHub, puis la validation automatique (`auval` pour l'AU, `pluginval` niveau 10 pour tous les formats). Un tag `v…` publie la version.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Tests (Linux ou Mac) :

```
cmake -B build -DALC_TESTS=ON -DALC_STANDALONE=ON
cmake --build build --target ALCProcessorTest ALCRenderUI
python3 tests/verify_dsp.py     # moteur C++ ↔ code web d'Audio Lab (Node.js requis)
```

## Licence

Code sous **GNU AGPLv3** (voir `LICENSE`), comme la version libre du framework [JUCE](https://juce.com) qu'il utilise. Police Inter sous licence SIL OFL 1.1 (`Resources/Inter-OFL-LICENSE.txt`).

© Transversal Studio — Ludovic Diaz
