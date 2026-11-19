# Fonctions Direct3D (XDK) de WWE '13

Le rendu natif (re:Blue + plume, repris de WWE '12 PC) intercepte les fonctions de la bibliothèque D3D de la
XDK, liée dans `default.xex`. Leurs adresses dans WWE '13 sont dans `port/src/native/wwe13_d3d_names.h`.

## Méthode (2026-11-17)

1. Code de WWE '12 généré avec le même SDK 0.10 (`C:\Dev\rexglue-sdk` @ `c94f5eb`), à partir du `default.xex`
   du joueur, configuration de `iMrBurn/wwe12-pc` @ `7537e14`.
2. `port/tools/match_functions.py` : chaque fonction de WWE '12 citée par le rendu natif est comparée à celles
   de WWE '13 par la structure de son code généré : même code au nombre près (« shape »), sinon similarité
   ligne à ligne ≥ 0,9 dans la fenêtre `0x82B80000-0x82BC0000` avec un écart d'au moins 0,03 sur le deuxième
   candidat (« similar »).
3. Contrôles : `D3DDevice_Swap` = `sub_82B9B2E0`, le seul appelant de `VdSwap` ; les fonctions chaudes vues au
   profileur pendant un match (`sub_82B99500` DrawIndexedVertices, `sub_82B986D8` SetVertexShader,
   `sub_82B92EE8` SetStreamSource) en font partie. Bibliothèque D3D de WWE '13 : `0x82B90000-0x82BA0000`
   (WWE '12 : `0x82A9F000-0x82AAB000`).

À faire : recensement des appels en jeu (outil `gen_d3d_census.py` de WWE '12 / 2K15) pour confirmer, et les
adresses de données (tables d'états de rendu et de samplers, cache d'états, lot de quads de texte), à retrouver
par les instructions des fonctions appariées qui les lisent.

## Table

| Fonction | WWE '12 | WWE '13 | Appariement |
|---|---|---|---|
| `D3DDevice_Present` | `82AA9000` | `82B9BA48` | shape |
| `D3DDevice_Swap` | `82AA8898` | `82B9B2E0` | similar 0.99 |
| `D3DDevice_SynchronizeToPresentationInterval` | `82AA83D8` | `82B9AE20` | shape |
| `D3DDevice_Resolve` | `82AA1028` | `82B976A8` | body |
| `D3DDevice_Clear` | `82AAA390` | `82B9CDD8` | shape |
| `D3DDevice_SetRenderTarget` | `82AA6290` | `82B93818` | shape |
| `D3DDevice_SetDepthStencilSurface` | `82AA6620` | `82B93BA8` | shape |
| `D3DDevice_DrawIndexedVertices` | `82AA2E78` | `82B99500` | shape |
| `D3DDevice_BeginVertices` | `82AA29B8` | `82B99040` | shape |
| `D3DDevice_EndVertices` | `82AA29A8` | `82B99030` | similar 1.00 |
| `D3DDevice_SetTexture` | `82A9F310` | `82B90660` | shape |
| `D3DDevice_SetVertexShader` | `82AA2050` | `82B986D8` | shape |
| `D3DDevice_SetPixelShader` | `82AA1E48` | `82B984D0` | shape |
| `D3DDevice_SetVertexDeclaration` | `82AA2268` | `82B988F0` | shape |
| `D3DDevice_SetStreamSource` | `82AA59E8` | `82B92EE8` | shape |
| `D3DDevice_SetIndices` | `82AA5B08` | `82B93090` | shape |

## Objet device : sa disposition a changé (2026-11-17)

La XDK de WWE '13 a ajouté des champs à `D3DDevice` : les positions lues en dur par re:Blue (venues de WWE '12)
doivent être traduites. Mesure : 545 des 612 fonctions de la bibliothèque D3D de WWE '12 appariées
(`match_functions.py --ref-range 82A9C000 82AC0000`), puis `port/tools/field_offset_map.py` relève les accès
`base + position` qui diffèrent à la même instruction. Plages confirmées par plusieurs sites :

| Positions WWE '12 | WWE '13 |
|---|---|
| `0xEC0`-`0x10C4` | identique |
| `0x28C4`-`0x2B90` | identique |
| `0x2FA8`-`0x34E4` | +24 (ex. `12216` → `12240`, déclaration de sommets courante) |
| `0x3500`-`0x6004` | +16 (ex. `13828` → `13844`) |

Les structures des ressources (`D3DResource`, `D3DTexture`, `D3DSurface`, tampons : `reblue/gpu/d3d.h`) sont
celles de la XDK et ne changent pas. Les positions du device lues par re:Blue / `svr_*.cpp` (`0x700`, `0x1700`,
`0x2710`, `0x3094`, ...) ne figurent pas telles quelles dans la bibliothèque : chacune sera vérifiée dans son
contexte au branchement (étape 3). `port/tools/compare_pair.py` montre les différences d'une paire de fonctions.

## Correction (2026-11-17, soir) : WWE '13 a la bibliothèque D3D de WWE 2K15

Appariement contre le code généré de WWE 2K15 PC (`C:\Dev\wwe2k15-pc2\port\generated\default`) : ses 22
fonctions crochetées ont toutes leur équivalent dans WWE '13, 15 identiques constantes comprises. Les 16
adresses ci-dessus sont confirmées, et 6 de plus sont crochetées comme dans 2K15 : `SetScissorRect`
`82B92DA0`, `D3DVertexBuffer_Unlock` `82B94970`, `D3DIndexBuffer_Unlock` `82B8E5C8`,
`CreateVertexDeclaration` `82B989F8`, `CreateVertexShader` `82B98EF0`, `CreatePixelShader` `82B98D08`.
`wwe13_d3d_names.h` suit donc le jeu de crochets et les interrupteurs de 2K15 (seuls `SVR_NO_DRAW_VERTICES`
et `WWE12_NO_DESC_HOOKS`), et plus ceux de WWE '12 : le rendu copié de 2K15 en dépend (shaders créés au
Create*, tampons au Unlock). Premier lancement avec les réglages de WWE '12 : son et 60 images/s, image noire.
