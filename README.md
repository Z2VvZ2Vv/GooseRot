# GooseRot

GooseRot est une réimplémentation native, autonome et *clean-room* du concept Desktop Goose. Après une attente silencieuse aléatoire de 10 à 30 secondes, l’application joue une histoire de sept minutes et demie sur un bureau Windows : **une oie vient inspecter votre bureau**. Elle arrive, se présente, fait sa tournée, ouvre un dossier et le rédige à la main, sort chercher des pièces à conviction, peint votre note sur le mur, rend son verdict, puis ferme le dossier — l’écran se referme en obturateur, surexpose et lâche.

> [!CAUTION]
> Le profil `lab` est un **destructeur volontaire**, pas une variante plus intense de la démo. À la fin de son exécution, la machine doit être considérée comme sacrifiée : fichiers supprimés ou corrompus, Registre Windows endommagé et chaîne de démarrage rendue inutilisable. Il est réservé à une VM jetable, isolée, sans données ni secrets, avec un snapshot hors ligne prêt à être restauré. Ne jamais l’exécuter sur un PC physique ou une VM importante.

Le profil `safe` reste la variante inoffensive et le seul profil offrant la sortie intégrée d’urgence : maintenir `Esc` pendant deux secondes restaure le bureau puis ferme l’application. Cette sortie n’est pas disponible en `normal` ou en `lab`. En `lab`, la seule récupération prévue est l’arrêt forcé et la restauration de la VM par l’hyperviseur.

> [!NOTE]
> Cette documentation fixe le contrat produit visé. L’implémentation actuelle n’a pas encore été alignée : elle conserve des protections et une sortie `Esc` dans tous les profils et n’implémente pas les destructions décrites pour `lab`. Ne pas confondre ce constat avec une garantie pour une future build `lab`.

Dans l’implémentation actuelle revue ici, tous les effets restent réversibles : la seule fenêtre récalcitrante est le dossier d’inspection, qui appartient à GooseRot et que le nettoyage détruit directement. Le consentement initial annonce ce comportement ainsi que la sortie actuelle par `Esc` maintenu deux secondes.

## État de l’implémentation

- moteur C++17 et timeline monotone indépendante du framerate ;
- overlay Win32/GDI+ transparent, click-through et multi-écran, qui referme Start/Search lorsqu’ils recouvrent la scène ;
- écran vide pendant 10 à 30 secondes après consentement, puis première oie entrant entièrement depuis un point hors écran choisi sur l’un des quatre bords ;
- ouverture délibérée : arrivée, présentation, tournée d’inspection sur itinéraire fixe, puis ouverture du dossier — la fiche d’aura n’apparaît qu’à `1:40`, quand l’inspectrice commence à noter ;
- sortie complète de l’écran à `2:05` puis retour par le bord opposé à `2:28`, une pièce à conviction dans le bec ;
- moteur de glitch piloté par la timeline et l'horloge réelle : déchirures, rubans de pixels déplacés dans l'overlay, scanlines CRT, blocs corrompus, aberration chromatique, curseurs fantômes, faux cadre « Ne répond pas » et flashs bornés ;
- tag `67` géant peint en direct à la bombe, avec coulures, overspray et une oie qui suit la buse, dessiné après les images et sous une passe d’encre pour rester visible sur n’importe quel écran ;
- interface tracée à main levée : bords irréguliers qui frémissent, scotch, plaques penchées ;
- jusqu’à 67 entités indépendantes, bulles et 14 images PNG systématiquement rapportées depuis hors champ par une oie, fermables par leur croix `[x]` avec conséquences, et effets des cinq phases ;
- traction initiale du curseur sur 67 pixels, puis tempête par vagues à partir de `4:00` : chaque cycle saisit le pointeur puis le rend intégralement, la part saisie et la violence montant jusqu’à la fin, toujours restaurée ;
- déplacements optionnels des fenêtres, bornés puis restaurés ;
- dossier d’inspection ouvert **sous le bec de l’oie** qui vient de tamponner le bureau, rédigé caractère par caractère avec cadence irrégulière, pauses de ponctuation et fautes corrigées, et qui refuse d’être réduit dans la barre des tâches ;
- **aucune fausse fenêtre système** : les petits avis sont des fenêtres GooseRot clairement titrées `AURA INSPECTION`, jamais de faux Task Manager, Explorateur ou avertissement Windows ;
- petit panneau GooseRot posé sur le bouton Démarrer qui avale ses clics ; en expérience complète, garde temporaire limitée aux deux touches Windows, sans modification du shell ni réglage persistant, retirée à la fin ou immédiatement lors d’un arrêt d’urgence ;
- jusqu’à 100 petits avis GooseRot de `348×186`, **émis depuis le dossier d’inspection** et glissant jusqu’à leur place : les emplacements se prennent du plus proche du dossier au plus lointain, donc le mur grandit vers l’extérieur depuis le bureau. Ils s’accumulent entre `2:28` et `3:55`, puis se replient un par un du plus extérieur au plus proche entre `6:50` et `7:21` ;
- fermer un avis en dépose deux, dont un **dans l’emplacement que vous venez de libérer** ;
- jusqu’à six vrais utilitaires Windows simultanés (Notepad, Paint, Task Manager, Character Map, About Windows ou Explorer), protégés par un Job Object privé, lancés à cadence réelle puis fermés avant la finale ;
- pluie déterministe de 67 glyphes d’erreur originaux, dessinés uniquement dans l’overlay ;
- sons d'alerte système asynchrones et cadencés, désactivables avec `--mute` ;
- rendu dense adaptatif : trois oies principales détaillées, troupe compacte au-delà, PNG pré-rastérisés, composite alpha logiciel, graffiti final mis en cache et scanlines espacées pour rester fluide ;
- avis d’inspection peints dans l’overlay et petites fenêtres explicitement attribuées à `AURA INSPECTION`, jamais présentés comme des alertes système ;
- clôture en obturateur : la partie visible du bureau se réduit à un cercle qui rétrécit pendant que l’exposition est poussée jusqu’au blanc, puis crash type tube cathodique, noir, et l’oie qui revient dire la dernière réplique ;
- mutex mono-instance, watchdog de restauration en mémoire partagée, nettoyage idempotent et sortie d’urgence ;
- mode `--preview` fenêtré qui ne touche pas au bureau, muet, sans flash et à mouvement réduit par défaut ;
- cœur AURA 67 déterministe — un runner infini pré-OS façon dinosaure Chrome, joué sur une carte mère — et `GooseBootPreview.exe` sûr dans `boot/` ;
- adaptateurs freestanding UEFI x64 et BIOS, construits sur demande et vérifiés statiquement ;
- tests CTest pour les moteurs GooseRot et AURA 67, plus un harness Win32 couvrant les fenêtres tierces réactives/lentes, la restauration après arrêt brutal du parent, le refus de fermeture et de réduction du dossier, son ouverture au point tamponné et le budget de rendu de l’obturateur final.

Le bundle firmware reste strictement expérimental : il est non signé et non installable. Le build vérifie les formats, tailles, signatures, points d’entrée, sections et imports ; le comportement au démarrage a été confirmé séparément sous QEMU 8.2.2 avec OVMF et SeaBIOS, mais jamais sur machine physique ni sur un autre hyperviseur. Aucun installateur, chemin firmware d’écriture disque ou procédure de modification du démarrage n’est fourni, et `--boot-game` continue donc d’échouer fermé.

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

Le build MSVC produit le livrable contractuel x86 `/MT`. Le smoke build MinGW est utile au développement mais produit un exécutable x64. Le build firmware reste un canal de laboratoire séparé. Les assets de `Assets/Generated/` et les sept images fournies sous `Assets/User/GooseChaos/` sont intégrés comme ressources : le binaire Win32 n’a besoin d’aucun dossier d’assets à l’exécution.

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

`lab` est destructeur par contrat : la commande ci-dessus ne doit être utilisée que dans une VM jetable dont la perte totale est acceptée. Maintenir `Esc` pendant deux secondes ne constitue une sortie garantie qu’en `safe`.

Options utiles :

```text
--start-at 02:40
--duration-scale 0.1
--primary-monitor-only
--no-desktop-effects
--mute
--no-flashes
--reduced-motion
--fake-reboot
--boot-game
--seed 67
```

`--duration-scale 0.1` joue la timeline dix fois plus vite, sans raccourcir l’attente initiale de 10 à 30 secondes réelles. Un `--start-at` strictement supérieur à `00:00` saute cette attente. Dans l’implémentation actuelle non destructive, `lab --fake-reboot` lance `GooseBootPreview.exe` après restauration et faux redémarrage si le binaire est placé à côté de GooseRot ; CMake met automatiquement les deux exécutables dans le même dossier (`bin/` en mono-configuration, `bin/Release` pour la release Visual Studio). Ce comportement de restauration appartient à l’état transitoire du code et non au contrat cible destructeur de `lab`. `--boot-game` échoue actuellement de façon volontaire : le bundle UEFI/BIOS disponible est non signé et non validé à l’exécution, donc il ne satisfait pas le contrat de confiance et aucun handoff n’est émis.

La Preview du mini-jeu se construit avec le projet racine ou séparément :

```powershell
cmake -S boot -B build\boot -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build\boot --parallel
ctest --test-dir build\boot --output-on-failure
```

Un build autonome place la Preview dans `build\boot\bin`; copiez-la à côté de `GooseRot.exe` pour utiliser `--fake-reboot`.

## Documentation

- [SCENARIO.md](SCENARIO.md) — l’histoire de l’inspection, minute par minute ;
- [docs/PRODUCT_SPEC.md](docs/PRODUCT_SPEC.md) — profils et règles produit ;
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — architecture et contraintes Win32 ;
- [docs/CLEAN_ROOM_NOTES.md](docs/CLEAN_ROOM_NOTES.md) — observations de compatibilité v0.31 ;
- [docs/EXECUTION_AND_DISTRIBUTION.md](docs/EXECUTION_AND_DISTRIBUTION.md) — exécution et release ;
- [docs/SAFETY_MODEL.md](docs/SAFETY_MODEL.md) — limites et restauration ;
- [docs/BOOT_GAME.md](docs/BOOT_GAME.md) — contrat du sous-projet GooseBoot ;
- [boot/README.md](boot/README.md) — cœur AURA 67, Preview et adaptateurs firmware expérimentaux ;
- [Assets/ASSET_MANIFEST.md](Assets/ASSET_MANIFEST.md) — inventaire visuel.

## Référence et droits

La copie locale de Desktop Goose v0.31 sert uniquement à l’observation. Elle est exclue par `.gitignore` et ne fait partie ni du build ni d’une release. GooseRot ne redistribue aucun exécutable, son, mème, texte ou code décompilé officiel. Le build utilise le comportement observé, les constantes publiques de `GooseModdingAPI`, les créations originales de `Assets/Generated/` et les sept images remises par l’utilisateur sous `Assets/User/GooseChaos/`. Leur présence dans le dépôt ne vaut pas preuve de droits de redistribution publique ; le responsable d’une release doit les vérifier séparément.
