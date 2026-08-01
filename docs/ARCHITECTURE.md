# GooseRot — architecture technique

> **Contrat cible :** `lab` est un profil destructeur réservé à une VM isolée et jetable ; il peut corrompre les fichiers, le Registre et le démarrage. La sortie par `Esc` maintenu deux secondes appartient exclusivement à `safe`. Le code actuel n’est pas encore aligné : ses chemins restent réversibles et son traitement de `Esc` est commun aux profils.

## Choix de plateforme

Le livrable principal sera une application **C++ Win32 native**, compilée en `x86` et liée avec le runtime MSVC statique (`/MT`). Ce choix permet un seul exécutable sans installation de .NET ou du Visual C++ Redistributable. Le binaire 32 bits fonctionne nativement sur Windows 7 32 bits et via WoW64 sur les éditions 64 bits de Windows 7 à Windows 11.

Paramètres de compilation envisagés :

```text
Architecture : x86
Sous-système : WINDOWS
Runtime : /MT
Unicode : activé
WINVER : 0x0601
_WIN32_WINNT : 0x0601
Optimisation release : /O2
```

Les API apparues après Windows 7 sont résolues dynamiquement avec `GetProcAddress` et possèdent un fallback.

## Réimplémentation de l’oie

Desktop Goose v0.31 est une application .NET managée. Sa copie locale et l’API publique de modding servent de référence pour comprendre :

- la géométrie du rig ;
- la locomotion et l’accélération ;
- le cou extensible ;
- l’alternance procédurale des pattes ;
- le rendu GDI+ de l’oie et de son ombre ;
- les états marche, course et charge.

Le nouveau moteur est écrit indépendamment. Le dépôt GooseRot ne doit pas recevoir de code directement copié depuis une décompilation. Les constantes déjà publiées dans `GooseModdingAPI` peuvent être relevées dans une note de compatibilité, puis vérifiées par observation visuelle.

## Composants

```text
GooseRotApp
 ├─ SafetySupervisor
 ├─ TimelineEngine
 ├─ GooseEngine
 │   ├─ GooseEntity × 3
 │   ├─ ProceduralRig
 │   └─ LocomotionController
 ├─ OverlayRenderer
 │   ├─ LayeredWindow
 │   ├─ SpeechBubbles
 │   ├─ MemeOverlays
 │   ├─ HandDrawnChrome
 │   ├─ LiveSprayTag
 │   ├─ FakeToasts
 │   ├─ GlitchLayer
 │   └─ ColorAndShakeEffects
 ├─ DesktopDirector
 │   ├─ CursorDirector
 │   └─ WindowDirector
 ├─ InteractionDirector
 │   ├─ ClipboardVisualGag
 │   ├─ NotepadGag
 │   ├─ PropDelivery
 │   ├─ TaskbarGuard
 │   └─ PopupSwarm
 ├─ AudioEffects
 ├─ BootGameHandoff
 └─ ShutdownDirector
```

### `SafetySupervisor`

Dans l’implémentation actuelle, il possède la priorité sur tous les autres modules et gère l’arrêt d’urgence, l’unicité du processus, l’état de restauration en mémoire partagée, la restauration et les timeouts. Dans le contrat cible, l’appui de deux secondes sur `Esc` et la restauration d’urgence sont activés uniquement en `safe` ; `normal` et `lab` ne doivent pas emprunter ce chemin de sortie.

### `TimelineEngine`

Charge une timeline déclarative embarquée. Chaque événement possède :

- une date de début ;
- une durée ;
- un profil minimal ;
- une action principale ;
- une variante autonome ;
- une procédure de nettoyage.

Une horloge monotone pilote la timeline. Les animations ne doivent jamais dépendre du nombre d’images rendues.

### `GooseEngine`

Conserve un état indépendant par oie. Jusqu’à 67 entités partagent le même renderer mais disposent de positions, vélocités, directions et tâches séparées.

### `OverlayRenderer`

Utilise une fenêtre Win32 transparente, sans bordure et non activable. Le rendu principal repose sur GDI/GDI+ et `UpdateLayeredWindow`, disponible bien avant Windows 7. La version actuelle emploie une surface virtuelle unique, recréée transactionnellement lors d'un changement d'affichage et plafonnée à 30 millions de pixels (environ 120 Mo). `--primary-monitor-only` réduit cette surface à l'écran principal sur les configurations très larges. Start et Search pouvant appartenir à une bande supérieure à `HWND_TOPMOST`, un thread de message léger possède un hook d'accessibilité `EVENT_SYSTEM_FOREGROUND` et traite immédiatement toute nouvelle fenêtre foreground, indépendamment de la cadence de rendu. Le callback revalide visibilité, intersection, classe et chemin du processus avant toute action ; seule cette surface shell foreground strictement identifiée peut ignorer la comparaison de z-order entre bandes. Un parcours de secours inspecte toutes les 200 ms la chaîne z-order documentée et bornée jusqu'à l'overlay. Seules les classes `Windows.UI.Core.CoreWindow` et `DV2ControlHost` sont admises, après vérification d'un exécutable Start/Search sous le vrai dossier `Windows\SystemApps`; l'hôte Start historique doit en plus être l'instance `explorer.exe` propriétaire de `GetShellWindow()`. Une surface validée reçoit `Esc`, `WM_CANCELMODE` puis `SW_HIDE`. Une application ordinaire, une surface située derrière la scène ou ouverte sur un autre écran n'est jamais ciblée. Après un congédiement, l'overlay reprend immédiatement la tête du groupe topmost ; le battement de maintien reste limité à 500 ms.

### `DesktopDirector`

Le déplacement de curseur et de fenêtres est animé, borné à l’espace visible et entièrement traçable. Avant le premier déplacement, `WindowDirector` sauvegarde le rectangle original de chaque fenêtre ciblée.

Ne jamais cibler :

- le shell, la barre des tâches ou le Gestionnaire des tâches ;
- les écrans de sécurité Windows ;
- les fenêtres invisibles, minimisées ou appartenant à GooseRot ;
- une fenêtre marquée comme exclue par le profil de test.

### `NotepadGag`

Crée une fenêtre GooseRot imitant le Bloc-notes et remplit directement son contrôle en lecture seule avec la banque de mots du projet. Aucune saisie synthétique n’est envoyée et aucun changement de focus ne peut faire écrire dans une application utilisateur. La cadence continue jusqu’à `5:58` et le nettoyage détruit la fenêtre.

`WM_CLOSE` est intercepté pour le gag : les premières tentatives renomment la fenêtre et la décalent de 67 pixels. Si elle finit par être détruite, la timeline la recrée tant que la phase de frappe reste active. Le nettoyage et l’arrêt d’urgence appellent `DestroyWindow` directement.

La réduction est refusée par trois chemins complémentaires, pour que le flux de texte ne puisse pas être rangé dans la barre des tâches : le style ne comporte ni `WS_MINIMIZEBOX` ni `WS_MAXIMIZEBOX`, `SC_MINIMIZE` est avalé dans `WM_SYSCOMMAND` et grisé dans le menu système, et toute réduction obtenue depuis l’extérieur est annulée par `WM_SIZE`/`SIZE_MINIMIZED` puis, en dernier recours, par un contrôle `IsIconic` à chaque tick. Chaque refus est signalé une fois au moteur, qui répond par une bulle.

### `OwnedWindowsApps`

Lance au maximum six vrais utilitaires intégrés à Windows : Notepad, Paint, Task Manager, Character Map, Command Prompt ou Explorer avec `/separate`. Chaque cible est un chemin Windows construit par le programme, sans URL ni argument utilisateur. Chaque entrée conserve uniquement le handle de processus et le PID renvoyés par `CreateProcessW`. L’énumération ne positionne et ne ferme que les fenêtres appartenant à ces PID ; elle ne revendique jamais une instance préexistante et ne force pas la terminaison d’un processus qui refuserait `WM_CLOSE`.

### `TaskbarGuard`

Une petite fenêtre GooseRot `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED` est posée exactement sur le bouton Démarrer et absorbe les clics qui l’atteignent, parce qu’un menu Démarrer ouvert recouvre toute la scène. Le rectangle vient du bouton réel quand il est identifiable — sous Windows 10, l’enfant de classe `Start` de `Shell_TrayWnd` — et d’une heuristique sur la géométrie de la barre sinon, la barre XAML de Windows 11 n’exposant pas ce bouton ; la position est réévaluée à chaque tick pour suivre une barre déplacée ou masquée automatiquement.

`WM_MOUSEACTIVATE` renvoie `MA_NOACTIVATE` : le clic est reçu et compté sans jamais voler le focus. `MA_NOACTIVATEANDEAT` conviendrait au blocage mais supprimerait aussi le message de bouton, donc le garde ne pourrait plus signaler la tentative. Aucun hook n’est installé, aucune touche n’est synthétisée, aucune fenêtre du shell n’est sous-classée, déplacée ou détruite : détruire le garde rend le bouton à Windows. `Ctrl+Shift+Échap`, `Alt+Tab` et la sortie `Échap` ne sont jamais concernés.

### `PopupSwarm`

Gère un ensemble borné de popups GooseRot qui imitent plusieurs outils Windows. Fermer une popup en programme deux autres tant que le plafond courant n’est pas atteint. Au plafond, toutes les requêtes `WM_CLOSE` ordinaires sont refusées ; `CloseAll()` reste le chemin privilégié du nettoyage de fin et de l’arrêt d’urgence.

Le plafond est réglable en cours de partie via `SetCeiling()`. Il vaut le maximum protecteur de 67 jusqu’au monologue final, pour que le gag de duplication garde de la marge, puis suit le budget décroissant de `DesiredPopupCount()`. `Dissolve()` détruit directement N fenêtres, sans passer par le chemin de refus : c’est le glitch qui mange l’essaim dans le dernier tiers, pas l’utilisateur qui obtient enfin le droit de fermer. Les créations et destructions sont différées hors du gestionnaire de messages. Les popups sont `WS_EX_NOACTIVATE`, restent dans la zone de travail et appartiennent toutes au processus GooseRot.

### `GlitchLayer` et rendu dense

Dessine, uniquement dans la surface de l’overlay, les déchirures, blocs corrompus, scanlines, curseurs fantômes, faux cadres « Ne répond pas », rubans de lignes déplacées et flashs. Le planificateur des rubans et flashs dépend du seed et de l'horloge réelle : un accélérateur de timeline ne peut donc jamais compresser leur cadence. Les flashs sont plafonnés à un pulse par tranche de 720 ms, durent au plus 110 ms et gardent un alpha borné. Les rubans utilisent `memmove` uniquement dans le DIB ARGB local ; aucune capture du bureau n'a lieu. Au-delà de douze oies, de 24 popups, de huit images ou de trois millions de pixels, le rendu passe en mode dense : trois oies restent complètes, les suivantes utilisent une silhouette compacte, les images stables passent par le composite ARGB mis en cache et les scanlines sont espacées.

### `AudioEffects`

Sélectionne aléatoirement un alias sonore Windows et le joue de manière asynchrone avec `PlaySound`. La cadence utilise l'horloge réelle, accélère progressivement sans boucle audio ni modification du volume et s'arrête pendant `Cleanup()`. `--mute` et la Preview désactivent entièrement ce module.

### `LiveSprayTag`

Le `67` est décrit comme trois tracés de points, pas comme un glyphe de police. Le rendu révèle les tracés par longueur d’arc, ce qui donne la peinture en direct, puis ajoute l’overspray et les coulures derrière la buse. `GraffitiPaintHead()` expose la position de la buse pour que l’oie puisse suivre son propre tag.

Trois règles garantissent que le tag est réellement visible, quel que soit l’écran :

- `TagScale()`, `TagCenter()` et `TagZone()` vivent dans le cœur partagé, donc le renderer et le placement des images calculent la même boîte ;
- aucune image brainrot ne peut se poser dans `TagZone()`, à aucun moment de la timeline, et celles déjà posées y sont reprises par une oie quand la phase graffiti commence ;
- le tag est dessiné **après** les images, et une passe d’encre presque opaque est posée sous la couleur, pour qu’un fond clair, le filtre de couleur ou une pile de photos ne puisse pas l’effacer.

C’est la correction du cas observé sur Windows 10 : sur un écran de petite définition, le tag était calculé au centre, tandis que les images placées avant la phase graffiti pouvaient occuper ce même centre et étaient dessinées par-dessus.

### `PropDelivery`

Une image brainrot n’apparaît jamais seule. Chaque `VisualSprite` traverse quatre états — `Fetching`, `Carried`, `Placed`, `Tearing` — pilotés par `UpdateSprites()`. Une oie libre est choisie par `PickFreeCarrier()`, marquée hors champ, envoyée en charge vers le bord le plus proche, puis l’image devient visible dans son bec dès qu’elle a quitté le canvas et revient au pas. Sans oie libre, la livraison est simplement reportée. Chaque étape possède une échéance calculée sur la distance réelle, bornée entre 5 et 18 secondes, pour qu’un écran très large ne coupe jamais un trajet et qu’aucun état ne puisse rester bloqué.

`PropCloseBox()` définit la croix `[x]` d’une image posée ; le moteur teste le clic sur cette même boîte, sans hook, en lisant la position du pointeur. La fermeture est réelle et payante : aura, pic de glitch, deux livraisons commandées, et une escalade sur l’essaim puis sur la troupe.

### `ShutdownDirector`

Dans l’état actuel du code, la conclusion commence par le nettoyage et la restauration, puis rend une explosion plein écran, une coupure noire et l’entrée d’une oie d’adieu avant la fermeture. Ce même chemin réversible est encore utilisé en `lab`. Le contrat cible de `lab` exclut cette restauration : la VM doit être considérée comme sacrifiée après la corruption des fichiers, du Registre et du démarrage.

### `BootGameHandoff`

Le contrat futur de `lab --boot-game` vérifiera la signature, le statut de validation runtime et les hashes des binaires GooseBoot avant d’écrire une requête non privilégiée et de quitter avec le code `67`. Les adaptateurs UEFI x64 et BIOS produisent désormais un bundle expérimental, mais son manifeste porte `experimental-unsigned`, `installable: false` et `runtimeValidated: false`. La version actuelle refuse donc toujours l’option et n’écrit aucun handoff. Elle ne détecte pas le firmware, ne touche pas aux disques et ne modifie aucune configuration de démarrage.

### Adaptateurs `GooseBoot`

Les deux adaptateurs appellent directement le même cœur AURA 67 freestanding à 30 Hz. L’UEFI x64 reste dans Boot Services, sélectionne/restaure si nécessaire un mode GOP compatible, désarme le watchdog hérité et s’appuie sur Simple Text Input, un timer avec rattrapage `GetTime` borné et, uniquement après confirmation finale, `ResetSystem`. Le BIOS charge un stage 2 fixe en lecture seule avec EDD, conserve les pointeurs VBE au format segment:offset, vérifie A20, sélectionne un framebuffer VBE 2.0 32 bits, entre en mode protégé, puis utilise le clavier PS/2 et le PIT. Leur cible CMake vérifie statiquement les formats, imports, tailles, signatures, points d’entrée, relocations et bornes d’adressage réel. Aucun test runtime OVMF ou SeaBIOS n’a encore confirmé le démarrage, les périphériques ou le reset ; voir `docs/BOOT_GAME.md` pour les limites détaillées.

## Performance VM

- boucle Win32 cadencée par QPC à 60 images/seconde, avec résolution d'attente de 1 ms ;
- pompe de messages bornée à 64 messages ou 2 ms avant de rendre, afin que les fenêtres de l'essaim ne puissent pas affamer la frame suivante ;
- plancher visé en scène saturée : 10 images/seconde ;
- aucune boucle active sans attente ;
- surface virtuelle unique plafonnée à 30 millions de pixels, ou écran principal seul sur demande ;
- assets décodés une seule fois puis mis en cache ;
- images brainrot pré-réduites à 256 px, rotations finales pré-rastérisées et compositées directement en ARGB prémultiplié ;
- graffiti terminé mis en cache et filtre couleur plein écran rempli directement dans la surface ;
- nombre maximal d’overlays image simultanés : 36 en `safe/normal`, 48 en `lab` ;
- croix `[x]` des images tracée à main levée en scène légère et en rectangle simple en scène dense ;
- nombre maximal d’oies et de popups GooseRot simultanées : 67 pour chaque type, tous profils confondus ;
- effets de glitch vectoriels ; les seuls accès pixel directs sont le filtre uni, les rubans locaux et le composite ARGB des caches, sans aucune relecture du bureau ;
- mémoire cible : moins de 150 Mo ;
- usage CPU non plafonné : le rendu peut saturer un cœur ou davantage pour protéger la fluidité ;
- absence de réseau après le lancement.

## Compatibilité et tests

Matrice minimale :

- Windows 7 SP1 x86, thème classique et Aero ;
- Windows 7 SP1 x64 ;
- Windows 10 22H2 x64 ;
- Windows 11 x64, version courante ;
- écran 1366×768 à 100 % ;
- écran 1920×1080 à 100/125/150 % ;
- double écran avec coordonnées négatives ;
- VM à 2 vCPU et 2 Go de RAM.

Les tests `lab` s’exécutent uniquement sur une VM isolée et jetable. Tant que les destructions ne sont pas implémentées, les tests doivent signaler explicitement qu’ils ne valident que l’ancien comportement réversible. Une fois le contrat cible aligné, la réussite attendue inclut une VM inutilisable et une récupération exclusivement par restauration du snapshot depuis l’hyperviseur.
