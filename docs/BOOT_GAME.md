# GooseBoot — jeu pré-OS BIOS et UEFI

> État actuel : le cœur déterministe, le renderer logiciel, les tests, la Preview Win32 et les adaptateurs UEFI x64/BIOS sont implémentés dans `boot/`. Les images firmware compilent, passent la vérification statique de format et de layout, et démarrent désormais en émulation : les deux ISO ont été exécutées sous QEMU 8.2.2 avec OVMF 2024.02 (UEFI x64) et SeaBIOS 1.16.3 (BIOS). Le jeu s’affiche, le clavier relance une partie, `R` provoque un vrai reset plateforme et `Esc` rend la main au firmware avec restauration du mode GOP. Elles restent expérimentales, non signées, non installables, et non validées sur machine physique ni sur les autres hyperviseurs. GooseRot refuse donc toujours `--boot-game` et n’émet aucun handoff.

## Objectif

GooseBoot vise un véritable mini-jeu pré-OS, indépendant de Windows. Il ne s’agit ni d’une vidéo, ni d’un faux écran de firmware, ni d’une ISO. Le dépôt fournit actuellement :

- une application UEFI au format PE/COFF ;
- un chargeur BIOS minimal et son programme de jeu 32 bits ;
- un cœur de gameplay commun et déterministe ;
- un preview Windows utilisant exactement le même moteur de jeu ;
- un bundle de laboratoire avec manifeste et SHA-256.

Le banc d’exécution QEMU/OVMF/SeaBIOS est en place et vert ; la validation sur matériel physique et sur les autres hyperviseurs reste à faire, exclusivement sur des supports virtuels vierges et jetables.

GooseBoot ne contient aucun installateur et ne modifie pas le démarrage d’une machine. Le branchement au profil `lab`, la détection BIOS/UEFI et l’installation expérimentale restent derrière une frontière d’intégration externe.

## Nom et concept

### AURA 67: POST Runner

Un runner infini, dans l’esprit du dinosaure hors ligne de Chrome, mais joué sur une carte mère. L’oie court toute seule, la carte défile de plus en plus vite, et la partie ne s’arrête que lorsqu’elle se prend un obstacle. Il n’y a ni chronomètre ni ligne d’arrivée : seul le score compte, et le meilleur reste affiché dans le HUD.

- sauter par-dessus les barrettes de RAM, condensateurs, boîtes `SEGFAULT`, tours et écrans bleus ;
- se baisser sous les curseurs hostiles volants, mais rester au sol quand ils passent haut ;
- frôler les obstacles pour déclencher un `CLUTCH` et monter la chaîne ;
- ramasser les badges `+67 AURA` placés sur l’arc du saut ;
- attraper la puce d’overclock pour armer `ROOT MODE` pendant exactement 67 ticks et pulvériser les obstacles ;
- pousser le compteur au-delà de `9999` pour allumer `MAX BRAINROT`.

L’économie garde les valeurs du projet : 1 AURA tous les 5 pixels parcourus, `67` par badge, `67` par frôlement, `67` par obstacle pulvérisé, palette firmware qui bascule tous les `670` AURA. La chaîne (jusqu’à `x9`) se construit uniquement par la prise de risque — jamais en ramassant des badges — et retombe après cinq secondes sans action. La vitesse monte de 345 à 622 pixels/seconde en environ 79 secondes puis se stabilise, la densité d’obstacles restant à son plafond.

Un saut au sol dure exactement 25 ticks et culmine à 86 pixels. Chaque vague produite par le générateur tient dans cet arc : l’écart minimal tiré est de 36 ticks, et les motifs se débloquent progressivement avec la rampe.

À la mort :

```text
KERNEL PANIC
AURA 004420   BEST 004420
[SPACE] RUN AGAIN    [R] RESET PLATFORM
```

Le panneau affiche aussi les badges, les frôlements, les obstacles franchis et la durée de survie. Un verrou de 20 ticks empêche la touche qui a causé le crash de sauter l’écran. Le jeu n’utilise aucun son. Les retours reposent sur le mouvement, les flashes modérés, les secousses et les changements de palette.

## Contrôles

| Entrée | Action |
|---|---|
| `Space`, `Haut` ou `W` | Sauter, puis un seul coup d’aile en l’air |
| `Bas` ou `S` | Se baisser au sol, plonger en l’air |
| `Space` ou `Enter` sur l’écran de panique | Relancer immédiatement |
| `R` sur l’écran de panique | Demander un redémarrage de la plateforme |
| `Esc` maintenu deux secondes | Fermer la Preview, retourner au firmware en UEFI ou arrêter le CPU en BIOS |

Ces contrôles décrivent le sous-projet autonome GooseBoot dans son état actuel. Ils ne constituent pas une sortie du profil GooseRot `lab` : au niveau produit, l’appui long sur `Esc` est réservé à GooseRot `safe`. Le raccordement actuel de `lab --fake-reboot` à la Preview est un comportement transitoire non aligné avec ce contrat.

La souris n’est pas utilisée.

## Artefacts expérimentaux produits

Avec l’option firmware activée, les images vérifiées statiquement sont générées dans `<build>/boot/firmware/`. La cible explicite `gooseboot_firmware_bundle` prépare `<build>/boot/firmware-dist/` :

```text
GooseBootX64.efi
gooseboot-bios-stage1.bin
gooseboot-bios-stage2.bin
gooseboot-bios.img
gooseboot-manifest.json
SHA256SUMS.txt
README.txt
```

Le dossier `<build>/boot/firmware/` contient en plus `gooseboot-bios-cdstub.bin`, le secteur d’amorçage utilisé uniquement par l’ISO BIOS.

Il n’existe volontairement aucun `installer.exe`, script d’écriture MBR/ESP, fichier BCD ou modificateur de variables UEFI dans ce périmètre.

Le manifeste contient le schéma, le jeu, la version, les tailles et SHA-256 des quatre artefacts, ainsi que les barrières de confiance `experimental-unsigned`, `installable: false` et `runtimeValidated: false`. Il ne décrit aucune procédure d’installation sur un disque système. Aucune image UEFI IA32 n’est produite.

### Construction

La cible firmware requiert Windows, MinGW GNU x64 et les GNU binutils `ld`, `objcopy` et `objdump` :

```powershell
cmake -S . -B build-firmware -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGOOSEROT_BUILD_BOOT_FIRMWARE=ON
cmake --build build-firmware --parallel
ctest --test-dir build-firmware --output-on-failure
cmake --build build-firmware --target gooseboot_firmware_bundle
```

La deuxième commande construit `gooseboot_firmware` et exécute automatiquement la vérification de layout. `ctest` la rejoue sous le nom `gooseboot_firmware_layout`. La dernière commande ne signe ni n’installe rien ; elle copie seulement les résultats dans le dossier de staging expérimental.

Depuis Linux, la même construction fonctionne avec la chaîne croisée mingw-w64 et `boot/cmake/toolchain-mingw-w64.cmake`. Une construction croisée produit tous les binaires firmware sauf `gooseboot-bios.img` et n’expose pas `gooseboot_firmware_bundle` : cet artefact est assemblé par un outil qui doit tourner sur la machine de build. Les deux ISO n’en ont pas besoin.

### ISO de test

`boot/tools/make_boot_isos.sh` fabrique deux ISO destinées à une machine virtuelle jetable, à partir d’un firmware déjà construit. Le script requiert `xorriso`, `mtools` et `dosfstools`, et n’écrit que des fichiers ordinaires dans le dossier de sortie demandé :

```bash
./boot/tools/make_boot_isos.sh build-firmware/firmware build-firmware/iso
```

| Image | Catalogue El Torito | Ce que le firmware exécute |
|---|---|---|
| `gooseboot.iso` | entrée BIOS par défaut, puis entrée EFI derrière un en-tête de section de plateforme | l’un ou l’autre, selon le type de firmware de la VM |
| `gooseboot-bios.iso` | entrée BIOS par défaut uniquement | `gooseboot-bios-cdstub.bin` suivi du stage 2 |

L’en-tête de section est déterminant. Un catalogue peut aussi ne contenir qu’une seule entrée dont l’identifiant de plateforme est porté par l’entrée de validation : OVMF démarre dessus sans broncher, mais un firmware qui ne cherche l’EFI que dans les en-têtes de section de plateforme `0xEF` — VMware compris — ne voit alors aucune entrée EFI. La disposition à deux entrées est celle de toutes les ISO d’installation grand public. L’ESP est en FAT16 plutôt qu’en FAT12 pour la même raison : les deux sont autorisés pour un support amovible, FAT16 est mieux supporté.

Le stage 1 n’est pas utilisable depuis un lecteur optique : son chemin `INT 13h/AH=42h` suppose des secteurs de 512 octets alors qu’un amorçage CD sans émulation en expose de 2048. Le stub `platform/bios/cdrom_stub.S` le remplace par un simple déplacement mémoire — le firmware charge l’image complète, le stub ne fait donc aucun appel disque, et le vérificateur de layout impose l’absence d’opcode `INT 13h` dans ce secteur. Il ne dépend que de l’adresse de chargement par défaut, donc aucun firmware n’a besoin d’honorer un segment El Torito personnalisé.

## Arborescence active résumée

```text
boot/
 ├─ CMakeLists.txt
 ├─ common/
 │   ├─ framebuffer.h
 │   ├─ input.h
 │   ├─ arena.h
 │   └─ random.h
 ├─ game/
 │   ├─ game.h / game.cpp
 │   └─ renderer.cpp
 ├─ platform/
 │   ├─ uefi/
 │   │   ├─ efi_main.cpp
 │   │   ├─ uefi_base.h / uefi_crt.cpp
 │   │   ├─ uefi_graphics.*
 │   │   ├─ uefi_input.*
 │   │   └─ uefi_clock.*
 │   ├─ bios/
 │   │   ├─ stage1.S / stage1.ld
 │   │   ├─ stage2_entry.S / linker.ld
 │   │   ├─ cdrom_stub.S / cdrom_stub.ld
 │   │   ├─ bios_main.cpp
 │   │   ├─ bios_graphics.cpp
 │   │   ├─ bios_input.cpp
 │   │   └─ bios_clock.cpp
 │   └─ preview/
 │       └─ win32_preview.cpp
 ├─ cmake/
 │   ├─ verify_firmware.cmake
 │   └─ package_firmware.cmake
 ├─ tools/
 │   ├─ make_bios_image.cpp
 │   └─ make_boot_isos.sh
 └─ tests/game_tests.cpp
```

## Cœur portable

Le gameplay ne connaît ni UEFI, ni BIOS, ni Win32. Il expose trois opérations (`game_initialize`, `game_tick`, `game_render`) et échange uniquement des structures fixes :

```text
GameState   → état déterministe complet
InputState  → touches maintenues, pressées et relâchées
FrameBuffer → base, largeur, hauteur, stride et format des pixels
GameSignal  → aucune action, sortie ou demande de redémarrage
```

Contraintes :

- aucune allocation dynamique ;
- état, pools d’entités et framebuffers de taille fixe ;
- aucune exception C++, RTTI ou bibliothèque standard dynamique ;
- rendu logiciel en 32 bits ;
- résolution interne 640×360, mise à l’échelle entière si possible ;
- oie, entités et police rendues sans asset externe ;
- boucle fixe à 30 Hz, interpolation de rendu facultative ;
- fonctionnement avec 32 Mo de RAM ou moins.

## Cible UEFI

`GooseBootX64.efi` est une application PE32+ avec le sous-système `IMAGE_SUBSYSTEM_EFI_APPLICATION`. Elle reste dans l’environnement Boot Services pendant toute la partie.

Services utilisés :

- Graphics Output Protocol pour obtenir le framebuffer ;
- Simple Text Input Protocol pour le clavier ;
- événements/timers Boot Services pour la cadence ;
- `SetWatchdogTimer(0, …)` pour désarmer le watchdog hérité du Boot Manager ;
- Runtime Services `GetTime` pour un rattrapage temporel borné ;
- Runtime Services `ResetSystem` uniquement lorsque le joueur choisit `R`.

Le jeu n’appelle pas `ExitBootServices` : ce n’est pas un noyau ni un chargeur d’OS. Son état et son framebuffer sont statiques ; les descriptions allouées par `QueryMode` sont immédiatement libérées avec `FreePool`. Il ne localise aucun protocole disque ou système de fichiers.

Si le mode GOP actif est trop petit, l’adaptateur énumère les modes, choisit le plus petit mode d’au moins 640×360, puis restaure le mode d’origine en sortant. Un framebuffer direct 32 bits compatible est mis à l’échelle entière et centré ; sinon, le fallback GOP Blt affiche l’image 640×360 à l’échelle 1:1, car Blt ne fournit pas de scaling. Simple Text Input ne signale pas le relâchement : une lease bornée approxime les directions maintenues à partir du repeat firmware, tandis qu’un appui explicite sur `Esc` quitte immédiatement l’application UEFI. Le timer reste la cadence principale ; `GetTime` rattrape au plus cinq secondes lorsque des événements binaires sont coalescés, avec repli à un tick par événement si l’horloge firmware est indisponible ou incohérente.

Seule la cible UEFI x86-64 est construite. `ResetSystem` n’est appelé qu’après la fin de la partie et un appui explicite sur `R`. Aucun de ces comportements n’a encore été validé sous OVMF ou sur firmware réel.

## Cible BIOS

La cible BIOS est séparée en deux binaires :

- `stage1` fait exactement 512 octets, exige les extensions EDD et lit les 127 secteurs fixes de son propre `stage2` depuis les LBA 1 à 127 avec `INT 13h/AH=42h`, avec trois tentatives bornées ;
- `stage2` fait 65 024 octets, sélectionne un mode VBE, vérifie/active A20, passe en mode protégé 32 bits, initialise l’état puis lance le cœur commun à `0x10000`.

Fonctions utilisées avant le mode protégé :

- lecture du propre programme par les services disque BIOS ;
- sélection d’un mode framebuffer VBE compatible ;
- vérification d’au moins 2 Mio de RAM ;
- test d’alias A20, activation rapide puis fallback 8042 borné si nécessaire ;
- chargement d’une GDT minimale.

Le mode VBE doit être un framebuffer linéaire direct-color 32 bits d’au moins 640×360, avec canaux RGB/BGR de 8 bits, pitch valide et adresse physique d’au moins 2 Mio. En mode réel, toutes les données locales utilisent des offsets relatifs à `CS=0x1000` sous 64 KiB ; la liste de modes VBE retournée en ROM reste un vrai pointeur segment:offset. Stage 1 effectue au plus trois fois le même transfert EDD de 127 secteurs, avec reset disque entre deux échecs. Après le passage en mode protégé, le jeu utilise un rendu 640×360 mis à l’échelle entière, suppose un clavier PS/2 ou une émulation legacy en scan-code set 1 et interroge le canal 2 du PIT à 30 Hz sans son. `Esc` arrête le CPU ; `R`, uniquement sur l’écran final, tente un reset par contrôleur clavier puis port chipset. Ces chemins n’ont pas été exécutés sous SeaBIOS. Aucun chemin de code n’écrit sur un disque : seule la lecture du propre `stage2` existe dans `stage1`.

## Assets pré-OS

Les adaptateurs pré-OS ne décodent aucun PNG/SVG et n’accèdent à aucun système de fichiers. L’oie, les obstacles, le décor de carte mère à trois couches de parallaxe, les palettes Matrix Green, Amber, Cyan et Neon Pink, ainsi que la police bitmap, sont rendus par le même renderer logiciel compilé dans chaque binaire.

## Futur handoff `--boot-game`

Après signature et validation runtime des artefacts firmware dans un environnement de laboratoire séparé, la présence de `--boot-game` dans le profil `lab` pourra signifier :

1. jouer toute la timeline Windows ;
2. restaurer le curseur et les fenêtres puis retirer tous les overlays ;
3. vérifier l’intégrité SHA-256 des artefacts GooseBoot ;
4. produire un `boot-game-handoff.json` non privilégié ;
5. quitter avec le code de processus `67`.

Le handoff contient :

```json
{
  "schema": 1,
  "request": "boot-game",
  "preferredFirmware": "auto",
  "game": "aura-67",
  "seed": 67
}
```

La version actuelle refuse l'option avant la timeline, n'écrit aucun JSON et ne retourne pas le code `67`. La présence d’un bundle local ne change rien : son manifeste le déclare non signé, non installable et non validé à l’exécution. À terme, GooseRot ne choisira et n’installera toujours aucun artefact de boot. L’intégrateur de laboratoire externe sera responsable de détecter le firmware, sélectionner un binaire signé et validé, puis réaliser toute expérimentation sur son propre environnement. Ce dépôt ne documente pas le remplacement de Windows Boot Manager, l’écriture ESP/MBR ou la modification des variables de boot.

## Tests

Les tests automatisés couvrent le cœur déterministe et le renderer via `gooseboot_tests` : rejeu à seed égal sur une session entière (crashs et relances comprises), divergence de seed, arc de saut fixe 25 ticks / 86 pixels, coup d’aile unique par phase aérienne, esquive accroupie d’un curseur à hauteur de tête, économie `67` et règles de chaîne, `ROOT MODE` qui pulvérise puis expire à l’heure, 10 000 ticks prouvant qu’aucun compteur ne termine la partie, écarts du générateur toujours au-dessus de la borne de saut sur six seeds, autopilote réactif survivant à la rampe sur cinq seeds, conservation du record entre deux runs, demande de reset accessible uniquement après un crash, sortie `Esc` maintenue, garde-fous du framebuffer, frames identiques et alignement de l’arène fixe.

`gooseboot_firmware_layout` vérifie statiquement :

- UEFI : PE32+ x86-64, sous-système EFI Application, entrée non nulle, absence d’import DLL, relocation `DIR64` et BSS framebuffer allouée sans contenu fichier ;
- BIOS : stage 1 de 512 octets avec signature `55 AA`, stage 2 de 127 secteurs, image combinée de 128 secteurs et contenu exact ;
- stage 2 : format i386, entrée `0x10000` et symboles utilisés en mode réel confinés à la fenêtre CS-relative de 64 KiB ;
- stub CD, quand il est fourni : 512 octets, signature `55 AA` et aucun opcode `INT 13h` dans le secteur.

Ces contrôles ne démarrent pas les images. Le banc runtime exécuté séparément couvre désormais :

- UEFI x64 sous OVMF 2024.02 : démarrage depuis l’ISO, rendu 1280×720 letterboxé dans un mode GOP 1280×800, clavier, relance, `R` = reset plateforme réel, `Esc` = retour au menu firmware avec restauration du mode d’origine ;
- BIOS sous SeaBIOS 1.16.3 : démarrage depuis l’ISO via le stub CD, mode VBE 640×400, clavier PS/2 et relance.

Reste à valider : matériel physique, VMware, VirtualBox et Hyper-V, les résolutions 640×480, 800×600, 1024×768 et 1920×1080, et le clavier AZERTY. Ces tests doivent utiliser exclusivement des supports virtuels temporaires, vierges et jetables.

## Critères de fin

Le sous-projet GooseBoot est considéré terminé lorsque :

- le même seed produit la même session sur Preview, BIOS et UEFI ;
- le jeu démarre sans Windows et sans réseau ;
- les quatre palettes et tous les textes sont lisibles ;
- `R` redémarre correctement sur les plateformes de test ;
- aucune écriture disque n’est possible depuis les binaires livrés ;
- l’installation sur une machine existante reste entièrement extérieure au dépôt.

## Références techniques

- UEFI Specification 2.11 : Graphics Output Protocol, Simple Text Input Protocol et Runtime Services ;
- format Microsoft PE/COFF : sous-système EFI Application ;
- documentation TianoCore EDK II pour la structure d’une application UEFI.
