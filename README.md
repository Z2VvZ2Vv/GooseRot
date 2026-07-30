# GooseRot

GooseRot est une réimplémentation native, autonome et *clean-room* du concept Desktop Goose. L’application joue une timeline comique de cinq minutes sur un bureau Windows : oie procédurale, bulles, compteur d’Aura, faux Bloc-notes, déplacements bornés de 67 pixels, duplication en trois oies, stickers, graffiti, filtres, compte à rebours et faux redémarrage.

La sécurité prime sur le gag : aucun profil ne provoque de BSOD réel, ne bloque le Gestionnaire des tâches, ne lit ou modifie le presse-papiers, ne redémarre Windows, ne touche au boot et ne crée de persistance. Les fenêtres qui refusent de se fermer et se dupliquent appartiennent toutes à GooseRot, sont plafonnées à neuf et disparaissent au nettoyage. `safe` est le profil par défaut, un consentement explicite précède les effets bureau et annonce ce comportement, et `Esc` maintenu deux secondes restaure le bureau puis ferme tout.

## État de l’implémentation

- moteur C++17 et timeline monotone indépendante du framerate ;
- overlay Win32/GDI+ transparent, click-through et multi-écran ;
- oie dessinée procéduralement en vue de dessus : corps, queue en éventail, ailes repliées, cou en S, bec articulé qui cacarde, pattes palmées, sourcils quand elle charge ;
- moteur de glitch piloté par la timeline : déchirures, scanlines CRT, blocs corrompus, aberration chromatique, curseurs fantômes, faux cadre « Ne répond pas », flashs ;
- tag `67` géant peint en direct à la bombe, avec coulures, overspray et une oie qui suit la buse ;
- interface tracée à main levée : bords irréguliers qui frémissent, scotch, plaques penchées ;
- trois entités indépendantes, bulles, overlays PNG embarqués et effets des cinq phases ;
- traction progressive du curseur sur 67 pixels avec verrouillage bec/pointeur visible, bornée puis restaurée ;
- déplacements optionnels des fenêtres, bornés puis restaurés ;
- fenêtres Aura/Sigma interactives, faux Bloc-notes qui refuse de se fermer et essaim de popups plafonné qui se duplique quand on le ferme — tout appartient à GooseRot ;
- fausses notifications système peintes dans l’overlay, jamais envoyées à Windows ;
- mutex mono-instance, watchdog de restauration en mémoire partagée, nettoyage idempotent et sortie d’urgence ;
- mode `--preview` fenêtré qui ne touche pas au bureau ;
- cœur AURA 67 déterministe et `GooseBootPreview.exe` sûr dans `boot/` ;
- adaptateurs freestanding UEFI x64 et BIOS, construits sur demande et vérifiés statiquement ;
- tests CTest pour les moteurs GooseRot et AURA 67, plus un harness Win32 avec fenêtre tierce réactive/lente et restauration après arrêt brutal du parent.

Le bundle firmware reste strictement expérimental : il est non signé, non installable et n’a pas été exécuté sous QEMU/OVMF ou SeaBIOS. Le build vérifie les formats, tailles, signatures, points d’entrée, sections et imports, mais pas le comportement au démarrage. Aucun installateur, chemin firmware d’écriture disque ou procédure de modification du démarrage n’est fourni, et `--boot-game` continue donc d’échouer fermé.

## Construire

Livrable cible MSVC x86 statique :

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target gooserot_release
```

Smoke build avec MinGW :

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw --parallel
ctest --test-dir build-mingw --output-on-failure
cmake --build build-mingw --target gooserot_smoke_bundle
```

Build firmware expérimental avec MinGW GNU x64 et ses binutils :

```powershell
cmake -S . -B build-firmware -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGOOSEROT_BUILD_BOOT_FIRMWARE=ON
cmake --build build-firmware --parallel
ctest --test-dir build-firmware --output-on-failure
cmake --build build-firmware --target gooseboot_firmware_bundle
```

Les images vérifiées statiquement sont placées dans `build-firmware/boot/firmware/`. La dernière commande prépare `build-firmware/boot/firmware-dist/` avec les quatre artefacts, le manifeste, les SHA-256 et un README. Ce dossier est un paquet de laboratoire non publiable : `gooseboot-manifest.json` indique `experimental-unsigned`, `installable: false` et `runtimeValidated: false`. Il ne faut ni l’écrire sur un disque physique ni l’utiliser comme chaîne de démarrage.

Le build MSVC produit le livrable contractuel x86 `/MT`. Le smoke build MinGW est utile au développement mais produit un exécutable x64. Le build firmware reste un canal de laboratoire séparé. Les assets de `Assets/Generated/` sont intégrés comme ressources : le binaire Win32 n’a besoin d’aucun dossier d’assets à l’exécution.

La cible `gooserot_release` n'existe que pour un build MSVC Win32 et prépare `dist/` avec `GooseRot.exe`, `GooseBootPreview.exe`, `README.txt` et leurs SHA-256. Les autres toolchains exposent uniquement `gooserot_smoke_bundle`, dans `smoke-dist/`, pour éviter de présenter un binaire x64 MinGW comme une release Windows 7. Le bundle firmware expérimental est séparé de ces deux distributions.

## Lancer

Commencer par la Preview sans effet système :

```powershell
build-mingw\bin\GooseRot.exe --preview --duration-scale 0.1
```

Puis, pour la démonstration desktop consentie :

```powershell
GooseRot.exe --mode safe
GooseRot.exe --mode normal
GooseRot.exe --mode lab --vm-confirmed
```

Options utiles :

```text
--start-at 02:15
--duration-scale 0.1
--primary-monitor-only
--no-desktop-effects
--fake-reboot
--boot-game
--seed 67
```

`--duration-scale 0.1` joue la timeline dix fois plus vite. En profil `lab`, `--fake-reboot` lance `GooseBootPreview.exe` après restauration et faux redémarrage si le binaire est placé à côté de GooseRot ; CMake met automatiquement les deux exécutables dans le même dossier (`bin/` en mono-configuration, `bin/Release` pour la release Visual Studio). `--boot-game` échoue volontairement : le bundle UEFI/BIOS disponible est non signé et non validé à l’exécution, donc il ne satisfait pas le contrat de confiance et aucun handoff n’est émis.

La Preview du mini-jeu se construit avec le projet racine ou séparément :

```powershell
cmake -S boot -B build\boot -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build\boot --parallel
ctest --test-dir build\boot --output-on-failure
```

Un build autonome place la Preview dans `build\boot\bin`; copiez-la à côté de `GooseRot.exe` pour utiliser `--fake-reboot`.

## Documentation

- [SCENARIO.md](SCENARIO.md) — timeline créative de cinq minutes ;
- [docs/PRODUCT_SPEC.md](docs/PRODUCT_SPEC.md) — profils et règles produit ;
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — architecture et contraintes Win32 ;
- [docs/CLEAN_ROOM_NOTES.md](docs/CLEAN_ROOM_NOTES.md) — observations de compatibilité v0.31 ;
- [docs/EXECUTION_AND_DISTRIBUTION.md](docs/EXECUTION_AND_DISTRIBUTION.md) — exécution et release ;
- [docs/SAFETY_MODEL.md](docs/SAFETY_MODEL.md) — limites et restauration ;
- [docs/BOOT_GAME.md](docs/BOOT_GAME.md) — contrat du sous-projet GooseBoot ;
- [boot/README.md](boot/README.md) — cœur AURA 67, Preview et adaptateurs firmware expérimentaux ;
- [Assets/ASSET_MANIFEST.md](Assets/ASSET_MANIFEST.md) — inventaire visuel.

## Référence et droits

La copie locale de Desktop Goose v0.31 sert uniquement à l’observation. Elle est exclue par `.gitignore` et ne fait partie ni du build ni d’une release. GooseRot ne redistribue aucun exécutable, son, mème, texte ou code décompilé officiel. Seuls le comportement observé, les constantes publiques de `GooseModdingAPI` et les créations originales de `Assets/Generated/` sont utilisés.
