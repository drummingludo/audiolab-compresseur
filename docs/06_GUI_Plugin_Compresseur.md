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
| Fenêtre dépliée | **580 × 504** (le panneau ajoute 142 px en bas) |
| Marge extérieure | 24 |
| Écran (carré) | x 286, y 24, 270 × 270, coins 10 |
| Rangée du bas | y 310, hauteur 28 |
| Panneau avancé | x 24, y 354, 532 × 126, coins 12 |
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
  - **Sélecteur de vue** « Transfert | Activité | Vu-Mètres » : bande #2A2B29, coins 9, marge intérieure 3 px, 3 segments de largeur égale, texte 10 px #C9C9CA. Segment choisi : pastille #005F73, texte #F9F9FB en 500, coins 7, ombre 0 1 3 rgba(0,0,0,.18).
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

- Fond « Surface » #2A2B29, coins 12, marge intérieure 16, **2 colonnes** (écart 32), **3 lignes de 26 px** (écart 8).
- Colonne gauche : **Knee** (curseur, 0–20 dB), **Lookahead** (curseur, ms), **Détection** (Peak | RMS).
- Colonne droite : **Side-chain** (menu Off / HPF / LPF / Bell +6, 60 px) + bouton **« Écouter »** (icône casque), **SC Freq** (Hz, échelle log), **SC Q**.
- Étiquettes 12 px #C9C9CA sur 68 px (+ 10 px d'écart) ; valeurs 12 px #F9F9FB alignées à droite sur 48 px (chiffres en cellules fixes).
- Curseurs : piste 4 px #3E3F3D, bouton #373837 de 18 px (liseré + ombre), **partie avant le bouton remplie** avec halo léger : Knee teal en dégradé, Lookahead orange plein, SC Freq / SC Q teal secondaire ; rien de coloré à la valeur minimale.
- Menu Side-chain (60 px) et bouton « Écouter » : fond #373837, coins 8 ; « Écouter » actif = #005F73. Sélecteur Détection : bande #3E3F3D, segment choisi #005F73. Écrire « Side-chain » avec un trait d'union normal.
- SC Freq et SC Q grisés (gris doux #A5A6A6) quand le side-chain est sur Off.
- Ouvrir le panneau agrandit la fenêtre de 142 px ; le refermer la réduit. L'état ouvert/fermé est mémorisé avec la session.

## 7. Couleurs, police, thème (charte V2)

Thème **sombre uniquement** (plus de bascule clair / sombre). Police **Poppins** embarquée (400, 500, 600, 700). Couleurs : fiche 02 (V2) — fond #1D1E1C, surface #2A2B29, capuchon #373837, rails #3E3F3D, texte #F9F9FB / #C9C9CA / #A5A6A6 ; teal #0A9396 → #79AFB0 (niveau, seuil, courbe), teal secondaire #79AFB0 (gain, niveaux, filtre side-chain), sélection #005F73 ; orange #BB3E03 → #EAA93D (temps, réduction de gain) ; rouge #9B2226 sur cadran clair, #BC6D71 sur fond sombre.

Bypass (depuis le logiciel hôte) : la lampe du VU s'éteint ; les réglages restent accessibles.

## 8. Ce qui a été retiré volontairement

- L'en-tête (nom, presets, A/B/C, Dry/Wet, Bypass) : le Bypass et les presets se font depuis le logiciel hôte. Le paramètre Dry/Wet existe toujours (automation).
- Les barres In / GR / Out à côté de la courbe (elles sont dans la vue Vu-Mètres).
- Le texte « GR » sur la vue Transfert et tous les chiffres sur la vue Activité.
- Knee en gros bouton (déplacé dans les réglages avancés).
- Make-up en gros bouton (remplacé par le petit bouton en bas à droite).
