# GooseRot — spécification produit

## Expérience

L’application attend d’abord silencieusement entre 10 et 30 secondes, puis joue automatiquement le scénario de sept minutes et demie décrit dans `SCENARIO.md`. L’utilisateur peut provoquer des réactions supplémentaires, mais aucune étape ne dépend de son activité : un bureau laissé totalement inactif est ensuite occupé jusqu’à la clôture.

L’application raconte une histoire : une oie vient inspecter le bureau, ouvre un dossier, le rédige, rassemble des pièces à conviction, peint sa note sur le mur et clôt le dossier.

L’oie est un acteur visible. Au lancement normal, elle reste entièrement hors écran et immobile pendant l’attente initiale, puis entre depuis l’un des quatre bords. Lorsqu’un effet concerne le curseur, une fenêtre, une pièce à conviction ou le presse-papiers, elle se déplace d’abord jusqu’à la cible puis annonce son action dans une bulle. Rien n’apparaît sans auteur : les pièces sont rapportées depuis hors champ dans un bec, le dossier s’ouvre sous le bec de l’oie qui vient de tamponner le bureau, et la fiche de notation n’existe pas tant que l’inspectrice n’a pas commencé à noter.

L’expérience laisse toujours une prise à l’utilisateur. La tempête de curseur fonctionne par vagues et rend le pointeur entre deux, les images posées peuvent réellement être fermées — au prix d’une escalade annoncée — et les petits avis GooseRot restent des fenêtres normalement cliquables.

## Profils d’exécution

| Profil | Identifiant | Redémarrage réel | Modifications persistantes | Usage |
|---|---|---:|---:|---|
| Inoffensif | `safe` | Non | Aucune | Démo, vidéo, poste personnel |
| Normal | `normal` | Non, simulé | Aucune | VM ou poste de test consenti |
| Destructeur | `lab` | Possible / système potentiellement non amorçable | Oui, irréversibles | VM isolée et jetable uniquement |

### `safe`

- joue toute la timeline ;
- accumule au maximum 100 avis GooseRot compacts explicitement titrés `AURA INSPECTION`, puis les ferme un par un avant l’obturateur final ;
- simule l’interception du presse-papiers dans ses propres overlays ;
- déplace temporairement le curseur et les fenêtres ;
- ferme l’expérience autour de `7:30` par un obturateur, une surexposition, un faux crash et l’oie d’adieu, puis se termine ;
- restaure toutes les fenêtres déplacées, rend le bouton Démarrer et laisse le presse-papiers intact.

### `normal`

- mêmes effets que `safe` ;
- le gag de collage reste une simulation visuelle et ne lit pas le presse-papiers ;
- autour de `7:30`, l’obturateur, la surexposition et le crash sont simulés ;
- n’utilise ni élévation UAC, ni fermeture forcée des applications ;

### `lab`

- **profil volontairement destructeur, sans restauration promise** ;
- réservé à une VM isolée, jetable, sans dossier partagé, périphérique attaché, donnée importante ni secret ;
- corrompt ou supprime des fichiers et dégrade le Registre Windows ;
- endommage la configuration ou la chaîne de démarrage, de sorte que Windows peut ne plus démarrer ;
- laisse volontairement le système dans un état saccagé et potentiellement irrécupérable à la fin ;
- ne propose pas la sortie par appui long sur `Esc` ; la récupération se fait exclusivement hors de la machine invitée, par arrêt et restauration du snapshot dans l’hyperviseur.

> **État de l’implémentation :** ce bloc décrit le contrat cible de `lab`. Le code actuellement présent dans le dépôt conserve encore la restauration et la sortie `Esc`, et n’implémente pas ces effets destructeurs. Cet écart doit rester explicite jusqu’à l’alignement du binaire.

## Exécutables

```text
GooseRot-Safe.exe
GooseRot-Normal.exe
GooseRot-Lab.exe
```

Le profil est compilé et verrouillé dans chaque fichier. La variante `Lab` intègre le choix VM et démarre directement sans dialogue d’avertissement ou de confirmation propre à GooseRot. Son manifeste `requireAdministrator` fait afficher une demande UAC unique par lancement ; un refus empêche le démarrage, sans nouvelle demande automatique. Les variantes `Safe` et `Normal` utilisent `asInvoker`.

Options de développement :

```text
--start-at 00:00
--duration-scale 1.0
--primary-monitor-only
--fake-reboot
--boot-game
--preview
--no-desktop-effects
--seed 67
```

Le délai initial est déterministe pour un `--seed` donné et reste compris entre 10 et 30 secondes réelles, indépendamment de `--duration-scale`. Un `--start-at` strictement supérieur à `00:00` le saute afin de reprendre directement au point demandé.

## Contrôles invariants

- maintenir `Esc` pendant deux secondes : restauration et arrêt d’urgence **uniquement en `safe`** ;
- `normal` et `lab` ne doivent pas traiter cet appui long comme une commande de sortie ;
- le Gestionnaire des tâches reste utilisable en `safe`, y compris par `Ctrl+Shift+Échap` ; aucune disponibilité n’est garantie après les dégradations du profil `lab` ;
- le bouton Démarrer peut être recouvert par une fenêtre GooseRot pendant la démonstration ; elle est détruite au nettoyage et ne modifie ni le shell ni aucun raccourci clavier ;
- la tempête de curseur ne doit jamais être continue : chaque cycle rend intégralement le pointeur pendant au moins deux secondes ;
- aucune fenêtre, boîte de dialogue, icône ou notification ne doit prétendre être une alerte système ; les avis compacts restent explicitement attribués à `AURA INSPECTION` ;
- la fiche de notation d'aura ne doit pas exister avant que l'inspectrice ne l'ait ouverte ;
- une seconde instance quitte immédiatement ;
- le presse-papiers n’est jamais lu, journalisé ou envoyé ailleurs.
