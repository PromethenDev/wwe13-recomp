# WWE '13 : passage au rendu natif (plan)

But : un jeu « quasi natif PC », comme WWE '12 PC et WWE 2K15 PC. Le code du jeu reste le code Xbox 360
recompilé (ReXGlue), mais le rendu ne passe plus par l'émulation du GPU Xenos (paquets PM4, EDRAM,
traduction des shaders au vol). Les appels Direct3D du jeu sont interceptés et dessinés directement en
Vulkan (re:Blue + plume), avec les shaders du jeu convertis à l'avance (XenosRecomp).

## Pourquoi

Mesuré sur WWE 2K15 (même méthode, 2026-10-27, docs de `wwe2k15-pc2`) : sur un iGPU AMD, l'émulation Xenos
fait 21-38 images/s, le rendu natif tient 60 images/s, même sur 2 cœurs. Sur WWE '13 (ce dépôt, 2026-11-17) :
2 160 passes de transfert de cibles de rendu par image et un thread « GPU Commands » à 22 % d'un cœur,
coûts propres à l'émulation qui disparaissent avec le rendu natif. Issues concernées : #68, #26, #52, #60, #76.

## Faisabilité vérifiée (2026-11-17)

- Le codegen du SDK 0.10 (celui de WWE '12 PC et 2K15, `C:\Dev\rexglue-sdk` @ `c94f5eb`) accepte le
  `default.xex` de WWE '13 avec `config/functions.toml` et setjmp/longjmp : 625 fichiers en 52 s.
- Bibliothèque D3D de WWE '13 vers `0x82B90000-0x82BA0000` : `sub_82B9B2E0` appelle `VdSwap`,
  `VdGetSystemCommandBuffer` et `VdPersistDisplay` ; l'attente de fence `sub_82B9D540` / `sub_82B99A70`
  est dans la même zone. WWE '12 : `0x82AA0000-0x82AC0000`.

## Licence

re:Blue (BSD-3) et plume (MIT) sont libres, mais le rendu de WWE '12 PC est adapté des portages SvR de
nefariousjosiah (GPL-3.0). L'intégrer rend le projet GPL-3.0 (le code BSD-3 de PromethenDev peut y être
inclus en gardant sa notice). Décision de l'utilisateur requise avant l'étape 3.

## Étapes

1. **Base SDK 0.10 (rendu Xenos)** : le jeu démarre et joue un match sur le SDK partagé + les patchs de
   2K15 ; reporter les correctifs WWE '13 utiles (setjmp, tissu de l'Undertaker, sauvegardes, clavier,
   QUITTER, icône). Mesure de référence avec `tools/windows/bench`.
2. **Carte D3D de WWE '13** (`docs/d3d_functions.md`, `wwe13_d3d_names.h`) : recensement des appels
   (outil `gen_d3d_census.py` de WWE '12 / 2K15), ancrages VdSwap, graphe d'appels. Objet device, offsets.
3. **Rendu natif branché** (`native_renderer.cmake` de WWE '12) : device, swapchain, Clear/Present, puis
   tampons, textures, déclarations, shaders, dessins. Le rendu Xenos reste disponible en secours.
4. **Shaders** : extraction depuis les `.pac` du joueur, conversion XenosRecomp + DXC, cache généré chez
   le joueur (jamais dans le dépôt ni dans un zip).
5. **Tranche visible** : menus, puis entrées, puis match, comparés image par image au rendu Xenos.
6. **Rendu natif par défaut**, puis optimisation (mesures avant/après) : 60 images/s en entrées, chargements
   accélérés, pipelines préparés, options graphiques dans les menus du jeu, AMD/Intel/Steam Deck.

## Moteurs de rendu : Vulkan, Direct3D 12, Direct3D 11

Les trois existent déjà dans WWE '12 PC (référence lue le 2026-11-17 : `iMrBurn/wwe12-pc` @ `7537e14`, clone en
lecture seule dans `C:\Dev\wwe12-pc-ref`) : `wwe12.exe` (Vulkan), `wwe12_d3d12.exe` (cible `WWE12_D3D12_EXE`,
`port/patches/plume-d3d12.patch`, 47 lignes), `wwe12_d3d11.exe` (cible `WWE12_D3D11_EXE`,
`port/patches/plume-d3d11.patch`, 2 517 lignes, d'après le travail D3D11 de Kaiko pour SvR 2011, avec son
accord, à créditer aussi ici). Réglage `gpu_backend` : `any` (Vulkan, puis D3D12, puis D3D11), `vulkan`,
`d3d12`, `d3d11`. Aucune DLL Microsoft livrée : shaders prélinkés (`wwe12_prelink.exe`), shaders D3D11 dans
l'exe ; jamais dxcompiler / dxil / d3dcompiler / fxc dans le zip. Pièges : `docs/dx11.md` (constantes lues à
zéro au-delà du dernier registre déclaré ; viewport MinZ > MaxZ refusé : depthFlip), `docs/couches.md`,
`docs/savoir_acquis.md`, `docs/perf_natif_steam_deck.md`.

Ordre : un seul moteur qui rend une image correcte (Vulkan), puis D3D12 (même code plume, un patch), puis
D3D11 (moins de fonctions ; chez WWE '12 les reflets du soleil manquent en D3D11). Mesure et capture
RenderDoc de référence par scène à chaque étape.

## Générique ou propre à WWE '13

| Partie (WWE '12 PC) | Générique | Propre à WWE '13 |
|---|---|---|
| plume + 3 backends + patchs | oui | |
| `reblue/gpu/device.cpp`, `present.cpp` (V-sync, fenêtre, résolution interne, netteté) | oui | offsets de l'objet device du jeu à vérifier |
| cache de pipelines (`pipelines.bin` en graine), réglages (`gpu_backend`, échelle, anisotropie, aspect) | oui | |
| `reblue/gpu/hooks/` (~50 fonctions D3D du jeu) | le code | **les adresses** : `wwe12_d3d_names.h` → `wwe13_d3d_names.h` |
| shaders (XenosRecomp + DXC / fxc) | la chaîne | extraction depuis les `.pac` de WWE '13 |
| `native/svr_resources.cpp`, `svr_geometry.cpp` (surfaces, resolves EDRAM, read-backs) | la méthode | cibles et read-backs propres aux scènes de WWE '13 |
| cadence 60 / 30, scripts de scène, menus natifs | la méthode | adresses et scripts de WWE '13 |
| backend GPU « null » (`rexglue-sdk-reblue.patch`) | oui | |

Méthode pour les adresses : générer le code de WWE '12 avec le même SDK 0.10, puis apparier chaque fonction de
la bibliothèque D3D de WWE '12 avec celle de WWE '13 par la structure de son code (instructions sans les
adresses), pas à la main.

Chaque étape : mesure avant/après, rien de risqué activé par défaut avant d'avoir été vu en jeu.

## Avancement

- 2026-11-17, étapes 1-2 : WWE '13 sur le SDK 0.10 (`port/`), carte D3D. WWE '13 a la bibliothèque D3D de
  WWE 2K15 (22 fonctions, 15 identiques octet pour octet), pas celle de WWE '12.
- 2026-11-17, étape 3 : `wwe13_native.exe` (re:Blue + plume, Vulkan, copié de WWE 2K15 PC). Premier essai noir
  (réglages de WWE '12 : pas de crochets Create* / Unlock) ; avec le jeu de crochets de 2K15 : 587 shaders
  convertis sans échec chez le joueur, écran de chargement, menu principal, sélection des catcheurs et entrées
  (Otunga sur la rampe, Stone Cold sur les cordes) affichés correctement, sans l'image coupée du rendu Xenos.
  RTX 5070 Ti, ×2 automatique (2560×1440) : menus 60 images/s, entrées 30 (la cadence du jeu), aucun à-coup
  > 25 ms, travail 11-13 ms par image, mémoire vidéo ~300-360 Mo.
- À faire : un match complet, mesure sur le GPU intégré AMD de ce PC et sur 2 cœurs (comme 2K15), reporter
  QUITTER / icône / clavier / launcher, puis D3D12 et D3D11.
- 2026-11-17, D3D12 et D3D11 : `wwe13_d3d12.exe` et `wwe13_d3d11.exe` (section ci-dessous). Menus, entrées et
  matchs corrects sur la RTX et sur l'iGPU AMD, 60 images/s en match.

## Direct3D 12 et Direct3D 11 (2026-11-17)

Même modèle que WWE '12 PC v1.3 (`C:\Dev\wwe12-pc-ref` @ `7537e14`, `docs/dx11.md`), décision de l'utilisateur
(option A) : les shaders du jeu sont convertis **au moment de la compilation** et intégrés dans les exe. Aucune DLL
Microsoft n'est livrée avec ces deux exe (ni dxcompiler, ni dxil, ni d3dcompiler, ni fxc) : vérifié avec
`llvm-objdump -p` (D3D12 n'importe que `d3d12.dll` / `dxgi.dll`, dxcompiler seulement en chargement différé pour les
builds de dev ; D3D11 que `d3d11.dll` / `dxgi.dll`) et par un match D3D12 lancé depuis un dossier sans aucune DLL
de DXC (0 variante manquante). `wwe13_native.exe` (Vulkan) ne change pas : ses shaders restent convertis chez le
joueur avec `dxcompiler.dll`.

**Construire** : `port\tools\build.ps1 -Native`. Il faut les conteneurs de shaders du jeu dans `work\shaders\xsc`
(hors dépôt : `WWE13_DUMP_SHADERS=<dossier>` sur `wwe13_native.exe`, 587 conteneurs) ; sans eux seul
`wwe13_native.exe` est compilé (avertissement CMake). Étapes, toutes automatiques :

1. `port\tools\prepare_native_deps.ps1` copie plume et reblue-XenosRecomp de `C:\Dev\native-deps` (partagé avec
   2K15, jamais modifié) dans `work\native-deps`, les remet aux commits de WWE '12 (`e0c8871`, `339af41`) et applique
   `port\patches` : `plume-reblue`, `plume-pipeline-cache` (2K15), `plume-d3d12`, `plume-d3d11` ;
   `xenosrecomp-reblue-wwe12` (celui de 2K15 + l'inversion de profondeur DXIL). Puis il compile `XenosRecomp.exe`.
   Refait seulement quand un patch change. Les trois exe utilisent ce plume (le code Vulkan est identique à celui de
   `native-deps`, le patch D3D11 n'ajoute qu'une valeur d'énumération côté commun).
2. CMake (`port/cmake/native_renderer.cmake`) : XenosRecomp → `native_gen/shader_cache.cpp` (DXIL + SPIR-V) et le
   HLSL de chaque shader ; `wwe13_prelink` (`port/src/prelink`, le `wwe12_prelink` de WWE '12) lie toutes les
   variantes de constantes de spécialisation → `linked_shader_cache.cpp` (D3D12) ;
   `port/tools/convert_shaders_d3d11.py` (fxc du Windows SDK) → `dxbc_shader_cache.cpp` (D3D11) ; shaders de l'hôte
   en DXIL (DXC) et DXBC (`host_shader_d3d11.py`).
3. Cibles `wwe13_d3d12` (`REBLUE_D3D12`) et `wwe13_d3d11` (`REBLUE_D3D12` + `REBLUE_D3D11`, `plume_d3d11.cpp`),
   options `WWE13_D3D12_EXE` / `WWE13_D3D11_EXE` (ON si les shaders sont là).

Chiffres de la compilation : 587 shaders convertis en 5 s ; 1 472 variantes DXIL pré-liées (0 échec, 4,4 Mo
compressés) ; 1 509 variantes DXBC (0 échec, 1,2 Mo compressés, 25 s).

**Code re:Blue** porté de WWE '12 v1.3 (tout derrière `REBLUE_D3D12` / `REBLUE_D3D11`, rien ne change pour Vulkan) :
backend DXBC, liaison des textures par emplacement (`gpu/d3d11_slots.cpp`), viewport à profondeur inversée (MinZ >
MaxZ) refusé par D3D11 : bornes échangées et `depthFlip` (c21.z, z' = w − z dans chaque vertex shader), aussi en
D3D12 (`svr_d3d12_depth_flip`, vrai par défaut, vieux pilotes Intel), profondeur 24 bits en D3D12 sur NVIDIA / Intel
(`svr_d3d12_depth32`), pas de comptage d'occlusion en D3D11 (pas d'UAV : pas de reflets du soleil, comme WWE '12),
cache DXBC et variantes pré-liées (`guest_shaders.cpp`), variante absente sans DXC = non dessinée et notée une fois.

**AUTO** (`gpu_backend = any`, défaut) : si `wwe13_native.exe` ne crée pas son device, il lance
`wwe13_d3d12.exe` avec les mêmes arguments (`native/wwe13_backend_chain.cpp`) et sort avec le code 0 (le lanceur
ne croit pas à un plantage) ; `wwe13_d3d12.exe` lance de même `wwe13_d3d11.exe`. Testé : Vulkan rendu indisponible
(`VK_ICD_FILENAMES` vers un fichier absent) → `wwe13_d3d12.exe` démarre et affiche le jeu. Le pas D3D12 → D3D11
n'a pas pu être provoqué ici (comme chez WWE '12). Avec une API choisie, pas de chaîne : message d'erreur.

**Lanceur** (`launcher2`) : la case « Native renderer » devient le menu **Graphics API** de WWE '12 : Automatic /
Vulkan / DirectX 12 / DirectX 11, plus « Emulated (Xenos) » pour garder l'ancien rendu `wwe13.exe` (case décochée).
Réglage `graphics_api=any|vulkan|d3d12|d3d11|xenos` dans `wwe13-enhanced.ini` (`native_renderer=0` devient
`xenos`). Un exe absent est grisé ; s'il manque quand même, le lanceur suit la chaîne AUTO. Le jeu reçoit
`--gpu_backend=<api>`.

**Mesures** (2026-11-17, fenêtré 1280×720 à l'écran, Otunga contre Stone Cold avec entrées, `play.ps1` d'après
`tools/windows/bench`) — intervalle moyen entre images / travail par image, sur 10 s :

| Exe | GPU | Menus | Entrées | Match (50 s) | Image > 25 ms en match |
|---|---|---|---|---|---|
| D3D12 (12_2) | RTX 5070 Ti, ×2 auto (2560×1440) | 16,7 / 13,5 ms | 33,3 ms (30, la cadence du jeu) | 16,7 / 12,9 ms | 4 |
| D3D11 (11_1) | RTX 5070 Ti, ×2 auto | 16,7 / 12,9 ms | 33,7 ms | 16,7 / 12,3 ms | 5 |
| D3D12 (12_1) | iGPU AMD Radeon, ×1 (1280×720) | 16,7 / 11,1 ms | 33,3 ms | 16,7 / 12,6 ms | 11 |
| D3D11 (11_1) | iGPU AMD Radeon, ×1 | 16,7 / 12,2 ms | 33,3 ms | 16,7 / 12,6 ms | 10 |
| Vulkan (contrôle) | RTX 5070 Ti, ×2 auto | 16,7 / 12,1 ms | 33,3 ms | — | — |

iGPU choisi par `--svr_gpu_device="AMD Radeon(TM) Graphics"` (même nom en Vulkan, D3D12 et D3D11). Mémoire vidéo en
match (D3D12) : ~550 Mo sur la RTX, ~520 Mo sur l'iGPU. D3D11 : pas de MSAA (comme WWE '12).

**Vu pendant les tests, pas lié au rendu** : 2 plantages sur 8 entrées, dans le code du jeu (`sub_82AA5458`, appelé
de `sub_82B173A0` / `sub_82AA5530`), lecture de l'adresse invitée non mappée `0x3A962680`, pendant l'entrée
d'Otunga contre Stone Cold, une fois en Vulkan et une fois en D3D12 (même adresse les deux fois). À étudier à part.

Diagnostic du 2026-11-17 (dumps lus à la main, contexte PPC à `rdi` : rN à 8×N) :
- `sub_82AA5458` est le **rejoueur de la file de commandes de rendu** du jeu : il tourne sur le fil `0xF800006C`
  (l'objet `0xAF087040` le possède : handle à +4) et `sub_82AA4C70` exécute chaque commande en appelant la
  bibliothèque D3D du jeu (`SetScissorRect`, `SetIndices`, `VertexBuffer_Unlock`…). Appelé par `sub_82AA5530`.
- Commandes de 16 octets : mot 0 = code (101 = fin de piste, 102 = fin de tout), mot 2 = nombre de mots de
  données qui suivent. Pour chaque piste (9, table à +108, 68 octets), le curseur part de piste+16 et avance de
  `mot2 + 1`. **La boucle ne compare jamais le curseur à la fin de piste** (piste+12) : elle ne s'arrête que
  sur 101 / 102.
- Au plantage, dans les deux dumps : liste `0xAEE240E0`, curseur `0xE8BB3E5A`, lecture à liste + curseur×16 =
  `0x3A962680` (non mappée). **Mêmes valeurs dans les deux plantages**, alors que le compteur d'images de l'objet
  (+40) diffère : mêmes données lues, pas un hasard de minutage.
- Côté écriture (`sub_82AA6988`, qui écrit le 102) : aucune vérification de taille de piste (0x1480 commandes par
  piste d'après la table à +160). Hypothèse principale : une piste déborde pendant certaines entrées, et le
  rejoueur lit une « longueur » qui n'en est pas une. Non prouvé : le minidump ne contient pas la mémoire des
  pistes (le rapport de plantage n'écrit qu'un petit dump).
- `wwe13.exe` (Xenos) : une partie de 9 min jouée à la main sans plantage. Pas concluant vu la fréquence (~1 sur 4).
- Sonde ajoutée (`port/src/render_queue_guard.cpp`, `--wwe13_renderq_guard=true`, **désactivée par défaut**) : le
  même rejoueur, commande pour commande, qui arrête une piste dont le curseur sort de [début, fin] et l'écrit dans
  le journal (`[wwe13-renderq]`). Essais D3D12 du 2026-11-17, Otunga contre Stone Cold :
  - avec la sonde, 4 essais sur 4 : à un instant donné, 6 à 7 des 9 pistes sont cassées **en même temps** (fin de
    piste remplie de commandes à zéro, ou codes absurdes comme 851974), et 2 à 3 ms plus tard trois fils du jeu
    (`F8000030`, `F8000050`, `F8000548`) plantent dans le gestionnaire de tâches `sub_82B18930` (lecture de
    l'adresse 8 : pointeur nul) ou l'exécuteur de commandes écrit à l'adresse 0. **La sonde ne corrige donc pas :
    elle déplace le plantage**, d'où le réglage par défaut à faux ;
  - sans la sonde (même exe) : 1 sur 2, plantage d'origine à `0x3A962680` ;
  - sans les recopies GPU → mémoire (`--svr_resolve_readback=false`) : 2 sur 2. Les recopies juste avant l'incident
    écrivent ailleurs (physique `0x154B…`–`0x1EC0…`, la file est vers `0x0E88…`–`0x0EF2…`) : elles ne sont pas en cause.
- Conclusion actuelle : le rejoueur n'est qu'une victime. À l'instant de l'incident, la mémoire des pistes **et**
  des structures du gestionnaire de tâches contient des zéros : soit une zone libérée puis réutilisée ou remise à
  zéro (sur Xbox une zone libérée garde son contenu, ici elle peut être rendue à zéro), soit une course entre fils
  du jeu à une transition de l'entrée. Le rendu natif reste suspect tant qu'un essai Xenos équivalent n'a pas été
  fait : le script de test n'arrive pas à piloter `wwe13.exe` (les touches ne sont pas prises, il part en démo).
- Pistes suivantes : (1) mettre en place des essais Xenos automatisés équivalents ; (2) un point d'arrêt matériel
  (registres de débogage) sur un mot d'une piste, qui note la fonction qui y écrit un zéro hors de
  l'enregistreur ; (3) regarder les libérations de mémoire physique (`XPhysicalFree`, `MmFreePhysicalMemory`)
  pendant l'entrée.

- **Essais Xenos automatisés (2026-11-17 soir)** : le script de test pilote bien `wwe13.exe` (`--gpu_plugin=xenos`).
  8 parcours valides Otunga contre Stone Cold avec entrées, arènes tirées au hasard : **0 plantage**. En natif, sur
  l'ensemble des essais du jour, environ 3 plantages sur 13. La probabilité d'avoir 0 sur 8 en Xenos si le taux était le
  même (~1 sur 4) est d'environ 10 % : le rendu natif est **probablement** en cause (pas prouvé). Relu : `BeginVertices`
  ne fait qu'observer, les recopies GPU → mémoire sont écartées, les structures hôtes placées en mémoire invitée
  passent par l'allocateur normal du SDK. Piste restante la plus probable : le GPU invité « null » qui libère les
  barrières (fences) aussitôt, alors que la Xbox (et l'émulation Xenos) les libère plus tard ; le jeu peut alors
  réutiliser ou remettre à zéro un tampon de la file pendant que le rejoueur le lit encore. À vérifier : retarder la
  libération des barrières du GPU null jusqu'au Present suivant, et refaire 8 essais natifs.
- **Série native du même soir** (exe actuel, Vulkan, shaders intégrés, même parcours automatisé) : **4 plantages sur
  6**, tous identiques (lecture `0x3A962680` dans `__imp__sub_82AA5458`, `wwe13_native.exe+0xE9A105`). Face à 0 sur 8 en
  Xenos, **le rendu natif déclenche bien ce plantage** (la probabilité d'un tel écart par hasard est d'environ 0,5 %).
  Le taux semble plus haut qu'avant les shaders intégrés (le démarrage ne convertit plus rien en fond) : le minutage
  compte, ce qui va dans le sens d'une course entre le fil de jeu et le rejoueur. Les dessins de re:Blue s'exécutent
  de façon synchrone dans les fonctions D3D appelées par le rejoueur : il est plus lent qu'avec Xenos (où ces
  fonctions ne font qu'écrire dans un tampon). Prochain essai : chronométrer le rejoueur par image en natif et en
  Xenos, et regarder si le fil de jeu attend vraiment la fin du rejeu avant de réécrire le même tampon.
- **Piste venue de 2K15 (2026-11-17, 21 h)** : 2K15 a eu une course de la même famille (issue #33, « Create a
  Championship » figé) : le fil principal appelle l'aide D3D `sub_82BA8358`, qui écrit dans le même anneau de
  commandes D3D que le fil de rendu, sans verrou ; « le rendu natif la déclenche en quelques minutes ». Corrigé dans
  2K15 par `port/src/render_wait_hook.cpp` (`wwe2k15_d3d_ring_lock`, une poignée de main : l'aide ne tourne que
  quand le fil de rendu est garé entre deux paquets, au Present ou dans ses attentes d'anneau / de fence).
  WWE '13 n'a **aucun** de ces crochets. Équivalents trouvés par `match_functions.py` contre le code généré de 2K15 :

  | 2K15 | WWE '13 | Rôle |
  |---|---|---|
  | `sub_82BA8358` | `sub_82B95718` | aide D3D du fil principal (shape) |
  | `sub_82BB0180` | `sub_82B9D540` | attente d'anneau (body) |
  | `sub_82BAFB78` | `sub_82B9CF38` | attente de fence (body) |
  | `sub_82BAC6B0` | `sub_82B99A70` | sondage de l'anneau (shape) |
  | `sub_82BA4738` | `sub_82B91AF8` | (body) |

  Le symptôme de WWE '13 (mémoire de la file et du gestionnaire de tâches à zéro) n'est pas celui de 2K15 (figé) :
  piste, pas preuve.
- **Poignée de main portée (2026-11-17, 21 h–22 h)** : `port/src/d3d_ring_lock.cpp`, `--wwe13_d3d_ring_lock=true`,
  **désactivée par défaut**. Le journal `[wwe13-ring]` (actif dans tous les cas, il ne fait qu'observer) montre
  que l'aide `sub_82B95718` est bien appelée par **3 fils autres que le fil de rendu** (retours `82B8F9C0`, chemin
  des textures `sub_82B8F900`, et `82B95AC8`) : environ 4 000 appels hors fil de rendu par partie.
  Même parcours automatisé (Otunga contre Stone Cold, entrées), même exe Vulkan :

  | Poignée de main | Essais | Plantages `0x3A962680` |
  |---|---|---|
  | activée | 6 | **0** |
  | coupée (série de 19 h 38) | 6 | 4 |
  | coupée (témoin de 21 h 33, même exe que la série activée) | 5 | 1 |

  Images/s identiques (39,3–39,4 de moyenne activée, 39,5–39,6 coupée : les entrées sont à 30). Avec la poignée de
  main, l'aide ne part seule (après 50 ms) que 21 fois, toutes au démarrage (environ 1 s, démarrage toujours 17 s),
  et le fil de rendu ne dépasse jamais son attente maximale. Si le taux sans correctif est d'environ 5 sur 11,
  0 sur 6 par hasard arrive environ 3 fois sur 100 ; avec le taux plus bas des essais plus anciens (~1 sur 3),
  environ 9 fois sur 100. **Très prometteur, pas prouvé** : reste désactivé par défaut (règle 6) jusqu'à plus
  d'essais et une partie jouée par iMrBurn avec `wwe13_d3d_ring_lock = true`.
- **Démenti le 2026-11-18** : avec la poignée de main activée, 2 plantages sur 4 essais valides (`vk3` en Vulkan,
  `dx12_1` en D3D12), même adresse `0x3A962680`, même instruction (`+0xE9A105`). **La poignée de main ne corrige pas
  ce plantage** (le 0 sur 6 de la veille était de la chance). Elle reste dans le code, désactivée, pour la course
  qu'elle couvre. Fait nouveau : le fil qui plante est le **fil de rendu** lui-même (même identifiant que le fil qui
  présente les images).

- **Cause trouvée (2026-11-18, 19 h)**. Piège matériel (registres de débogage x64, `port/src/renderq_watch.cpp`,
  `--wwe13_renderq_watch=true`, désactivé par défaut) sur les pointeurs de listes (+48..+59) et de pistes
  (+104..+111) de l'objet file de rendu `0xAF087040` (créé par `sub_82AA3E48`, listes `AE350000` / `AE880000`,
  alignées sur 64 Ko). Au premier plantage piégé, les écritures juste avant sont toutes du **fil de rendu** (celui
  qui plante), en alternance :
  - `sub_82AA3928` (exe+0x3E37B86) : **entrée dans une sous-liste** (sauve liste et curseur en +60.. / +68..,
    profondeur +76, met la sous-liste en liste active, curseur 0) ; appelée par la commande `sub_82B0C6E8` ;
  - `sub_82AA3988` (exe+0x7B4FE3) : retour (restaure `AE350000`), commande `sub_82B0C6F8`.

  Dernière écriture : entrée dans la sous-liste `AEE240E0`, **exactement la liste lue au plantage** (dumps du
  2026-11-17 compris), sans retour : son contenu n'est plus fait de commandes valides, le curseur part à
  `0xE8BB3E5A`. Boucle du fil de rendu `sub_82AA5530` : lance les fils de travail qui enregistrent l'image suivante
  (`sub_82B24658`, seul appelant), **puis** rejoue l'image courante (`sub_82AA5458`), puis attend les fils
  (`sub_82B24600`). Rejeu et enregistrement se chevauchent donc ; sur console le rejeu ne fait qu'écrire des paquets
  GPU et finit vite, en natif il dessine pour de vrai et dure bien plus longtemps : les sous-listes de l'image rejouée
  sont réécrites ou libérées par l'enregistrement de la suivante pendant qu'on les lit.
- **Correctif** : `--wwe13_renderq_serial=true` (`port/src/render_queue_guard.cpp`) lance les fils de travail juste
  **après** le rejeu (même travail, dans l'ordre). Même parcours automatisé, Vulkan :

  | Réglage | Essais | Plantages |
  |---|---|---|
  | `wwe13_renderq_serial = true` | 8 | **0** |
  | sans (2026-11-17 et 18, tous essais natifs de ce parcours) | 16 | 8 |

  0 sur 8 par hasard : environ 4 sur 1000 au taux mesuré (1 sur 2), environ 4 sur 100 même à 1 sur 3.
  Coût mesuré (RTX 5070 Ti) : travail du fil de rendu en match 15,7–16,0 ms par image au lieu de 12,6–12,7 ms,
  environ 59 images/s au lieu de 60 en match, entrées inchangées (30). Sur un processeur plus faible, la baisse sera
  plus forte. **Activé par défaut depuis le 2026-11-18** (décision d'iMrBurn : 16 ms par image comme WWE '12 PC) ;
  vérifié sans option : 1 parcours Vulkan, 1 parcours D3D12, aucun plantage, le journal confirme l'ordre. Pour le
  couper : `wwe13_renderq_serial = false` dans `wwe13.toml`. Piste pour réduire le coût : ne protéger que les
  sous-listes (copie ou report de leur libération) au lieu de tout l'enregistrement.
- **Mode automatique (2026-11-18, soir)** : `wwe13_renderq_serial_auto` (désactivé par défaut). Second piège
  (`--wwe13_renderq_sublist_watch=true`, diagnostic) : l'écrivain des sous-listes pendant le rejeu est
  l'enregistreur du jeu `sub_82B0E578` (commande 80) sur le fil du jeu (celui qui crée la file, pas le fil de
  rendu) ; 12 écritures attrapées, toutes pendant les **entrées** (images de 33 ms), 10 pendant un rejeu. Le mode
  automatique ordonne dès qu'une boucle de rendu dépasse 28 ms et ne rend le chevauchement qu'après 60 nouvelles
  boucles de médiane < 25 ms (match, menus). Résultats (Vulkan, RTX 5070 Ti, même parcours) :

  | Mode | Plantages | Match : images/s | Pire image | Travail | Images > 25 ms |
  |---|---|---|---|---|---|
  | automatique | **0 sur 8** | **60,0** | 26,6 ms | 12,7 ms | 2 en 160 s |
  | ordre toujours forcé (défaut) | 0 sur 8 | 59,2 | 34,1 ms | 15,7 ms | 17 en 160 s |
  | sans correction (référence) | 8 sur 16 | 60,0 | 28,9 ms | 12,8 ms | 3 en 120 s |

  Petit PC simulé (affinité 4 cœurs logiques, un essai chacun, 20 s de match) : forcé 58,4 images/s, travail
  16,3 ms ; automatique 59,9 images/s, travail 14,2 ms.
- **Profil du match (RTX, 60 images/s)** : fil de rendu à 16 % d'un cœur (81 % des échantillons en attente),
  3 fils de travail à ~11 % chacun, le jeu entier sous un cœur : sur un gros PC le jeu attend sa propre cadence.
  Le build du portage est déjà en `x86-64-v2` (sans AVX2, comme le SDK) : l'issue #70 ne concerne que l'ancien build.
- Écartés le même jour : poussées de l'anneau D3D depuis d'autres fils (observateur dans `d3d_ring_lock.cpp` :
  aucune hors de l'aide déjà couverte), remise à zéro `EVENT_WRITE_ZPD` du GPU null (bloc de 64 octets seulement).

## Attitude Era (2026-11-18)

- Natif Vulkan : menu des chapitres, fiche du match, intro « Raw is War » et entrée de Shawn Michaels (arène 1997)
  corrects ; vu aussi par iMrBurn (« ça marche le mode AE »). Xenos : même parcours correct.
- Une fois : juste après la vidéo d'introduction du mode (jouée seulement la première fois), le menu des chapitres
  ne réagissait plus à aucune touche (jeu à 60 images/s). Après redémarrage, plus de vidéo, plus de blocage. Non
  reproduit : il faudrait une sauvegarde où l'intro n'a pas encore été vue, en natif et en Xenos.

## Gameplay (demande d'iMrBurn, 2026-11-17)

- **Dégagement de l'IA au compte de 2 : trouvé (2026-11-17)**, correctif `port/src/pin_kickout.cpp`, désactivé par
  défaut (`--wwe13_pin_two_count=true`, pourcentage `--wwe13_pin_two_count_pct`, 50 par défaut).
  - Le tombé est un mini-jeu de barre sur le composant `FLMG` du catcheur au sol : barre 0..120, curseur +96, vitesse
    +100, zone verte lo +80 / hi +84, état +72 (3 = réussi = dégagement).
  - L'IA le joue par l'action 477. Le constructeur `sub_82406CD0` fixe le délai +356, le dépassement voulu +360 et
    « tente » +364 ; la mise à jour `sub_8240B330` appuie jusqu'à lo+1 puis lâche. Dans la zone, elle se dégage tout
    de suite (au compte de 1) ; au-delà, elle rate et **ne réessaie jamais** (+364 = 0), donc elle perd au compte de 3.
  - Le compte de l'arbitre est un gestionnaire à part (`[0x8347C554]`, +80 images, +84 compte, ~90 images par compte
    à 60 images/s) qui n'attend pas le mini-jeu.
  - Le correctif retarde l'appui de l'IA pour une partie de ses dégagements prévus, afin qu'elle lâche à un instant tiré
    au hasard entre le 2 et le 3. Ses ratés (défaite à 3) restent comme ils sont. Journal `[wwe13-pin]` (actif par
    défaut, `--wwe13_pin_log`) : le plan de l'IA et chaque changement d'état de la barre, avec le compte de l'arbitre.
  - Le jeu a aussi son propre dégagement « à 2,9 » (`sub_82552050`, octet +2165 du catcheur, choisi par
    `sub_8240AC80` / `sub_823FDCC8`), presque jamais vu, car l'action 477 lâche bien avant.
  - Pas encore vu en jeu.

- **Dégagement de l'IA au compte de 2** : l'ordinateur se dégage presque toujours au compte de 1, ou jamais (il
  perd au compte de 3). Chercher la décision de dégagement de l'IA lors d'un tombé (jauge / seuil / timing par
  rapport au compte de l'arbitre), répartir le moment du dégagement (1, 2, ou pas) selon les dégâts et le
  momentum ; réglage désactivable. Après la stabilisation du rendu natif.
- Avant toute release du fork : le lanceur vérifie les mises à jour sur les releases de PromethenDev
  (`launcher2/core/update.*`) : le pointer vers le dépôt du fork.
