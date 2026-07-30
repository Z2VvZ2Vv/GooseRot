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
 │   └─ ColorAndShakeEffects
 ├─ DesktopDirector
 │   ├─ CursorDirector
 │   └─ WindowDirector
 ├─ InteractionDirector
 │   ├─ ClipboardVisualGag
 │   └─ NotepadGag
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

Conserve un état indépendant par oie. Les trois entités partagent le même renderer mais disposent de positions, vélocités, directions et tâches séparées.

### `OverlayRenderer`

Utilise une fenêtre Win32 transparente, sans bordure et non activable. Le rendu principal repose sur GDI/GDI+ et `UpdateLayeredWindow`, disponible bien avant Windows 7. La version actuelle emploie une surface virtuelle unique, recréée transactionnellement lors d'un changement d'affichage et plafonnée à 30 millions de pixels (environ 120 Mo). `--primary-monitor-only` réduit cette surface à l'écran principal sur les configurations très larges.

### `DesktopDirector`

Le déplacement de curseur et de fenêtres est animé, borné à l’espace visible et entièrement traçable. Avant le premier déplacement, `WindowDirector` sauvegarde le rectangle original de chaque fenêtre ciblée.

Ne jamais cibler :

- le shell, la barre des tâches ou le Gestionnaire des tâches ;
- les écrans de sécurité Windows ;
- les fenêtres invisibles, minimisées ou appartenant à GooseRot ;
- une fenêtre marquée comme exclue par le profil de test.

### `NotepadGag`

Crée une fenêtre GooseRot imitant le Bloc-notes et remplit directement son contrôle en lecture seule avec la banque de mots du projet. Aucune saisie synthétique n’est envoyée et aucun changement de focus ne peut faire écrire dans une application utilisateur. À `1:00`, la fenêtre est réduite puis fermée pendant le nettoyage.

### `ShutdownDirector`

En `safe` et `normal`, la conclusion est un faux redémarrage rendu dans l’overlay, suivi d’une fermeture propre. Dans l’état actuel du code, ce même chemin est encore utilisé en `lab`. Le contrat cible de `lab` exclut cette restauration : la VM doit être considérée comme sacrifiée après la corruption des fichiers, du Registre et du démarrage.

### `BootGameHandoff`

Le contrat futur de `lab --boot-game` vérifiera la signature, le statut de validation runtime et les hashes des binaires GooseBoot avant d’écrire une requête non privilégiée et de quitter avec le code `67`. Les adaptateurs UEFI x64 et BIOS produisent désormais un bundle expérimental, mais son manifeste porte `experimental-unsigned`, `installable: false` et `runtimeValidated: false`. La version actuelle refuse donc toujours l’option et n’écrit aucun handoff. Elle ne détecte pas le firmware, ne touche pas aux disques et ne modifie aucune configuration de démarrage.

### Adaptateurs `GooseBoot`

Les deux adaptateurs appellent directement le même cœur AURA 67 freestanding à 30 Hz. L’UEFI x64 reste dans Boot Services, sélectionne/restaure si nécessaire un mode GOP compatible, désarme le watchdog hérité et s’appuie sur Simple Text Input, un timer avec rattrapage `GetTime` borné et, uniquement après confirmation finale, `ResetSystem`. Le BIOS charge un stage 2 fixe en lecture seule avec EDD, conserve les pointeurs VBE au format segment:offset, vérifie A20, sélectionne un framebuffer VBE 2.0 32 bits, entre en mode protégé, puis utilise le clavier PS/2 et le PIT. Leur cible CMake vérifie statiquement les formats, imports, tailles, signatures, points d’entrée, relocations et bornes d’adressage réel. Aucun test runtime OVMF ou SeaBIOS n’a encore confirmé le démarrage, les périphériques ou le reset ; voir `docs/BOOT_GAME.md` pour les limites détaillées.

## Performance VM

- cible normale : 30 images/seconde ;
- 15 images/seconde lorsque seules des animations lentes sont visibles ;
- aucune boucle active sans attente ;
- surface virtuelle unique plafonnée à 30 millions de pixels, ou écran principal seul sur demande ;
- assets décodés une seule fois puis mis en cache ;
- nombre maximal d’overlays image simultanés : 12 en `safe/normal`, 24 en `lab` ;
- mémoire cible : moins de 150 Mo ;
- usage CPU cible : moins de 10 % de deux vCPU hors pics de transition ;
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
