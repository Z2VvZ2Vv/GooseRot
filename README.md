# GooseRot

GooseRot est une réimplémentation native, autonome et *clean-room* du concept Desktop Goose. Après une attente silencieuse aléatoire de 10 à 30 secondes, l’application joue une histoire de sept minutes et demie sur un bureau Windows : **une oie vient inspecter votre bureau**. Elle arrive, se présente, fait sa tournée, ouvre un dossier et le rédige à la main, sort chercher des pièces à conviction, peint votre note sur le mur, rend son verdict, puis ferme le dossier — l’écran se referme en obturateur, surexpose et lâche.

> [!CAUTION]
> Le profil `lab` est un **destructeur volontaire**, pas une variante plus intense de la démo. À la fin de son exécution, la machine doit être considérée comme sacrifiée : fichiers supprimés ou corrompus, Registre Windows endommagé et chaîne de démarrage rendue inutilisable. Il est réservé à une VM jetable, isolée, sans données ni secrets, avec un snapshot hors ligne prêt à être restauré. Ne jamais l’exécuter sur un PC physique ou une VM importante.

Le profil `safe` reste la variante inoffensive et le seul profil offrant la sortie intégrée d’urgence : maintenir `Esc` pendant deux secondes restaure le bureau puis ferme l’application. Cette sortie n’est pas disponible en `normal` ou en `lab`. En `lab`, la seule récupération prévue est l’arrêt forcé et la restauration de la VM par l’hyperviseur.

> [!NOTE]
> Cette documentation fixe le contrat produit visé. L’implémentation actuelle n’a pas encore été alignée : elle conserve des protections et une sortie `Esc` dans tous les profils et n’implémente pas les destructions décrites pour `lab`. Ne pas confondre ce constat avec une garantie pour une future build `lab`.

Dans l’implémentation actuelle revue ici, tous les effets restent réversibles : la seule fenêtre récalcitrante est le dossier d’inspection, qui appartient à GooseRot et que le nettoyage détruit directement. `GooseRot-Safe.exe` et `GooseRot-Normal.exe` conservent le dialogue initial ; `GooseRot-Lab.exe` démarre directement, sans avertissement ni confirmation, et reste réservé à une VM jetable.

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
- jusqu’à 100 petits avis GooseRot de `348×186`, créés instantanément sans fondu, glissement ni secousse et répartis de façon déterministe sur toute la zone de travail : 1 avis à `3:10`, 12 à `3:55`, 42 à `5:15`, 80 à `6:15`, puis le plafond de 100 au compte à rebours ; ils se replient ensuite dans l’ordre inverse de création avant l’obturateur final ; avant `3:55`, un avis fermé reste fermé sans réaction ni remplacement ;
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

La cible `gooserot_release` n'existe que pour un build MSVC Win32 et prépare `dist/` avec `GooseRot-Safe.exe`, `GooseRot-Normal.exe`, `GooseRot-Lab.exe`, `GooseBootPreview.exe`, `README.txt` et leurs SHA-256. Les autres toolchains exposent uniquement `gooserot_smoke_bundle`, dans `smoke-dist/`, pour éviter de présenter un binaire x64 MinGW comme une release Windows 7. Le bundle firmware expérimental est séparé de ces deux distributions.

## Lancer

Commencer par la Preview sans effet système :

```powershell
build-mingw\bin\GooseRot-Safe.exe --preview --duration-scale 0.1
```

Puis, pour la démonstration desktop consentie :

```powershell
GooseRot-Safe.exe
GooseRot-Normal.exe
GooseRot-Lab.exe
```

`lab` est destructeur par contrat : le binaire dédié ne présente aucun dialogue de lancement interne et ne doit être utilisé que dans une VM jetable dont la perte totale est acceptée. Windows demande toutefois l’élévation administrateur avant chaque lancement de `GooseRot-Lab.exe`. Un refus annule le lancement ; l’application ne relance pas la demande en boucle. `GooseRot-Safe.exe` et `GooseRot-Normal.exe` restent en `asInvoker`. Chaque exécutable verrouille son profil ; `--mode` ne permet pas de transformer une variante en une autre. Maintenir `Esc` pendant deux secondes ne constitue une sortie garantie qu’en `safe`.

Au démarrage, `GooseRot-Lab.exe` détecte réellement si Windows a démarré via BIOS ou UEFI. Les artefacts sont embarqués dans les ressources du seul exécutable Lab, qui extrait dans `%TEMP%\GooseRot-Lab\` uniquement ce qui correspond à la machine : `GooseBootX64.efi` en UEFI, ou `gooseboot-bios-stage1.bin` et `gooseboot-bios-stage2.bin` en BIOS. Le type détecté et les chemins sont disponibles dans `LabStartupArtifacts` juste après `RunLabStartup`, ainsi que dans `GOOSEROT_FIRMWARE_TYPE`, `GOOSEROT_LAB_UEFI`, `GOOSEROT_LAB_BIOS_STAGE1` et `GOOSEROT_LAB_BIOS_STAGE2`. Cette extraction n’installe rien et ne modifie ni le Registre, ni le firmware, ni la chaîne de démarrage.

À la fin de la timeline, Lab conserve toute la conclusion visuelle de Safe/Normal : nettoyage, explosion, écran noir puis oie d’adieu. Une dernière image entièrement noire est ensuite affichée et, pendant qu’elle reste à l’écran, `RunLabConclusion` dans `src/lab_mode.cpp` est appelé une seule fois avec les chemins extraits. Ce point d’extension est vide par défaut et constitue l’endroit prévu pour ajouter du code propre à la conclusion Lab ; l’overlay se ferme à son retour.

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

`--duration-scale 0.1` joue la timeline dix fois plus vite, sans raccourcir l’attente initiale de 10 à 30 secondes réelles. Un `--start-at` strictement supérieur à `00:00` saute cette attente. Dans l’implémentation actuelle non destructive, `GooseRot-Lab.exe --fake-reboot` lance `GooseBootPreview.exe` après restauration et faux redémarrage si le binaire est placé à côté de GooseRot ; CMake met automatiquement les exécutables dans le même dossier (`bin/` en mono-configuration, `bin/Release` pour la release Visual Studio). Ce comportement de restauration appartient à l’état transitoire du code et non au contrat cible destructeur de `lab`. `--boot-game` échoue actuellement de façon volontaire : le bundle UEFI/BIOS disponible est non signé et non validé à l’exécution, donc il ne satisfait pas le contrat de confiance et aucun handoff n’est émis.

La Preview du mini-jeu se construit avec le projet racine ou séparément :

```powershell
cmake -S boot -B build\boot -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build\boot --parallel
ctest --test-dir build\boot --output-on-failure
```

Un build autonome place la Preview dans `build\boot\bin`; copiez-la à côté de `GooseRot-Lab.exe` pour utiliser `--fake-reboot`.

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
