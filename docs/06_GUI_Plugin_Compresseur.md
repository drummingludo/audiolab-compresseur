# 06 — Interface du plugin « Audio Lab Compresseur » (VST3 / AU)

**Statut : mise en page validée par Ludovic le 31/10/2026 ; habillage à la charte UI V2 des plugins (fiche 02, 01/11/2026) depuis la version 1.1.0.** Référence visuelle : maquette Design « Audio Lab Compresseur — proportions des vues », planches « Charte — sombre » (replié, déplié, trois vues).
Ce document fait foi pour la mise en page du plugin ; les couleurs, la police et les composants suivent la charte `02 — Charte UI des plugins Audio Lab` (V2).

---

## 1. Principes

- **Une seule règle d'espacement : 24 px.** Marge extérieure de 24 px sur les 4 côtés, 24 px entre deux potentiomètres voisins et entre deux rangées. 16 px seulement entre l'écran et la rangée du bas, et entre la rangée du bas et le panneau avancé (ces éléments vont ensemble).
- **L'essentiel visible, le reste replié.** 4 gros potentiomètres + l'écran. Les réglages fins (Knee, Lookahead, Détection, Side-chain) sont dans un panneau caché par défaut.
- **Un écran carré unique (270 × 270)** qui montre une vue à la fois : Transfert, Activité ou Vu-Mètres. Les graphes remplissent tout le carré.
- **Le « matériel » réservé aux mesures** (charte V2) : commandes sobres (capuchons gris, repère, léger halo sur les arcs) ; seul le VU-mètre a un rendu d'appareil (cadran clair rétroéclairé, verre, ombre d'aiguille). Barres de niveau à bout droit.
- **Pas de chiffres inutiles** : pas de texte « GR » sur la vue Transfert, aucun chiffre sur la vue Activité.

## 2. Dimensions (en px, à l'échelle 100 %)

| Élément | Valeur |
|---|---|
| Fenêtre repliée | **580 × 362** |
| Fenêtre dépliée | **580 × 640** depuis la 2.0.0 (le panneau ajoute 278 px en bas ; 580 × 504 en 1.x) |
| Marge extérieure | 24 |
| Écran (carré) | x 286, y 24, 270 × 270, coins 10 |
| Rangée du bas | y 310, hauteur 28 |
| Panneau avancé | x 24, y 354, 532 × 262, coins 12 (7 lignes ; 532 × 126 en 1.x) |
| Redimensionnement | proportionnel, 60 % à 200 %, mémorisé |

## 3. Potentiomètres principaux (2 × 2)

| | Colonne 1 | Colonne 2 |
|---|---|---|
| Rangée 1 | THRESHOLD (teal) | RATIO (teal) |
| Rangée 2 | ATTACK (orange) | RELEASE (orange) |

- Centres : x = 75,6 et 202,8 ; y = 75,6 et 223.
- Arc : rayon 49, **épaisseur 3,2**, ouverture 270° (de 7 h 30 à 4 h 30). Rail #3E3F3D à bouts ronds (éclairci au survol).
- Arc coloré : **dégradé** du départ à la valeur (teal #0A9396 → #79AFB0 ; orange #BB3E03 → #EAA93D), **bout mobile coupé droit**, pastille de départ ronde. **Halo léger** de la même couleur (flou 2 px, opacité ≈ 55 %), renforcé pendant le réglage.
- Threshold : l'arc part de 0 dB (à droite) et descend vers la valeur. Les autres partent du minimum.
- Capuchon : disque #373837 de 74 px, liseré de lumière en haut, ombres 0 1 2 rgba(0,0,0,.45) + 0 8 20 rgba(0,0,0,.5).
- **Repère** : trait fin de 1,5 px, couleur de fin de l'arc, collé au bord du capuchon (du rayon 37 vers le rayon 29), dirigé vers la valeur, bout droit.
- Valeur au centre : Poppins 14 px semi-gras (chiffres en cellules fixes) ; unité dessous 11 px #A5A6A6.
- Nom : majuscules 11 px semi-gras, interlettrage 0,06 em, à 58 px sous le centre. Les noms de la 2ᵉ rangée tombent exactement sur le bas de l'écran (y 294).
- Manipulation : glisser vertical (Maj = fin), molette, flèches du clavier, double-clic = valeur par défaut, bulle d'aide « Pourquoi ? ».

## 4. Rangée du bas (y 310, hauteur 28)

- **À gauche : « Réglages avancés »** avec chevron (12 px). Le chevron est aligné sur la marge de 24 px. Fermé : chevron « › », texte #C9C9CA. Ouvert : chevron « ⌄ », texte #F9F9FB.
- **À droite, sur la largeur exacte de l'écran (x 286 → 556), écart de 8 px entre les deux :**
  - **Sélecteur de vue** « Transfert | Activité | Vu-Mètres », **en icônes depuis la 1.2.1** (courbe coudée = Transfert, forme d'onde = Activité, trois barres = Vu-Mètres ; le nom s'affiche en bulle d'aide) : bande #2A2B29, coins 9, marge intérieure 3 px, 3 segments de largeur égale, texte 10 px #C9C9CA. Segment choisi : pastille #005F73, texte #F9F9FB en 500, coins 7, ombre 0 1 3 rgba(0,0,0,.18).
  - **Petit Make-up** (bouton de 28 px, coin bas droit) : arc teal secondaire #79AFB0 rayon 11,5, épaisseur 2,4, capuchon #373837 de 16 px, repère, halo. À sa gauche, deux lignes alignées à droite : « MAKEUP » (9 px semi-gras, interlettrage 0,04 em) et la valeur « +4 dB » (10 px, #C9C9CA).

## 5. Écran carré (270 × 270)

Fond « Surface » #2A2B29, bord 1 px #3E3F3D, coins 10. Le contenu est découpé à l'intérieur des coins arrondis.

### Vue Transfert
- Graphe −60 → 0 dB sur les deux axes, **il remplit tout le carré** (1 dB en entrée = 1 dB en sortie à l'écran).
- Grille tous les 12 dB (#3E3F3D). Diagonale 1:1 en pointillés #757675 (4/4).
- Zone de knee : bande teal 7 % de part et d'autre du seuil ; ligne de seuil verticale pointillée teal #0A9396 **à 100 %**.
- Courbe teal #0A9396 2,5 px + remplissage dégradé teal 25 % → 2 %.
- Point en direct orange #EAA93D (pastille ≈ 12 px bord #1D1E1C + halo) avec pointillés orange 50 % vers les axes.
- **Chiffres à l'intérieur, petits** : Poppins 8 px #A5A6A6. Sortie le long du bord gauche (0, −12, −24, −36, −48), juste au-dessus de chaque ligne. Entrée le long du bord bas (−48, −36, −24, −12, 0), juste à gauche de chaque ligne.
- **Pas de texte « GR ».**

### Vue Activité
- Défilement de l'enveloppe, **remplit tout le carré, aucun chiffre**.
- Entrée en gris doux (#A5A6A6, remplissage 18 %), sortie en teal (#0A9396, remplissage 35 %), symétriques autour du centre.
- Grille −12 / −24 / −36 dB (#3E3F3D). Seuil en pointillés teal (6/4) en haut et en bas.

### Vue Vu-Mètres
- **Cadran VU** : x 14, y 14, 242 × 172, coins 8, **cadran clair #F9F9FB rétroéclairé** encastré (ombre intérieure : décalage 4 px vers le bas, flou 5,5, 50 %) ; halo de lampe ambre #EAA93D sous le pivot (34 % → 12 % → 0), **éteint en bypass** ; bords assombris (0 → 9 %) ; reflet de verre diagonal (blanc 85 % → 18 %, coupé net) qui ne touche pas les chiffres.
- VU à aiguille de réduction de gain : graduations −20 … +3 (grandes #1D1E1C, petites #969697, chiffres #686868), zone rouge #9B2226 au-dessus de 0, « VU » (gras) et « GAIN REDUCTION » (semi-gras, #686868). Aiguille #1D1E1C de 1,6 px avec ombre portée, pivot en relief visible en bas du cadran. **Balistique VU normalisée : 99 % en 300 ms, dépassement ≈ 1,3 %.**
- Dessous, 3 barres horizontales **In / GR / Out** : piste 170 × 8 (x 46 ; y 200, 222, 244), coins 4, fond #1D1E1C, bord #3E3F3D. Nom à gauche (10 px semi-gras, #A5A6A6), valeur à droite (9 px #C9C9CA, chiffres en cellules fixes).
  - In / Out : dégradé teal secondaire #79AFB0 → ambre #EAA93D (82 %) → rouge #BC6D71, de gauche à droite. **GR : orange #EAA93D, part de la droite (0 dB) et s'allonge vers la gauche**, dans le même sens que l'aiguille du VU (demande de Ludovic, 01/11). Repère de seuil teal #0A9396 sur In.
  - **Bout mobile coupé droit**, départ arrondi.

## 6. Panneau « Réglages avancés » (caché par défaut)

- Fond « Surface » #2A2B29, coins 12, marge intérieure 16, **2 colonnes** (écart 32), **7 lignes de 26 px** (écart 8) depuis la 2.0.0.
- Colonne gauche (dynamique, détecteur) : **Knee** (curseur, 0–20 dB), **Lookahead** (curseur, ms), **Détection** (Peak | RMS), **RMS time**, **Hold**, **Release** (Fixed | Auto), **True peak** (Off | On).
- Colonne droite (side-chain, stéréo, qualité) : **Side-chain** (menu Off / HPF / LPF / Bell +6, 60 px) + bouton **« Écouter »** (icône casque), **SC Freq** (Hz, échelle log), **SC Q**, **SC source** (Internal | External), **Stereo link**, **Mode** (Stereo | Mid-Side), **Oversampling** (Off | 2× | 4×).
- Les trois premières lignes de chaque colonne sont celles de la 1.x, inchangées ; les lignes 2.0.0 s'ajoutent dessous. Tous les sélecteurs commencent à 78 px du bord de la colonne, sauf « Oversampling » (étiquette plus longue : 95 px).
- Étiquettes 12 px #C9C9CA sur 68 px (+ 10 px d'écart) ; valeurs 12 px #F9F9FB alignées à droite sur 48 px (chiffres en cellules fixes).
- Curseurs : piste 4 px #3E3F3D, bouton #373837 de 18 px (liseré + ombre), **partie avant le bouton remplie** avec halo léger : Knee teal en dégradé, Lookahead orange plein, SC Freq / SC Q teal secondaire ; rien de coloré à la valeur minimale.
- Menu Side-chain (60 px) et bouton « Écouter » : fond #373837, coins 8 ; « Écouter » actif = #005F73. Sélecteur Détection : bande #3E3F3D, segment choisi #005F73. Écrire « Side-chain » avec un trait d'union normal.
- SC Freq et SC Q grisés (gris doux #A5A6A6) quand le side-chain est sur Off. Depuis la 2.0.0, sont aussi grisés (toujours modifiables) les réglages sans effet dans la situation : en ∞:1, Knee, Détection, RMS time et Mode ; RMS time en Peak ; True peak quand le ratio n'est pas ∞ ou que le lookahead est sous 1 ms. Un sélecteur grisé a ses textes en gris doux et sa pastille en « Capuchon ».
- Ouvrir le panneau agrandit la fenêtre de 278 px (142 px en 1.x) ; le refermer la réduit. L'état ouvert/fermé est mémorisé avec la session.

## 7. Couleurs, police, thème (charte V2)

Thème **sombre uniquement** (plus de bascule clair / sombre). Police **Poppins** embarquée (400, 500, 600, 700). Couleurs : fiche 02 (V2) — fond #1D1E1C, surface #2A2B29, capuchon #373837, rails #3E3F3D, texte #F9F9FB / #C9C9CA / #A5A6A6 ; teal #0A9396 → #79AFB0 (niveau, seuil, courbe), teal secondaire #79AFB0 (gain, niveaux, filtre side-chain), sélection #005F73 ; orange #BB3E03 → #EAA93D (temps, réduction de gain) ; rouge #9B2226 sur cadran clair, #BC6D71 sur fond sombre.

Bypass (depuis le logiciel hôte) : la lampe du VU s'éteint ; les réglages restent accessibles.

## 8. Ce qui a été retiré volontairement

- L'en-tête (nom, presets, A/B/C, Dry/Wet, Bypass) : le Bypass et les presets se font depuis le logiciel hôte. Le paramètre Dry/Wet existe toujours (automation).
- Les barres In / GR / Out à côté de la courbe (elles sont dans la vue Vu-Mètres).
- Le texte « GR » sur la vue Transfert et tous les chiffres sur la vue Activité.
- Knee en gros bouton (déplacé dans les réglages avancés).
- Make-up en gros bouton (remplacé par le petit bouton en bas à droite).

## 9. Version 2.0.0 (03/11/2026) : qualité audio de niveau professionnel

Mise en œuvre de l'audit qualité (fiche 15, partie Compresseur). La face principale (580 × 362) ne change pas ; tous les nouveaux réglages sont dans « Advanced settings ». Réglages par défaut : le compresseur sonne **exactement** comme la 1.2.1 et comme l'outil web (vérifié au bit près, voir § 9.6).

### 9.1 Ce qui change

1. **Ratio ∞ = vrai limiteur.** Potentiomètre Ratio au maximum (∞:1) : plus un compresseur 100:1, mais un limiteur à plafond garanti. **Plafond = Threshold + Makeup** (ex. preset « Brickwall limiter » : −10 + 6 = −4 dBFS). Le Lookahead règle la fenêtre d'anticipation (la montée du gain est une rampe douce en S sur toute la fenêtre), le Release règle le retour. En ∞ : Attack et Knee sont sans effet, la détection est toujours en crête (Peak) et sur Left / Right. Passage d'un ratio à ∞ et retour par fondu enchaîné de 20 ms : aucun clic.
   - Plafond **exact sur les échantillons** quel que soit le Lookahead (même 0 ms : le gain agit alors sur l'échantillon lui-même).
   - **True peak** (On par défaut) : le limiteur voit aussi les crêtes entre les échantillons (interpolation ×8, brique commune `Limiter.h`, marge 0,05 dB). Actif dès 1 ms de Lookahead (en dessous, seul le plafond sur les échantillons est garanti).
   - Avec un filtre de side-chain ou un side-chain externe, le limiteur obéit au signal de détection : le plafond porte alors sur ce signal, pas sur la sortie (comme tout limiteur à clé).
2. **Side-chain externe** : bus d'entrée optionnel « Sidechain » (stéréo, ou mono pour Pro Tools). Réglage **SC source** : Internal (le compresseur écoute sa propre entrée) ou External (il écoute le side-chain choisi dans le logiciel). External sans side-chain branché : détection interne, et un message le dit. Le filtre du side-chain (HPF / LPF / Bell) et le bouton Listen s'appliquent aussi au side-chain externe. Passage Internal ↔ External par fondu de 20 ms.
3. **Détecteur** : Stereo link 0 à 100 % (mélange des réductions de gain : à 50 %, la voie la plus calme reçoit la moitié de la réduction de l'autre) ; Mode Stereo ou Mid-Side (détection et gain sur le milieu et les côtés ; changement par fondu) ; Hold (la réduction reste en place avant le release) ; Release Fixed / Auto (deux constantes de temps, voir § 9.2) ; fenêtre RMS réglable (3 ms par défaut, comme avant).
4. **Suréchantillonnage** Off / 2× / 4× (brique commune `Oversampler.h`, phase linéaire) : détecteur et gain tournent au rythme haut, moins de repliement quand le gain bouge vite. Il ne sert pas au limiteur ∞ (qui reste au rythme de base pour garantir son plafond exact). Changement par un court fondu de sortie (5 ms), un silence le temps de remplir le nouveau chemin, puis un fondu d'entrée (5 ms) : environ 13 ms en tout avec un lookahead de 0, jusqu'à 45 ms avec 10 ms de lookahead en ∞:1. La latence change ; réglage non automatisable.
5. **Lissage** : Threshold, Ratio, Knee, Makeup et Stereo link sont lissés (rampe de 20 ms arrondie, qui retombe exactement sur la valeur demandée). Changer le Lookahead pendant la lecture ne fait plus de clic : fondu enchaîné de 20 ms entre l'ancien et le nouveau retard, sur le son traité **et** sur le son sec du Dry/Wet.

### 9.2 Nouveaux réglages (panneau Advanced settings)

| Réglage affiché | Identifiant | Plage | Défaut | Effet |
|---|---|---|---|---|
| True peak | `truePeak` | Off / On | On | Limiteur ∞ : plafond aussi entre les échantillons (dès 1 ms de Lookahead) |
| SC source | `scSource` | Internal / External | Internal | Source de la détection |
| Stereo link | `stereoLink` | 0 à 100 % | 100 % | 100 % = même réduction sur les deux voies (comme en 1.x) |
| Mode | `stereoMode` | Stereo / Mid-Side | Stereo | Travail en Left / Right ou en Mid / Side |
| Hold | `hold` | 0 à 500 ms (course quadratique) | 0 ms | Maintien de la réduction avant le release |
| Release | `autoRelease` | Fixed / Auto (Off / On dans l'automation) | Fixed | Auto : après une crête brève, retour 3 fois plus rapide que le Release ; après une compression longue, 3 fois plus lent (deux détecteurs en parallèle, on garde le plus fort) |
| RMS time | `rmsWindow` | 1 à 300 ms (log) | 3 ms | Durée de moyenne du détecteur RMS |
| Oversampling | `oversampling` | Off / 2× / 4× | Off | Suréchantillonnage du compresseur (non automatisable) |

Les identifiants et le sens des réglages 1.x ne changent pas. Une session enregistrée avec la 1.x se rouvre avec ses réglages ; les réglages 2.0.0, absents de la session, prennent leur valeur par défaut (testé). Reset (preset « Default ») remet aussi le détecteur par défaut, mais laisse le suréchantillonnage et le Dry/Wet.

### 9.3 Latence (annoncée au logiciel, exacte ; le son sec du Dry/Wet et du bypass est recalé dessus)

| Format | Off | 2× | 4× |
|---|---|---|---|
| VST3 / AU | Lookahead (0 à 10 ms) | Lookahead + 69 éch. | Lookahead + 76 éch. |
| AAX (Pro Tools, latence fixe) | 10 ms | 10 ms + 69 éch. | 10 ms + 76 éch. |

Exemples à 48 kHz : Lookahead 5 ms → 240 / 309 / 316 éch. ; AAX → 480 / 549 / 556 éch. Les 69 et 76 éch. sont les mêmes à toutes les fréquences d'échantillonnage. En AAX, changer le Lookahead ne change pas la latence (le gain est retardé du complément) ; seul le suréchantillonnage la change.

### 9.4 Mesures avant / après (banc `tests/quality_test.cpp`)

| Mesure | 1.2.1 | 2.0.0 |
|---|---|---|
| Preset Brickwall (plafond −4 dBFS), mesure de l'audit | crêtes −2,3 dBFS, true peak −0,3 dBTP | −4,000 dBFS (True peak Off) ; −4,05 dBFS et ≤ −4,0 dBTP (True peak On) |
| Brickwall, signal d'épreuve (batterie, sinus 11 / 12 kHz, impulsions isolées, musique dense), 44,1 / 48 / 96 / 192 kHz | +3,8 à +4,6 dBFS | crêtes −4,0000 dBFS partout ; true peak −3,93 à −4,04 dBTP |
| Brickwall, contenu limité à 20 kHz, Lookahead 1 et 5 ms | | true peak −4,050 dBTP (sous le plafond) |
| Repliement, sinus 9,5 kHz −6 dBFS, 4:1, att 0,1 / rel 10 ms, 44,1 kHz | −53,7 dB (Off) | −63,1 dB (2×), −79,1 dB (4×) |
| Repliement, sinus 15 kHz, 48 kHz | −51,3 dB | −56,4 dB (2×), −88,2 dB (4×) |
| Clic quand le Lookahead passe de 0 à 5, 10, puis 2 ms (énergie au-dessus de 3 kHz sur un sinus 220 Hz) | −6,9 dB (clic franc) | −107,7 dB (rien) |
| Makeup 0 → +12 dB d'un coup | −0,1 dB (clic franc) | −98,3 dB (rampe de 20 ms) |
| Threshold 0 → −30 dB, attaque 1 ms | −36,7 dB | −73,2 dB |
| Ratio 10 → ∞ → 8, Mid-Side, SC source, suréchantillonnage en cours de lecture | | −76 à −86 dB, aucun clic |
| Test de nul (sans compression) traité − sec | | exact au bit près (Off) ; −115 dB (2×), −103 dB (4×) |

Le petit reste de true peak au-dessus du plafond (au plus 0,07 dB mesuré) ne vient que du contenu situé entre 20 kHz et la fréquence de Nyquist (bruit plein spectre) : c'est la limite annoncée de la brique commune. Pour tout contenu jusqu'à 20 kHz, la crête vraie reste sous le plafond.

### 9.5 Même son à toutes les fréquences d'échantillonnage

Mesuré à 44,1 / 48 / 96 / 192 kHz : courbe statique (écart < 0,0001 dB, aussi en 4×), constantes de temps (attaque 10 ms → 9,98 à 9,99 ms ; release 100 ms → 100,0 ms), filtre de side-chain (−3,010 dB à sa fréquence), Hold (100 ms tenus à 0,1 ms près), fenêtre RMS, plafond du limiteur.

### 9.6 Tests

- `tests/quality_test.cpp` : banc sans JUCE (86 vérifications chiffrées, environ 6 s), lancé aussi sur Mac par la fabrication automatique avant la compilation.
- `tests/processor_test.cpp` : plugin complet (latences, Dry/Wet, bypass, AAX, bus side-chain, preset Brickwall à 44,1 / 48 / 96 kHz, réouverture d'une session 1.x).
- `tests/verify_dsp.py` : comparaison au code web d'Audio Lab. Toujours identique au bit près pour tout le compresseur (ratio < ∞) ; la comparaison du « limiteur » ∞ est retirée (le web garde son compresseur 100:1).
- pluginval niveau 10 et captures de l'interface (100 %, 60 %, 200 %, panneau déplié, mode ∞, Mid-Side).

### 9.7 Limites connues

- True peak : il faut au moins 1 ms de Lookahead ; avec du contenu au-delà de 20 kHz, dépassement possible de 0,07 dB au plus.
- Le suréchantillonnage ne s'applique pas au limiteur ∞ (inutile pour son plafond, qui est exact au rythme de base) ; changer le suréchantillonnage coupe le son 13 à 45 ms (fondus) car la latence change.
- Avec une compression extrême (attaque 0,1 ms, release 10 ms), baisser le seuil de 30 dB d'un coup produit un peu d'aigu pendant 20 ms (−55 dB) : c'est l'attaque qui suit la réduction qui monte, comme si la musique montait d'autant ; avec une attaque d'1 ms, rien (−73 dB).
