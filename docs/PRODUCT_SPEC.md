# GooseRot — spécification produit

## Expérience

L’application joue automatiquement le scénario décrit dans `SCENARIO.md`. L’utilisateur peut provoquer des réactions supplémentaires, mais aucune étape ne dépend de son activité : un bureau laissé totalement inactif reste chaotique pendant les cinq minutes.

L’oie est un acteur visible. Lorsqu’un effet concerne le curseur, une fenêtre ou le presse-papiers, elle se déplace d’abord jusqu’à la cible puis annonce son action dans une bulle de BD.

## Profils d’exécution

| Profil | Identifiant | Redémarrage réel | Modifications persistantes | Usage |
|---|---|---:|---:|---|
| Inoffensif | `safe` | Non | Aucune | Démo, vidéo, poste personnel |
| Normal | `normal` | Non, simulé | Aucune | VM ou poste de test consenti |
| VM Chaos | `lab` | Non, simulé | Aucune | Snapshot jetable |

### `safe`

- joue toute la timeline ;
- simule l’interception du presse-papiers dans ses propres overlays ;
- déplace temporairement le curseur et les fenêtres ;
- affiche un faux redémarrage autour `5:00`, puis se ferme ;
- restaure toutes les fenêtres déplacées et laisse le presse-papiers intact.

### `normal`

- mêmes effets que `safe` ;
- le gag de collage reste une simulation visuelle et ne lit pas le presse-papiers ;
- autour de `5:00`, un redémarrage visuel est simulé ;
- n’utilise ni élévation UAC, ni fermeture forcée des applications ;

### `lab`

- densité d’overlays et vitesse d’animation supérieures ;
- glitch et faux BSOD visuels à la fin ;
- `--fake-reboot` peut enchaîner sur la Preview sûre du mini-jeu ;
- `--boot-game` est réservé à un futur package firmware signé et validé à l’exécution ; il échoue fermé avec le bundle expérimental actuel ;
- aucune écriture boot, modification du firmware ou persistance.

## Commandes prévues

```text
GooseRot.exe --mode safe
GooseRot.exe --mode normal
GooseRot.exe --mode lab --vm-confirmed
```

Options de développement :

```text
--start-at 00:00
--duration-scale 1.0
--primary-monitor-only
--fake-reboot
--boot-game
--vm-confirmed
--preview
--no-desktop-effects
--seed 67
```

## Contrôles invariants

- maintenir `Esc` pendant deux secondes : arrêt d’urgence dans tous les profils ;
- `Ctrl+Shift+Esc` et le Gestionnaire des tâches restent utilisables dans tous les profils ;
- une seconde instance quitte immédiatement ;
- le presse-papiers n’est jamais lu, journalisé ou envoyé ailleurs.
