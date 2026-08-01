# Desktop Goose v0.31 — notes de compatibilité clean-room

## Méthode

L’analyse a été statique : le binaire de référence n’a pas été exécuté. Les métadonnées PE/.NET, les chaînes, la configuration distribuée et surtout les sources publiques de `GooseModdingAPI` ont servi à établir un contrat de comportement. L’implémentation GooseRot a ensuite été écrite indépendamment ; aucun code décompilé n’est présent dans `src/` ou `boot/`.

## Observations utiles

Desktop Goose v0.31 est un exécutable PE32 managé ciblant .NET Framework 4.5.2, WinForms et System.Drawing. La fenêtre principale est sans bordure, toujours au premier plan et traversable aux clics grâce aux styles layered/transparents. L’original limite sa surface à l’écran primaire, invalide toute la fenêtre dans une boucle `Application.Idle` et utilise un pas fixe de 1/120 seconde.

L’oie est rendue entièrement par primitives 2D — lignes, capsules et ellipses — et non par sprite. L’API publique expose les constantes suivantes :

| Paramètre | Valeur publique |
|---|---:|
| Marche / course / charge | 80 / 200 / 400 px/s |
| Accélération normale / charge | 1300 / 2300 px/s² |
| Intervalle des pas normal / charge | 0,2 / 0,1 s |
| Rayon du corps / sous-corps | 22 / 15 px |
| Rayon du cou | 13 px |
| Rayons de tête | 15 / 10 px |
| Rayon des yeux / IPD | 2 / 5 px |
| Écart des pieds / dépassement | 6 px / 0,4 |

Les comportements historiques actifs sont la promenade, les traces de boue, le retour de fenêtres de mème ou de faux Bloc-notes et, si activé, la poursuite du pointeur. L’original utilise notamment des fenêtres WinForms séparées et `Cursor.Clip`. GooseRot ne recopie pas ces algorithmes : il conserve seulement la silhouette, les ordres de grandeur et l’idée d’une oie qui rejoint sa cible avant d’agir.

## Différences intentionnelles

- delta monotone réel, boucle de rendu cadencée par QPC à 60 FPS et primitives adaptatives au lieu d’une simulation fixe à 120 Hz ;
- surface du bureau virtuel multi-écran et coordonnées négatives ;
- alpha par pixel via `UpdateLayeredWindow`, sans couleur clé ;
- trois états d’oie indépendants ;
- aucune DLL de mod chargée ;
- aucun réseau, son ou asset officiel redistribué ; les alertes optionnelles utilisent uniquement les alias fournis par Windows ;
- aucune capture de saisie ou modification du presse-papiers ;
- déplacement direct, ponctuel et restauré du curseur au lieu de le verrouiller avec `Cursor.Clip` ;
- faux Bloc-notes interne afin que le texte ne puisse jamais atteindre une application utilisateur ;
- dans l’implémentation actuelle, sortie d’urgence de deux secondes et restauration centralisée dans tous les profils ; le contrat cible réserve cette sortie à `safe`.

## Référence comportementale MEMZ Clean

Le fichier local `MEMZ-Clean.exe` a lui aussi été observé uniquement par analyse statique et reste hors de Git et des releases. L'échantillon analysé est un PE32 x86 non signé de 12 800 octets, SHA-256 `4BBF7589615EBDB6C769D6D2E7BDCB26072BAC0CDA6E225A4133BA8819E688D5`. Ses imports confirment des familles d'effets — alias sonores Windows, déplacement du curseur, lancement de programmes, saisie synthétique et copies GDI du bureau — sans constituer une source de code pour GooseRot.

Les réinterprétations GooseRot sont indépendantes et volontairement plus étroites : aucun site ni `ShellExecute`, aucune saisie globale enregistrée ou synthétisée, aucune lecture ou écriture des pixels du bureau et aucun texte ou timing copié. Le seul filtrage global, explicitement opt-in, absorbe les deux touches Windows pendant l’expérience complète. Les caractères vont directement dans le faux Notepad GooseRot ; les « fault ribbons » déplacent seulement des lignes de la surface ARGB locale ; les glyphes d’erreur sont des cercles et croix vectoriels originaux ; les flashs et lancements de programmes ont une cadence gouvernée par l'horloge réelle ; les programmes proviennent d'une liste fixe de chemins Windows.

## Frontière de redistribution

Le dossier local `DesktopGoose v0.31/` reste exclu de Git et des releases. Les sons historiques ont une provenance tierce et le logiciel officiel interdit sa redistribution ; les mèmes historiques n’offrent pas tous une attribution exploitable. Le build GooseRot embarque exclusivement les fichiers originaux de `Assets/Generated/`, les sept images fournies explicitement pour ce projet sous `Assets/User/GooseChaos/` et du code écrit pour ce dépôt.
