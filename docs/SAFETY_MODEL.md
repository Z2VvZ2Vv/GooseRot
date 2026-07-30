# GooseRot — modèle de sécurité

## Limite fondamentale

Le modèle de sécurité ne couvre que le profil `safe`. Le profil `lab` est au contraire défini comme destructeur : il peut corrompre des fichiers, le Registre Windows et la chaîne de démarrage jusqu’à rendre le système inutilisable. Il doit être exécuté uniquement dans une VM isolée et jetable dont la perte complète est acceptée.

La documentation décrit ici le contrat cible. Dans l’état actuel du dépôt, le code de `lab` n’implémente pas encore ces destructions et conserve des mécanismes de restauration. Cette divergence ne doit jamais être interprétée comme une garantie de sécurité d’une future build `lab`.

## Effets autorisés en `safe`

- overlays transparents ;
- déplacement borné et restauré des fenêtres ;
- déplacement ponctuel du curseur ;
- texte écrit directement dans le faux Bloc-notes interne à GooseRot ;
- simulation visuelle de `Ctrl+V`, sans hook clavier ni accès au presse-papiers ;
- faux glitch, faux BSOD et faux firmware ;
- faux redémarrage rendu dans l’overlay, sans appel système.

## Effets interdits en `safe`

- suppression, chiffrement ou corruption de fichiers ;
- écriture dans le MBR, la partition EFI, le firmware ou les variables UEFI ;
- modification de BCD, Winlogon, Run/RunOnce, Startup ou des tâches planifiées ;
- persistance après déconnexion ou redémarrage ;
- désactivation de Defender, SmartScreen, UAC ou du Gestionnaire des tâches ;
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

Le test d'intégration `gooserot_win32_integration` n'agit que sur ses propres processus et fenêtres factices. Il vérifie un déplacement exact, une cible lente, le fallback borné et la restauration par le watchdog après un `ExitProcess` volontaire du parent de test.

## Consentement et environnement

- `safe` est le profil par défaut ;
- maintenir `Esc` pendant deux secondes restaure et ferme l’application uniquement en `safe` ;
- `normal` et `lab` ignorent ce geste comme commande de sortie ;
- `lab` affiche un avertissement bloquant indiquant que la VM sera saccagée et pourra ne plus redémarrer ;
- le HUD affiche en permanence le profil actif ;
- les tests `lab` se font sur une VM isolée, jetable, avec snapshot hors ligne et sans données importantes.
