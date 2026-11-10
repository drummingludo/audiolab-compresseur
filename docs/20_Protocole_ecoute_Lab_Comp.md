# 20 · Protocole d'écoute en aveugle du Lab Comp 3.0.0 (10/11/2026)

Le banc de mesures dit ce que fait le plugin. Il ne dit pas s'il sonne bien. Aucune écoute n'a été faite pendant le développement de la 3.0.0 : ce protocole sert à la faire, à volume égal et sans savoir quel compresseur on entend, contre FabFilter Pro-C, un compresseur UAD et un compresseur Waves.

Tant que ces écoutes ne sont pas faites, la seule chose établie est ce que le banc mesure (fiche 06, section 3.0.0). Aucune supériorité sonore n'est affirmée.

## 1. Ce qu'on compare

Quatre compresseurs sur la même piste, un par copie de piste dans Logic :

| Lettre de travail | Compresseur | Réglage de départ |
|---|---|---|
| L | Lab Comp 3.0.0 | réglages de la scène, Oversampling 4×, OS filter Linear |
| P | FabFilter Pro-C (2 ou 3) | style Clean, Lookahead Off, Oversampling 4×, Auto Gain Off, Mix 100 % |
| U | un compresseur UAD à réglages continus (par exemple Precision Channel Strip ou API 2500) | réglages de la scène |
| W | Waves C1 Compressor ou Renaissance Compressor | réglages de la scène ; sur le Renaissance : Electro, Smooth, ARC coupé |

Les lettres servent seulement à préparer la session. Les fichiers écoutés porteront d'autres noms, tirés au sort (étape 4).

## 2. Les scènes

Trois sources, de 20 à 30 secondes chacune, en boucle : une voix, un bus de batterie, une basse tenue. Pour chaque source, un réglage du Lab Comp sert de référence.

| Scène | Source | Threshold | Ratio | Knee | Attack | Release | Detection | Réduction visée |
|---|---|---|---|---|---|---|---|---|
| 1 | Voix | à régler | 3:1 | 8 dB | 10 ms | 80 ms, Fixed | Peak | 3 à 5 dB sur les phrases fortes |
| 2 | Bus de batterie | à régler | 4:1 | 2 dB | 30 ms | 100 ms, Auto | Peak | 4 à 6 dB sur la caisse claire |
| 3 | Basse tenue | à régler | 4:1 | 6 dB | 5 ms | 30 ms, Smart | Peak | 6 dB environ |
| 3 bis | Basse tenue | comme la scène 3 | 4:1 | 6 dB | 5 ms | 30 ms, Fixed | Peak | 6 dB environ |

La scène 3 bis compare le Lab Comp à lui-même : c'est le test du release Smart, qui ne distord plus le grave sur les mesures (0,004 % contre 2,5 %). L'oreille doit dire si la différence s'entend et si Smart ne ramollit pas l'attaque des notes.

## 3. Régler les quatre compresseurs pour qu'ils fassent le même travail

Les temps d'attaque et de release ne veulent pas dire la même chose d'une marque à l'autre. Recopier « 10 ms » partout ne donne pas le même résultat. On aligne donc sur ce que le compresseur fait, pas sur ce que ses boutons affichent.

1. Régler le Lab Comp sur la scène, jusqu'à obtenir la réduction visée sur son VU.
2. Sur chaque autre compresseur, mettre le même ratio et le même knee (ou le plus proche), puis descendre le seuil jusqu'à lire la même réduction moyenne sur les mêmes passages.
3. Ajuster l'attaque et le release à l'oreille pour que l'aiguille de réduction bouge à la même vitesse que celle du Lab Comp : même retard sur l'attaque d'une note, même temps de retour après une frappe.
4. Couper tout ce qui n'a pas d'équivalent dans les quatre : saturation, caractère, auto-gain, side-chain filtré, lookahead.
5. Noter les réglages retenus dans le tableau de la section 6.

## 4. Volume égal et anonymat

Un son plus fort de 0,3 dB paraît meilleur. C'est le piège principal.

1. Exporter chaque piste traitée en WAV 24 bits, même début et même fin, sans normalisation. Nommer les fichiers `scene1_L.wav`, `scene1_P.wav`, `scene1_U.wav`, `scene1_W.wav`, et ainsi de suite.
2. Me les envoyer dans la conversation. Je mesure le volume de chacun (LUFS intégré, ITU-R BS.1770), je les ramène tous au même volume à 0,1 dB près, je vérifie qu'aucun n'écrête, puis je les renomme au hasard (`scene1_A.wav` à `scene1_D.wav`). Je garde la clé et je ne la donne qu'après les notes.
3. Sans moi : aligner les volumes avec le Loudness Meter de Logic (LUFS-I, écart inférieur à 0,1 dB) et demander à quelqu'un d'autre de renommer les fichiers.

## 5. L'écoute

- Même casque ou mêmes enceintes, même volume d'écoute pour toute la séance. Ne plus toucher au bouton de volume.
- Pour chaque scène, écouter les quatre fichiers dans un ordre libre, autant de fois que nécessaire, puis noter chacun sur 5 pour chaque critère du tableau.
- Faire deux passes à un jour d'écart. Si les notes d'un même fichier changent beaucoup d'une passe à l'autre, la différence n'est pas fiable.
- Une séance de 20 minutes au plus. Au-delà, l'oreille fatigue et tout se ressemble.

| Critère | Ce qu'on écoute |
|---|---|
| Transparence | Entend-on le compresseur travailler quand on ne le devrait pas ? |
| Attaques | Les débuts de notes et les frappes gardent-ils leur netteté ? |
| Grave | La basse reste-t-elle propre, sans grésillement ni mollesse ? |
| Pompage | Le niveau respire-t-il de façon gênante entre les frappes ? |
| Aigu | Les cymbales et les sifflantes restent-elles douces ? |
| Préférence | Lequel garderait-on dans un mix ? |

## 6. Feuille de relevé

À remplir pendant la préparation (réglages) puis pendant l'écoute (notes).

| Scène | Fichier | Transparence | Attaques | Grave | Pompage | Aigu | Préférence | Remarques |
|---|---|---|---|---|---|---|---|---|
| 1 | A | | | | | | | |
| 1 | B | | | | | | | |
| 1 | C | | | | | | | |
| 1 | D | | | | | | | |

Même tableau pour les scènes 2, 3 et 3 bis.

| Scène | Compresseur | Threshold | Ratio | Knee | Attack | Release | Réduction lue |
|---|---|---|---|---|---|---|---|
| 1 | L | | | | | | |
| 1 | P | | | | | | |
| 1 | U | | | | | | |
| 1 | W | | | | | | |

## 7. Lire le résultat

- Le Lab Comp au niveau des trois autres sur la préférence et sans critère à la traîne : l'objectif sonore est tenu pour cette scène.
- Un critère où il est nettement derrière sur les deux passes : c'est le prochain chantier du moteur, avec la scène et le réglage comme cas de test.
- Des notes qui se contredisent d'une passe à l'autre : les quatre sont trop proches pour être départagés sur cette source. C'est un bon résultat, pas un échec.

## 8. Ce que ce protocole ne couvre pas

- Le limiteur ∞:1 : il se compare à un limiteur (Pro-L, L2), pas à un compresseur. Protocole à écrire si besoin.
- Le suréchantillonnage 8× et le filtre Minimum : leurs effets sont sous −90 dB sur les mesures, donc en principe inaudibles. À vérifier une fois, en comparant le Lab Comp à lui-même.
- La charge CPU face à Pro-C : à lire dans le moniteur de performances de Logic, avec dix instances de chaque sur la même piste.
