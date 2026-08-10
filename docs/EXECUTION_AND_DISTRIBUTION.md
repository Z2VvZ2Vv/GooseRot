# GooseRot — exécution et distribution

## Livrable

Une release contient :

```text
GooseRot-Safe.exe
GooseRot-Normal.exe
GooseRot-Lab.exe
SHA256SUMS.txt
README.txt
```

Le bundle `GooseRot-Lab.exe --fake-reboot` ajoute `GooseBootPreview.exe` à côté des trois exécutables. La cible CMake `gooserot_release`, disponible uniquement en MSVC Win32, produit ce dossier de staging et `SHA256SUMS.txt` dans `build*/dist/`. MinGW expose à la place `gooserot_smoke_bundle` dans `smoke-dist/`, explicitement non publiable.

Les images, la timeline et les textes sont embarqués dans les ressources des exécutables. Quand les entrées firmware sont fournies au build, le `.efi` et les deux stages BIOS sont embarqués uniquement dans `GooseRot-Lab.exe` ; aucun fichier firmware ne doit rester à côté de l’exécutable.

Le sous-projet pré-OS produit séparément un bundle de laboratoire expérimental. Il ne fait partie ni de `gooserot_release` ni de `gooserot_smoke_bundle`, et il ne constitue pas une release publiable :

```text
GooseBootX64.efi
gooseboot-bios-stage1.bin
gooseboot-bios-stage2.bin
gooseboot-bios.img
gooseboot-manifest.json
SHA256SUMS.txt
README.txt
```

Construction avec MinGW GNU x64 et les GNU binutils :

```powershell
cmake -S . -B build-firmware -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGOOSEROT_BUILD_BOOT_FIRMWARE=ON
cmake --build build-firmware --parallel
ctest --test-dir build-firmware --output-on-failure
cmake --build build-firmware --target gooseboot_firmware_bundle
```

Les images intermédiaires vérifiées sont placées dans `build-firmware/boot/firmware/` et le bundle dans `build-firmware/boot/firmware-dist/`. Le manifeste indique explicitement `trust: experimental-unsigned`, `installable: false` et `runtimeValidated: false`. Les contrôles CMake valident notamment les architectures/entrées, le layout UEFI/BIOS, la relocation UEFI `DIR64`, l’absence d’import DLL et les bornes des symboles BIOS en mode réel ; ils ne prouvent pas le démarrage. Aucun test QEMU/OVMF/SeaBIOS n’a été exécuté faute d’outil disponible. Il n’existe ni ISO, ni installateur, ni commande de déploiement sur disque physique.

## Lancement local avec `Win+R`

> **DANGER — `lab` détruit son environnement par contrat.** À la fin, les fichiers, le Registre et la chaîne de démarrage peuvent être corrompus et Windows peut ne plus démarrer. Utiliser exclusivement une VM isolée et jetable, sans données ni secrets, avec un snapshot restaurable depuis l’hyperviseur. L’appui de deux secondes sur `Esc` permet de quitter uniquement `safe` ; il ne constitue pas une sortie de `normal` ou `lab`.

Lorsque l’exécutable est déjà présent sur la machine :

```text
C:\Chemin\GooseRot-Safe.exe
C:\Chemin\GooseRot-Normal.exe
C:\Chemin\GooseRot-Lab.exe
C:\Chemin\GooseRot-Lab.exe --boot-game  # réservé, échoue : bundle non signé/non validé
```

Chaque fichier verrouille son profil : `--mode` ne permet pas de changer de variante. `GooseRot-Lab.exe` démarre immédiatement sans avertissement ou confirmation. Les lignes `lab` ci-dessus documentent l’interface de commande, pas une recommandation d’exécution. Le code actuel n’est pas encore aligné avec le contrat destructeur et conserve des protections supplémentaires ; cette divergence est temporaire et ne doit pas servir de garantie de sécurité.

Avant d’initialiser l’application, Lab détecte le firmware et extrait depuis ses ressources `GooseBootX64.efi` pour UEFI, ou `gooseboot-bios-stage1.bin` et `gooseboot-bios-stage2.bin` pour BIOS, dans `%TEMP%\GooseRot-Lab\`. Avec MinGW x64, `GOOSEROT_BUILD_BOOT_FIRMWARE=ON` construit puis embarque ces fichiers. Pour une autre toolchain, `GOOSEROT_LAB_FIRMWARE_DIR` peut désigner le dossier `firmware` produit auparavant. Sans l’une de ces deux sources, Lab se construit mais refuse son démarrage avec une erreur explicite car aucune ressource firmware n’est embarquée.

Les chemins contenant des espaces doivent être entourés de guillemets.

## Distribution GitHub

Le canal officiel est **GitHub Releases**, avec :

- un binaire versionné et immuable ;
- un hash SHA-256 publié séparément ;
- idéalement une signature Authenticode ;
- un changelog ;
- une page expliquant clairement les trois profils.

Une URL GitHub ouverte via `Win+R` peut conduire l’opérateur vers la dernière release. Le téléchargement puis l’ouverture restent des actions visibles et consenties.

Un lanceur « télécharger puis exécuter » en une ligne, un `Invoke-Expression`, un contournement de politique PowerShell ou une désactivation de SmartScreen ne font pas partie du projet. Ces méthodes ressemblent à une chaîne d’exécution malveillante, sont fragiles sur Windows 7 à cause des différences PowerShell/TLS et nuisent à la confiance dans le binaire.

## Script hébergé

Si un script GitHub est ajouté plus tard, son périmètre est le téléchargement vérifié et le lancement explicite de l’un des trois exécutables GooseRot. Il ne manipule jamais les artefacts GooseBoot, qui appartiennent à un canal de laboratoire séparé.

## Compatibilité Windows 7 → Windows 11

Le binaire évite les dépendances modernes non présentes sur Windows 7. Il utilise la conscience DPI système historique via `SetProcessDPIAware` et conserve un rendu GDI+ classique ; il n'annonce pas de prise en charge DPI per-monitor.

Dans l’implémentation actuelle, `GooseRot-Lab.exe` déclare `requireAdministrator` et Windows affiche donc une demande UAC unique avant son lancement. Un refus annule le lancement sans boucle de relance. Le processus déjà élevé installe et démarre le service local, attend qu’il soit prêt, puis lance directement l’expérience. Le service surveille ce premier worker et ne prend le relais dans la session interactive que si celui-ci est tué, sans seconde demande UAC. `GooseRot-Safe.exe` et `GooseRot-Normal.exe` déclarent `asInvoker`. En `safe` et `normal`, la conclusion reste une explosion et une coupure noire entièrement rendues dans l’overlay, compatibles avec toutes les versions ciblées.

Lab reprend cette conclusion visuelle complète. Après l’explosion et l’oie d’adieu, l’overlay affiche une dernière image noire opaque et appelle `RunLabConclusion` une fois avant de se fermer. Son implémentation dans `src/lab_mode.cpp` ne fait rien par défaut et reçoit `LabStartupArtifacts` afin de servir de point d’extension explicite au code Lab.

## Publication

Avant chaque release :

1. construire en Release `/MT` ;
2. vérifier les imports du PE sur Windows 7 ;
3. analyser le binaire avec au moins deux moteurs antivirus ;
4. exécuter `safe` et `normal` dans des snapshots propres ;
5. exécuter `lab` uniquement dans une VM isolée et jetable, puis restaurer son snapshot depuis l’hyperviseur ;
6. vérifier que l’arrêt d’urgence par appui long sur `Esc` fonctionne en `safe` et n’est pas exposé en `normal` ou `lab` ;
7. vérifier la restauration des fenêtres en `safe` et confirmer que le presse-papiers est resté intact ;
8. produire le SHA-256 ;
9. signer puis publier le binaire et le hash.

Le bundle firmware ne peut rejoindre un canal de publication qu’après des tests runtime reproductibles sous OVMF et SeaBIOS sur supports virtuels vierges, une revue du comportement clavier/vidéo/timer/reset, puis une signature et une mise à jour explicite de `runtimeValidated`. Ces étapes ne sont pas réalisées dans l’état actuel.
