# GooseRot — modèle de sécurité

## Limite fondamentale

GooseRot est un programme de démonstration comique. Même en profil `lab`, l’application Win32 ne redémarre pas Windows et ne modifie aucun état de démarrage. Les effets bureau restent néanmoins gênants : le profil `lab` doit être réservé à une VM consentie avec snapshot. Les artefacts firmware expérimentaux sont un sous-projet séparé et ne sont jamais lancés par GooseRot.

## Effets autorisés

- overlays transparents ;
- déplacement borné et restauré des fenêtres ;
- déplacement ponctuel du curseur ;
- texte écrit directement dans le faux Bloc-notes interne à GooseRot ;
- simulation visuelle de `Ctrl+V`, sans hook clavier ni accès au presse-papiers ;
- faux glitch, faux BSOD et faux firmware ;
- faux redémarrage rendu dans l’overlay, sans appel système.

## Effets interdits dans tous les modes, y compris `lab`

- suppression, chiffrement ou corruption de fichiers ;
- écriture dans le MBR, la partition EFI, le firmware ou les variables UEFI ;
- modification de BCD, Winlogon, Run/RunOnce, Startup ou des tâches planifiées ;
- persistance après déconnexion ou redémarrage ;
- désactivation de Defender, SmartScreen, UAC ou du Gestionnaire des tâches ;
- élévation, exploitation ou contournement de droits ;
- dissimulation du processus ou communication réseau cachée ;
- fermeture forcée d’applications avec des données non enregistrées.

## Mini-jeu de boot

Le mini-jeu possède un cœur portable, une Preview Win32 et deux adaptateurs freestanding : application UEFI x64 et chaîne BIOS 32 bits. Ils sont limités à l’affichage, au clavier, au temps et à une demande explicite de redémarrage après la partie. L’UEFI ne localise aucun protocole disque. Le BIOS stage 1 lit seulement les 127 secteurs fixes de son propre stage 2 avec EDD ; aucun adaptateur ne contient de chemin d’écriture disque.

Deux variantes appartiennent à GooseRot :

- `lab --fake-reboot` : restauration vérifiée, transition vers le Preview, puis sortie ;
- `lab --boot-game` : contrat futur de validation et handoff ; l’option échoue fermé malgré la présence des artefacts expérimentaux, car ils sont non signés et non validés à l’exécution.

La cible `gooseboot_firmware_bundle` assemble uniquement un dossier de staging et un manifeste portant `installable: false` et `runtimeValidated: false`. Les vérifications actuelles portent sur le format et le layout ; aucun démarrage QEMU/OVMF/SeaBIOS n’a été exécuté. La détection du firmware et l’installation dans une chaîne de démarrage existante ne sont pas dans le périmètre. Elles restent dans un projet d’intégration étudiant séparé, sans procédure fournie ici. Le fichier `gooseboot-bios.img` est destiné uniquement à un futur test sur support virtuel vierge et jetable, jamais à un disque physique.

## Restauration

Le programme conserve uniquement en mémoire :

- la position originale des fenêtres qu’il a déplacées ;
- la fenêtre qui avait le focus ;
- aucun contenu de presse-papiers, qui n’est jamais lu ni modifié ;
- l'identité PID/TID des fenêtres tierces concernées.

Tout est effacé à la fermeture. Aucun log ne contient de texte saisi, de contenu du presse-papiers ou de titre de document.

## Crash et watchdog

Un processus de secours du même exécutable surveille le moteur via une mémoire partagée locale, sans journal sur disque :

- l’arrêt normal ou d’urgence ferme les fenêtres GooseRot et retire l’overlay ;
- les rectangles, PID/TID, focus et position du curseur restent uniquement en mémoire ;
- le processus de secours attend la terminaison du parent et ne restaure qu’en l’absence du marqueur de sortie propre ;
- la position initiale du curseur et le focus sont restaurés ;
- aucun BSOD réel ni processus de représailles n’existe ; après terminaison, le seul effet permis au processus de secours est la restauration.

Le test d'intégration `gooserot_win32_integration` n'agit que sur ses propres processus et fenêtres factices. Il vérifie un déplacement exact, une cible lente, le fallback borné et la restauration par le watchdog après un `ExitProcess` volontaire du parent de test.

## Consentement et environnement

- `safe` est le profil par défaut ;
- tous les profils indiquent explicitement qu'aucun redémarrage réel n'aura lieu ;
- `lab` emploie le même consentement modal au premier plan avec une icône d'avertissement ;
- le HUD affiche en permanence le profil actif ;
- les tests `lab` se font sur une VM avec snapshot et sans données importantes.
