# État des lieux (17 novembre 2026)

Fork de [PromethenDev/wwe13-recomp](https://github.com/PromethenDev/wwe13-recomp) (licence BSD 3-Clause,
fork autorisé, auteur crédité dans `README.md` et `THIRD_PARTY.md`). Point de départ : v1.5.0 (`d657498`).

## Outils de build (Windows natif, sans WSL)

| Outil | Version |
|---|---|
| SDK ReXGlue | fork `rexglue-sdk-yukes` @ `987593c` (v0.7.3) + 100 patchs (`rexglue-patches/`) |
| Compilateur | LLVM clang 23.1.2 (pilote GNU `clang++`, comme la release ; clang-cl ignore `-ffp-model=strict`) |
| CMake / Ninja | 4.4.3 / 1.13.2 |
| Vulkan (test) | RTX 5070 Ti, API 1.4.351 |
| Options | RelWithDebInfo `-O2`, `-march=x86-64-v3`, CRT statique, lld |

Pièges rencontrés : patchs en CRLF après un clone Windows (les lire depuis git en LF) ; liens symboliques
de `thirdparty/libmspack` non créés sous Windows (copier les fichiers) ; chemin de `llvm-ar` avec espace.

## Mesures (banc `tools/windows/bench`, 1 contre 1 avec entrées)

Machine de test : Ryzen 7 7800X3D, RTX 5070 Ti, 720p.

| Test | Images/s | Images en retard | CPU |
|---|---|---|---|
| Plein CPU, match | 60 stable | ~0 | 90-130 % d'un cœur |
| 4 threads, entrées | 31 (cible 30) | 1 | 125 % |
| 2 threads (1 cœur), match | ~30-40 au lieu de 60 | 300-600 | 120 % |

- Thread principal : 66 % d'un cœur, dont ~2/3 d'attente active du GPU (`sub_82B99A70`).
  Céder le cœur pendant cette attente (`WWE13_FENCE_YIELD=1`) : pas de gain mesurable sur 1 cœur, reste coupé.
- Temps GPU par image en match (RTX 5070 Ti, 720p) : 3,8 ms. Dessin de la scène 1,9 ; transferts de
  cibles de rendu 0,6 (environ 2 160 passes par image) ; textures 0,47 ; envoi mémoire 0,34 ; copies EDRAM 0,3.
- Issue #68 (Ryzen Z2 A, iGPU 8 CU, 480p) : surtout limité par le GPU, pas par le CPU.

## Bugs connus (issues ouvertes du dépôt d'origine), par catégorie

- **Plantage** : #75 (édition CAW : fenêtre mémoire physique pleine, NULL déréférencé), #76 (iGPU 256 Mo :
  `VK_ERROR_DEVICE_LOST`, puis 185 Mo de log), #53 (King of the Ring, log énorme), #57 #51 #39 #34 #1
  (démarrage), #23 #17 #12 (créations importées), #21, #31.
- **Gel** : #67, #25.
- **Très lent** : #68, #60, #52, #49, #26 ; #70 (processeurs sans AVX2).
- **Graphique** : #74 (sous-titres), #73 (texture du corbillard), #72 (ombre du ring), #69, #63, #37, #32.
- **Sauvegarde / DLC** : #64, #27, #66, #50 ; #28 (arènes créées).
- **Manette / clavier** : #55 (clavier en espagnol).
- **Demandes** : #77 (2160p / 4320p), #65, #71 (musique), #48 (replay).

## Fait dans ce fork

- Menu principal : BOUTIQUE devient QUITTER et ferme le jeu (vu en jeu). `WWE13_EXIT_ENTRY=0` pour l'enlever.
- Icône WWE pour `wwe13.exe` et le launcher, détails de fichier (version).
- Diagnostics : `WWE13_PHYS_CENSUS=1` (allocations physiques avec appelants) ; essai coupé
  `physical_window_fallback` pour #75 (il casse la mesure de pool du jeu au chargement d'un match).
- Banc de test reproductible et échantillonneur de threads (`tools/windows/bench`).

## Plan proposé (gain ÷ risque)

1. #76 : sur un GPU à petite mémoire dédiée, placer les gros tampons en mémoire partagée ; après une perte
   du GPU, fermer proprement au lieu d'écrire des centaines de milliers d'erreurs.
2. Mode « PC faible » mesuré : coût des transferts de cibles de rendu (2 160 passes/image), des textures
   rechargées à chaque image, de la conversion de profondeur ; viser les iGPU (Steam Deck, Z2 A, 5500U).
3. #75 : comprendre qui remplit la fenêtre de 6,5 Mo dans l'éditeur CAW (reproduire avec un catcheur chargé).
4. #70 : build `x86-64-v2` à côté du build AVX2, à mesurer.
5. Ensuite les bugs graphiques, un par un, avec captures.
