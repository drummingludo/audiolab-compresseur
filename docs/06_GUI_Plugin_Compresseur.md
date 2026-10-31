# 06 — Interface du plugin « Audio Lab Compresseur » (VST3 / AU)

**Statut : validée par Ludovic le 31/10/2026.** Référence visuelle : maquette Design « Audio Lab Compresseur — proportions des vues » (planches « Plugin replié », « Plugin déplié », « Option B »).
Ce document fait foi pour le plugin et pour tout futur plugin Audio Lab du même type (même grille, mêmes composants). Il complète la charte `02_Charte_UI_Audio_Lab.md`.

---

## 1. Principes

- **Une seule règle d'espacement : 24 px.** Marge extérieure de 24 px sur les 4 côtés, 24 px entre deux potentiomètres voisins et entre deux rangées. 16 px seulement entre l'écran et la rangée du bas, et entre la rangée du bas et le panneau avancé (ces éléments vont ensemble).
- **L'essentiel visible, le reste replié.** 4 gros potentiomètres + l'écran. Les réglages fins (Knee, Lookahead, Détection, Side-chain) sont dans un panneau caché par défaut.
- **Un écran carré unique (270 × 270)** qui montre une vue à la fois : Transfert, Activité ou Vu-Mètres. Les graphes remplissent tout le carré.
- **Look « matériel analogique » discret** : repères sur les capuchons, léger halo sur les arcs, ombre intérieure du panneau VU, barres de niveau à bout droit.
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
| Rangée 1 | THRESHOLD (bleu) | RATIO (bleu) |
| Rangée 2 | ATTACK (orange) | RELEASE (orange) |

- Centres : x = 75,6 et 202,8 ; y = 75,6 et 223.
- Arc : rayon 49, épaisseur 5,2, ouverture 270° (de 7 h 30 à 4 h 30). Rail gris #E5E5EA à bouts ronds.
- Arc coloré : **bout mobile coupé droit**, départ arrondi (pastille ronde de même couleur). **Halo léger** de la même couleur (flou 2 px, opacité ≈ 55 %).
- Threshold : l'arc part de 0 dB (à droite) et descend vers la valeur. Les autres partent du minimum.
- Capuchon : disque blanc 74 px, ombre 0 1 2 rgba(0,0,0,.08) + 0 8 20 rgba(0,0,0,.14).
- **Repère** : trait fin de 1,5 px, couleur du bouton, collé au bord du capuchon (du rayon 37 vers le rayon 29), dirigé vers la valeur, bout droit.
- Valeur au centre : 18 px semi-gras ; unité dessous 11 px gris #86868B.
- Nom : majuscules 11 px semi-gras, interlettrage 0,06 em, à 58 px sous le centre. Les noms de la 2ᵉ rangée tombent exactement sur le bas de l'écran (y 294).
- Manipulation : glisser vertical (Maj = fin), molette, flèches du clavier, double-clic = valeur par défaut, bulle d'aide « Pourquoi ? ».

## 4. Rangée du bas (y 310, hauteur 28)

- **À gauche : « Réglages avancés »** avec chevron (12 px). Le chevron est aligné sur la marge de 24 px. Fermé : chevron « › », texte gris #515154. Ouvert : chevron « ⌄ », texte #1D1D1F.
- **À droite, sur la largeur exacte de l'écran (x 286 → 556), écart de 8 px entre les deux :**
  - **Sélecteur de vue** « Transfert | Activité | Vu-Mètres » : bande #F2F2F7, coins 9, marge intérieure 3 px, 3 segments de largeur égale, texte 11 px. Segment choisi : pastille blanche, coins 7, ombre 0 1 3 rgba(0,0,0,.12).
  - **Petit Make-up** (bouton de 28 px, coin bas droit) : arc vert rayon 11,5, épaisseur 2,4, capuchon blanc 16 px, repère, halo. À sa gauche, deux lignes alignées à droite : « MAKEUP » (9 px semi-gras, interlettrage 0,04 em) et la valeur « +4 dB » (Menlo 10 px, gris #515154).

## 5. Écran carré (270 × 270)

Fond #F2F2F7, bord 1 px #E5E5EA, coins 10. Le contenu est découpé à l'intérieur des coins arrondis.

### Vue Transfert
- Graphe −60 → 0 dB sur les deux axes, **il remplit tout le carré** (1 dB en entrée = 1 dB en sortie à l'écran).
- Grille tous les 12 dB (#E5E5EA). Diagonale 1:1 en pointillés #D1D1D6 (4/4).
- Zone de knee : bande bleue 7 % de part et d'autre du seuil ; ligne de seuil verticale pointillée bleue 50 %.
- Courbe bleue 2,5 px + remplissage dégradé bleu 25 % → 2 %.
- Point en direct orange (pastille 10 px bord blanc + halo 18 px) avec pointillés orange vers les axes.
- **Chiffres à l'intérieur, petits** : Menlo 8 px #8E8E93. Sortie le long du bord gauche (0, −12, −24, −36, −48), juste au-dessus de chaque ligne. Entrée le long du bord bas (−48, −36, −24, −12, 0), juste à gauche de chaque ligne.
- **Pas de texte « GR ».**

### Vue Activité
- Défilement de l'enveloppe, **remplit tout le carré, aucun chiffre**.
- Entrée en gris (#8E8E93, remplissage 18 %), sortie en bleu (remplissage 35 %), symétriques autour du centre.
- Grille −12 / −24 / −36 dB. Seuil en pointillés bleus (6/4) en haut et en bas.

### Vue Vu-Mètres
- **Panneau VU** : x 14, y 14, 242 × 172, coins 8, fond #F2F2F7, **ombre intérieure** (décalage 4 px vers le bas, flou 5,5, opacité 50 %) pour simuler une face encastrée de matériel analogique.
- VU à aiguille de réduction de gain : graduations −20 … +3, zone rouge au-dessus de 0, « VU » (20 px gras) et « GAIN REDUCTION » (10 px semi-gras). Aiguille bleue, axe gris foncé. **Balistique VU normalisée : 99 % en 300 ms, dépassement ≈ 1,3 %.**
- Dessous, 3 barres horizontales **In / GR / Out** : piste 170 × 8 (x 46 ; y 200, 222, 244), coins 4. Nom à gauche (10 px semi-gras), valeur à droite (Menlo 9 px).
  - In / Out : dégradé vert → jaune (82 %) → rouge. GR : orange. Repère de seuil bleu sur In.
  - **Bout mobile coupé droit**, départ arrondi.

## 6. Panneau « Réglages avancés » (caché par défaut)

- Fond #F2F2F7, coins 12, marge intérieure 16, **2 colonnes** (écart 32), **3 lignes de 26 px** (écart 8).
- Colonne gauche : **Knee** (curseur, 0–20 dB), **Lookahead** (curseur, ms), **Détection** (Peak | RMS).
- Colonne droite : **Side-chain** (menu Off / HPF / LPF / Bell +6, 60 px) + bouton **« Écouter »** (icône casque), **SC Freq** (Hz, échelle log), **SC Q**.
- Étiquettes 12 px #515154 sur 64 px ; valeurs Menlo 12 px alignées à droite sur 48 px.
- Curseurs : piste 4 px #E5E5EA, bouton blanc 18 px avec ombre.
- SC Freq et SC Q grisés quand le side-chain est sur Off.
- Ouvrir le panneau agrandit la fenêtre de 142 px ; le refermer la réduit. L'état ouvert/fermé est mémorisé avec la session.

## 7. Couleurs

| Rôle | Clair | Sombre |
|---|---|---|
| Fond fenêtre | #FFFFFF | #1C1C1E |
| Écran / bandes / panneau | #F2F2F7 | #2C2C2E |
| Rails, grille, bords | #E5E5EA | #3A3A3C |
| Texte | #1D1D1F | #F5F5F7 |
| Texte secondaire | #515154 | #C7C7CC |
| Gris doux (unités, chiffres) | #86868B / #8E8E93 | #8E8E93 |
| Bleu (Threshold, Ratio, courbe) | #007AFF | #0A84FF |
| Orange (Attack, Release, GR) | #FF9500 | #FF9F0A |
| Vert (Make-up, niveaux) | #34C759 | #30D158 |
| Jaune | #FFCC00 | #FFD60A |
| Rouge (zone rouge, crêtes) | #FF3B30 | #FF453A |

- Police : **Inter** (texte), **Menlo / monospace** (chiffres).
- Thème clair / sombre : suit automatiquement le réglage du Mac ou de Windows.

## 8. Ce qui a été retiré volontairement

- L'en-tête (nom, presets, A/B/C, Dry/Wet, Bypass) : le Bypass et les presets se font depuis le logiciel hôte. Le paramètre Dry/Wet existe toujours (automation).
- Les barres In / GR / Out à côté de la courbe (elles sont dans la vue Vu-Mètres).
- Le texte « GR » sur la vue Transfert et tous les chiffres sur la vue Activité.
- Knee en gros bouton (déplacé dans les réglages avancés).
- Make-up en gros bouton (remplacé par le petit bouton en bas à droite).
