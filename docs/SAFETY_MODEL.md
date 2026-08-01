# GooseRot — modèle de sécurité

## Limite fondamentale

Le modèle de sécurité ne couvre que le profil `safe`. Le profil `lab` est au contraire défini comme destructeur : il peut corrompre des fichiers, le Registre Windows et la chaîne de démarrage jusqu’à rendre le système inutilisable. Il doit être exécuté uniquement dans une VM isolée et jetable dont la perte complète est acceptée.

La documentation décrit ici le contrat cible. Dans l’état actuel du dépôt, le code de `lab` n’implémente pas encore ces destructions et conserve des mécanismes de restauration. Cette divergence ne doit jamais être interprétée comme une garantie de sécurité d’une future build `lab`.

## Effets autorisés en `safe`

- overlays transparents, y compris déchirures, scanlines, blocs corrompus, faux curseurs, faux cadres « Ne répond pas » et fausses notifications, tous peints à l’intérieur de l’overlay ;
- flashs plein écran bornés par l'horloge réelle et sons d'alerte Windows asynchrones, explicitement annoncés et désactivables ;
- déplacement borné et restauré des fenêtres ;
- traction initiale du curseur sur 67 pixels, puis tempête par vagues, bornée à l’écran, restaurée lors du nettoyage, et qui rend intégralement le pointeur pendant une partie de chaque cycle ;
- texte écrit directement dans le faux Bloc-notes interne à GooseRot, fenêtre qui refuse d’être réduite dans la barre des tâches tant que sa phase de frappe est active ;
- fenêtre GooseRot posée sur le bouton Démarrer, qui absorbe les clics l’atteignant ;
- lancement consenti de six utilitaires Windows simultanés au maximum, cadencé par l’horloge réelle et suivi exclusivement par leur PID de création ;
- garde clavier temporaire, après choix explicite de l’expérience complète, qui absorbe uniquement les deux touches Windows ;
- fermeture ciblée de Start/Search lorsqu’ils recouvrent l’overlay, via notification foreground sans frappe synthétique ;
- fenêtres GooseRot qui refusent de se fermer et se dupliquent, dans les limites décrites plus bas ;
- simulation visuelle de `Ctrl+V`, sans hook clavier ni accès au presse-papiers ;
- faux glitch, glyphes d’erreur originaux, faux BSOD et faux firmware ;
- explosion, coupure noire et conclusion rendues dans l’overlay, sans appel système.

## Menu Démarrer protégé

Le clic sur le bouton Démarrer est bloqué par recouvrement :

- il s’agit d’une fenêtre appartenant au processus GooseRot, positionnée sur le rectangle du bouton et détruite par le nettoyage, l’arrêt d’urgence et la fin du processus ;
- aucune fenêtre du shell n’est sous-classée, déplacée, désactivée ou détruite ;
- `WM_MOUSEACTIVATE` renvoie `MA_NOACTIVATE` : le clic est absorbé sans jamais prendre le focus à l’application de l’utilisateur ;
- `Ctrl+Shift+Échap`, `Alt+Tab`, la zone de notification et le reste de la barre des tâches restent utilisables ;
- la sortie d’urgence `Échap` maintenue deux secondes n’est jamais affectée ;
- une fois le garde détruit, Windows retrouve immédiatement son comportement normal : aucun réglage du shell n’a été modifié ;
- le consentement initial annonce explicitement ce recouvrement.

Dans l’expérience complète uniquement, le clavier reçoit en plus un hook bas niveau borné à `VK_LWIN` et `VK_RWIN`. Il n’enregistre rien, ne synthétise rien et ne change aucun réglage persistant. Le mode réduit et la Preview ne l’installent jamais. Le garde reste actif pendant les visuels de conclusion puis disparaît à la sortie de la boucle ; l’arrêt d’urgence et les erreurs le retirent immédiatement.

## Images fermables

Les images brainrot posées sur le bureau portent une croix `[x]` réellement cliquable :

- le clic est détecté en lisant la position du pointeur au moment d’un appui, sans hook clavier ni souris, puisque l’overlay laisse déjà passer les clics ;
- aucune saisie n’est enregistrée : seul l’état du bouton gauche et la position du pointeur sont consultés, jamais le contenu d’une frappe ;
- la fermeture ne touche qu’un dessin interne à l’overlay et n’affecte aucune fenêtre du système ;
- les conséquences — aura, glitch, deux livraisons commandées et une oie supplémentaire à partir de la cinquième destruction — restent entièrement dans les limites décrites ici.

## Aucune imitation de Windows

GooseRot n’ouvre et ne dessine aucune fenêtre, boîte de dialogue, icône d’erreur ou notification se faisant passer pour un composant du système. C’est d’abord un choix de mise en scène — l’écart se voit et casse l’illusion — mais c’est aussi une propriété utile ici : rien de ce qui s’affiche ne peut être confondu avec un avertissement réel de Windows. Toutes les surfaces GooseRot s’annoncent comme appartenant à l’inspection.

## Fenêtre récalcitrante

Une seule fenêtre résiste, et elle est bornée par construction :

- il s’agit du dossier d’inspection créé par GooseRot, jamais d’une fenêtre appartenant à une autre application ;
- il refuse d’être réduit, jamais d’être détruit : le nettoyage et l’arrêt d’urgence appellent `DestroyWindow` directement ;
- il peut revenir tant que sa phase de rédaction est active, puis le nettoyage le détruit directement ;
- chaque vrai utilitaire est créé suspendu et refusé si son assignation au Job Object privé échoue ; les plus anciens reçoivent d’abord `WM_CLOSE` de façon répétée puis, après quatre secondes de grâce et au nettoyage, le job ou le handle exact retourné par `CreateProcessW` empêchent uniquement ces enfants de survivre, sans jamais adopter ni viser un PID préexistant ;
- dans le code actuel, `Esc` maintenu deux secondes, le nettoyage de fin et la fermeture du processus détruisent toutes ces fenêtres sans passer par leur gestionnaire de fermeture ;
- le consentement initial annonce explicitement ce comportement.
- le consentement propose un démarrage réduit et muet ; `--mute`, `--no-flashes` et `--reduced-motion` restent disponibles en ligne de commande.

## Effets interdits en `safe`

- suppression, chiffrement ou corruption de fichiers ;
- écriture dans le MBR, la partition EFI, le firmware ou les variables UEFI ;
- modification de BCD, Winlogon, Run/RunOnce, Startup ou des tâches planifiées ;
- persistance après déconnexion ou redémarrage ;
- désactivation de Defender, SmartScreen, UAC ou du Gestionnaire des tâches ;
- fenêtre non fermable appartenant à une autre application ;
- fenêtre, dialogue, icône ou notification imitant un composant de Windows ;
- élévation, exploitation ou contournement de droits ;
- dissimulation du processus ou communication réseau cachée ;
- fermeture forcée d’applications avec des données non enregistrées.

## Conséquences attendues en `lab`

- suppression ou corruption de fichiers dans la VM ;
- dégradation persistante du Registre Windows ;
- corruption de la configuration ou des données de démarrage ;
- échec possible du prochain démarrage ;
- absence de nettoyage ou de restauration en fin d’exécution ;
- perte totale de la VM considérée comme le résultat normal du test.

Le profil `lab` ne doit jamais être lancé sur une machine physique, une VM contenant des données utiles, ou un environnement relié à des disques, dossiers partagés, comptes ou secrets de production. Le snapshot doit être restauré depuis l’hyperviseur, jamais depuis l’invité compromis.

## Mini-jeu de boot

Le mini-jeu possède un cœur portable, une Preview Win32 et deux adaptateurs freestanding : application UEFI x64 et chaîne BIOS 32 bits. Ils sont limités à l’affichage, au clavier, au temps et à une demande explicite de redémarrage après la partie. L’UEFI ne localise aucun protocole disque. Le BIOS stage 1 lit seulement les 127 secteurs fixes de son propre stage 2 avec EDD ; aucun adaptateur ne contient de chemin d’écriture disque.

Deux variantes appartiennent à GooseRot :

- `lab --fake-reboot` : dans le code actuel non aligné, restauration vérifiée, transition vers la Preview, puis sortie ; cette restauration devra disparaître du contrat cible destructeur ;
- `lab --boot-game` : option actuellement refusée malgré la présence des artefacts expérimentaux, car ils sont non signés et non validés à l’exécution.

La cible `gooseboot_firmware_bundle` assemble uniquement un dossier de staging et un manifeste portant `installable: false` et `runtimeValidated: false`. Les vérifications actuelles portent sur le format et le layout ; aucun démarrage QEMU/OVMF/SeaBIOS n’a été exécuté. La détection du firmware et l’installation dans une chaîne de démarrage existante ne sont pas dans le périmètre. Elles restent dans un projet d’intégration étudiant séparé, sans procédure fournie ici. Le fichier `gooseboot-bios.img` est destiné uniquement à un futur test sur support virtuel vierge et jetable, jamais à un disque physique.

## Restauration

Cette section s’applique au profil `safe`. `lab` ne promet aucune restauration.

Le programme conserve uniquement en mémoire :

- la position originale des fenêtres qu’il a déplacées ;
- la fenêtre qui avait le focus ;
- aucun contenu de presse-papiers, qui n’est jamais lu ni modifié ;
- l'identité PID/TID des fenêtres tierces concernées.

Tout est effacé à la fermeture. Aucun log ne contient de texte saisi, de contenu du presse-papiers ou de titre de document.

## Crash et watchdog

Cette section s’applique au profil `safe`. En `lab`, ni le watchdog ni la fermeture du processus ne constituent une procédure de récupération.

Un processus de secours du même exécutable surveille le moteur via une mémoire partagée locale, sans journal sur disque :

- l’arrêt normal ou d’urgence ferme les fenêtres GooseRot et retire l’overlay ;
- les rectangles, PID/TID, focus et position du curseur restent uniquement en mémoire ;
- le processus de secours attend la terminaison du parent et ne restaure qu’en l’absence du marqueur de sortie propre ;
- la position initiale du curseur et le focus sont restaurés ;
- aucun BSOD réel ni processus de représailles n’existe ; après terminaison, le seul effet permis au processus de secours est la restauration.

Le test d'intégration `gooserot_win32_integration` n'agit que sur ses propres processus et fenêtres factices. Il vérifie un déplacement exact, une cible lente, le fallback borné, la restauration par le watchdog après un `ExitProcess` volontaire du parent de test, le refus de réduction du dossier d’inspection par les trois chemins prévus, son ouverture au point tamponné par l’oie et le budget de rendu de l’obturateur final.

## Consentement et environnement

- `safe` est le profil par défaut ;
- maintenir `Esc` pendant deux secondes restaure et ferme l’application uniquement en `safe` ;
- `normal` et `lab` ignorent ce geste comme commande de sortie ;
- `lab` affiche un avertissement bloquant indiquant que la VM sera saccagée et pourra ne plus redémarrer ;
- le HUD affiche en permanence le profil actif, ainsi que l’état courant du pointeur pendant la tempête (`CURSOR: SEIZED` ou `CURSOR: YOURS`) ;
- les tests `lab` se font sur une VM isolée, jetable, avec snapshot hors ligne et sans données importantes.
