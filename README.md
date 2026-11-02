# Audio Lab Compressor

Le compresseur d'**Audio Lab** (« Chimiste du son », Transversal Studio) en plugin **VST3 / AU** pour Logic Pro, Ableton Live, FL Studio, Cubase, Reaper, Studio One, Bitwig…, et **AAX** pour Pro Tools (version de test non signée, voir plus bas).

- **Même son que l'outil web** : le moteur est un portage ligne à ligne du processeur `al-comp` d'`Audio_Lab.html`. Le banc de test compare les deux versions : sortie identique au bit près à 44,1 / 48 / 96 kHz.
- **Interface à la charte Transversal (V2)** : potentiomètres à arc, écran Transfert / Activité / Vu-Mètres (VU à cadran clair rétroéclairé), réglages avancés repliables, thème sombre, police Poppins, fenêtre redimensionnable. Dry/Wet et Bypass depuis le logiciel hôte.
- Réglages : Threshold, Ratio (jusqu'à ∞:1), Knee, Attack (0,1–200 ms), Release (10–1000 ms), Makeup, Lookahead (0–10 ms, latence compensée), détection Peak / RMS, filtre de side-chain (HPF, LPF, cloche +6 dB) avec écoute du side-chain.

## Télécharger

Chaque fabrication produit deux archives (onglet **Actions** > dernière fabrication > *Artifacts*) :
- `AudioLabCompressor-Mac.zip` : VST3 + AU, Apple Silicon et Intel, avec un installateur en un double-clic.
- `AudioLabCompressor-Windows.zip` : VST3 64 bits.

Pour une version durable sur la page **Releases**, créer un tag `v…` : la fabrication y dépose alors les deux archives.
Mode d'emploi : fichier `LISEZ-MOI.txt` dans chaque archive.

### Pro Tools (AAX)

Chaque archive contient aussi `Audio Lab Compressor.aaxplugin` (Mac universel, Windows 64 bits), fabriqué avec le SDK AAX fourni par JUCE (GPL v3). Il n'est **pas encore signé** par PACE : il ne se charge que dans **Pro Tools Developer**. Particularité AAX : la latence annoncée est fixe (10 ms, lookahead maximal), comme l'exige Pro Tools ; un retard interne complète le lookahead choisi, le son est identique à l'échantillon près au VST3 décalé.

## Fabrication

Chaque envoi sur `main` déclenche la fabrication sur des machines Mac et Windows de GitHub, puis la validation automatique (`auval` pour l'AU, `pluginval` niveau 10 pour VST3 et AU ; pour l'AAX, contrôle de structure, d'architectures et du point d'entrée — pluginval ne sait pas charger l'AAX). Un tag `v…` publie la version.

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

Code sous **GNU AGPLv3** (voir `LICENSE`), comme la version libre du framework [JUCE](https://juce.com) qu'il utilise. Police Poppins sous licence SIL OFL 1.1 (`Resources/Poppins-OFL-LICENSE.txt`).

© Transversal Studio — Ludovic Diaz
