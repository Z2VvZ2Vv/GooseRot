# GooseRot — scénario de référence

> Statut : concept initial enregistré  
> Version : 0.3  
> Durée totale : 5 minutes  
> Plateforme cible : Windows, idéalement dans une VM de démonstration  
> Ambiance : desktop goose, brainrot TikTok, faux chaos système, aucun son requis

> Profils : `safe`, `normal` et `lab` — voir `docs/PRODUCT_SPEC.md` et `docs/SAFETY_MODEL.md`

## Intention

GooseRot est un programme comique Windows mettant en scène une oie qui infiltre progressivement le bureau pendant cinq minutes. L’expérience commence comme une petite nuisance absurde, escalade vers un chaos visuel contrôlé, puis se termine proprement.

Principe essentiel : **l’oie provoque elle-même le chaos**. Les actions de l’utilisateur peuvent déclencher des réactions bonus, mais la timeline ne doit jamais attendre un mouvement, un clic ou une réponse pour continuer. Même devant un bureau totalement inactif, les cinq minutes doivent rester remplies d’animations, de fenêtres déplacées, de textes et de gags autonomes.

## Phase 1 — 0:00 à 1:00 : The Mewing Infiltration

### 0:00 — Passive Entrance

L’oie apparaît exactement au centre de l’écran avec une bulle :

> 🤫🧏‍♂️ *Mewing in progress... DO NOT DISTURB.*

### 0:15 — Aura Deduction

Déplacer le curseur de plus de 50 pixels fait sautiller l’oie sur place et déclenche une boîte de dialogue Win32 classique.

- Titre : `Aura Points Deducted`
- Texte : `You broke the streak. -10,000 Aura. Apologize immediately.`
- Boutons : `[ Sorry ]` / `[ Forgive Me ]`

Si l’utilisateur ne bouge pas, l’oie perd patience au bout de cinq secondes, bondit toute seule près du pointeur et affiche quand même la boîte avec une bulle :

> I felt your aura move. Nice try.

Sans réponse, la boîte se ferme seule après quelques secondes et l’oie sélectionne arbitrairement `[ Sorry ]` avant de commenter :

> Apology barely accepted.

### 0:40 — Auto-Typing (et la fenêtre qui refuse de partir)

L’oie ouvre un faux Bloc-notes appartenant au processus GooseRot et y compose rapidement un flux aléatoire de brainrot :

> skibidi rizzler alpha male grindset non-stop no cap fr fr...  
> ohio sigma mewing streak aura farming level 67...  
> fanum tax detected. jawline protocol activated...

Le texte est recomposé en permanence depuis une banque de mots, de nombres, d’emojis et de ponctuation. L’oie écrit **en boucle jusqu’au gag suivant à `1:00`**, sans répéter exactement la même phrase. À `1:00`, elle ajoute une dernière ligne — `SESSION SAVED. +67 AURA.` — puis réduit directement le Bloc-notes dans la barre des tâches.

Tenter de fermer cette fenêtre ne marche pas. Le `[X]` est refusé trois fois : le titre change (`NON - Grindset`, `Untitled - Grindset (le bouton est décoratif)`), la fenêtre se décale de 67 pixels et l’oie commente. À la quatrième tentative elle se ferme… et revient aussitôt sous le nom `Untitled - Grindset (2) - récupéré automatiquement`. Après ce retour unique, elle se ferme normalement.

## Phase 2 — 1:00 à 2:15 : Cursor & Window Hijack

### 1:00 — Sprint & Double Offset

L’oie prend le contrôle à la fois du curseur et des fenêtres, toujours avec le nombre `67` comme règle.

Quand l’utilisateur tente de cliquer sur une icône ou une fenêtre active, l’oie sprinte jusqu’au pointeur, s’y accroche avec son bec et **traîne elle-même le curseur de 67 pixels vers la droite**. La traction dure environ une seconde : un lien rose relie le bec au pointeur, un anneau pulse autour du curseur et le mot `GRABBED` s’affiche dessous. Chaque pas repart de la position réelle du pointeur, donc résister à la souris ne fait que déplacer le point de départ. Si le bord de l’écran bloque, l’oie tire dans l’autre sens, puis abandonne proprement. Elle affiche aussitôt une bulle :

> NO CLICK. ONLY 67.  
> Cursor privileges revoked.  
> Your aim had negative aura.

En parallèle, l’oie choisit régulièrement une fenêtre visible au hasard, sprinte jusqu’à sa barre de titre, s’y accroche et **déplace elle-même cette fenêtre de exactement 67 pixels** dans une direction choisie au hasard.

Chaque déplacement est annoncé par une bulle de BD choisie aléatoirement :

> 67 PIXELS. PERFECTLY CALCULATED.  
> Your window had negative aura.  
> Interior design by Goose.  
> I put it there. Don't question the grindset.

Le déplacement de fenêtres se reproduit automatiquement toutes les quelques secondes, même si l’utilisateur ne touche à rien. Si aucun clic n’a lieu pendant quinze secondes, l’oie va chercher le curseur immobile et le pousse quand même de 67 pixels en annonçant :

> AFK IS NOT A DEFENSE.

Le curseur reste toujours dans les limites de l’écran. Les fenêtres restent entièrement visibles et reviennent à leur position initiale lors de la fermeture de GooseRot.

### 1:30 — Brainrot Subtitles

Des images de mèmes TikTok — visages de chats en réaction, badges « Aura Points », etc. — apparaissent automatiquement sous forme d’overlays transparents, à intervalles irréguliers. Du texte défile au-dessus de l’oie :

> Tralala la la la... 🎶  
> Tutti frutti cappuccina ☕  
> Bombardino crocodilo 🐊

### 2:00 — Clipboard Certified

Un gag visuel simule un verrouillage du collage sans installer de hook et sans lire ou modifier le presse-papiers :

> +9999 AURA

Indépendamment de toute tentative de collage, l’oie tamponne aussi un énorme badge `CLIPBOARD CERTIFIED: +9999 AURA` au milieu de l’écran, puis le traîne jusqu’à un coin comme un autocollant mal posé.

## Phase 3 — 2:15 à 3:30 : The “Spray-Painted 67” Squad

### 2:15 — Duplication

L’oie tremble puis se divise en trois oies distinctes, toutes cacardent, et l’essaim de popups commence.

Des fenêtres GooseRot apparaissent régulièrement : `aura_report_FINAL_v3.txt`, `ne_pas_fermer.exe`, `Propriétés de : Ohio`… Chacune propose un bouton `FERMER`. **Le fermer en fait apparaître deux autres.** Le compteur de fenêtres ouvertes s’affiche dans le HUD. Au plafond de neuf fenêtres, elles résistent une fois (`NON. (réessayez, pour voir)`) puis se laissent fermer, pour que le bureau puisse se vider.

### 2:45 — The Graffiti & Vibe

- Oie 1 : peint un `67` géant en rose néon, **trait par trait, en direct**. Le tag occupe environ la moitié de la hauteur de l’écran. L’oie marche le long du tracé en suivant la buse, la peinture apparaît derrière elle, des coulures se mettent à couler sous les traits déjà posés et l’overspray s’accumule autour. Le tag est peint en une quinzaine de secondes.
- Oies 2 et 3 : se regroupent autour du `67` et démarrent une animation de balancement synchronisé, comme si elles vibraient devant le graffiti.

### 3:15 — The Sigma Trap

Une boîte de dialogue apparaît au centre :

> Are you a Sigma Chad or a NPC?

Boutons : `[ SIGMA CHAD ]` / `[ NPC ]`

Le bouton `[ SIGMA CHAD ]` s’éloigne de 100 pixels dès que le pointeur le survole. Si l’utilisateur ne fait rien, il commence à fuir tout seul toutes les deux secondes tandis que l’oie le poursuit. Après huit secondes sans réponse, une oie appuie elle-même sur `[ NPC ]` et affiche :

> AFK detected. NPC confirmed.

## Phase 4 — 3:30 à 4:30 : Visual Chaos — No-Sound VM Mode

### 3:30 — Screen Shake

Pour compenser l’absence de son dans la VM, l’overlay principal déclenche une secousse rapide de 4 pixels toutes les deux secondes.

### Tout du long — Dégradation de l’affichage

Une intensité de glitch monte avec la timeline (0 avant `1:30`, environ 0,25 à `2:45`, 0,4 à `3:30`, 0,6 à `4:00`, 0,75 à `4:30`, presque 1 à la fin) et chaque gros gag ajoute un pic qui retombe. Elle pilote, toujours à l’intérieur de l’overlay :

- des bandes de déchirure horizontales décalées en rose et cyan ;
- des blocs de framebuffer « corrompus » ;
- des scanlines CRT qui défilent ;
- une aberration chromatique rouge/cyan sur les textes lourds ;
- des curseurs fantômes qui se multiplient autour du vrai pointeur ;
- un faux cadre `explorer.exe — (Ne répond pas)` qui dérive ;
- de brefs flashs blancs ou bleus.

### Tout du long — Fausses notifications

Des notifications style Windows glissent depuis le coin bas-droit, peintes dans l’overlay et jamais envoyées au centre de notifications :

> Sécurité Windows — Menace détectée : negative rizz.  
> Windows Update — Installation de 67 mises à jour de l’aura…  
> Explorateur de fichiers — explorer.exe ne répond plus. *(mensonge, il va très bien)*

### 4:00 — Color Filter

Un overlay transparent plein écran, à 15 % d’opacité, alterne la teinte du bureau entre vert Matrix et rose néon, en synchronisation avec les secousses.

### 4:15 — Final Monologue

Les trois oies encerclent le pointeur et affichent simultanément :

> CRITICAL ERROR: MAXIMUM BRAINROT REACHED.

## Phase 5 — 4:30 à 5:00 : The Final Countdown

### 4:30 — Red Timer

Un grand compte à rebours rouge apparaît en haut de l’écran :

> 00:30

### 4:45 — The Circle Dance

Les oies tournent rapidement en cercle autour du pointeur.

### 4:59 — The Final Click

L’oie principale marche sur un bouton virtuel `[ RESET AURA ]`.

### 5:00 — Graceful Shutdown

L’application restaure d’abord le bureau et se ferme proprement. Tous les profils utilisent un faux redémarrage visuel ; aucun redémarrage Windows réel n’est implémenté. Le profil `lab --fake-reboot` lance la Preview de **AURA 67: Firmware Frenzy** lorsqu’elle est disponible. Un bundle UEFI/BIOS expérimental peut être construit séparément, mais il est non signé, non installable et non validé à l’exécution. `lab --boot-game` reste donc désactivé et n’émet jamais de faux handoff.

---

## Pistes créatives ajoutées

Ces idées ne remplacent pas le scénario de base ; elles forment une réserve pour les prochaines versions.

### Un fil rouge : le compteur d’Aura

Afficher discrètement un compteur d’Aura dès `0:15`, puis le faire évoluer à chaque gag. Les valeurs peuvent devenir volontairement absurdes :

- mouvement de souris : `−10 000`
- fenêtre déplacée par l’oie : `−67`
- choix « NPC » : `−1 000 000`
- tentative de collage : `+9 999`
- apparition du graffiti : compteur remplacé momentanément par `67`

À `4:59`, le bouton `[ RESET AURA ]` ramène le compteur à `0`, ce qui donne une vraie conclusion visuelle.

### Micro-gags possibles

- L’oie vérifie brièvement sa jawline dans un faux miroir avant de lancer le Bloc-notes.
- Le titre du Bloc-notes devient momentanément `Untitled - Grindset`.
- Une notification factice annonce : `Windows Defender has detected: negative rizz`.
- Après avoir choisi `[ NPC ]`, la boîte répond : `Honesty bonus: +2 Aura`.
- Le bouton `[ SIGMA CHAD ]` fuit dans une direction différente à chaque survol, mais reste toujours visible.
- Pendant le compte à rebours, les trois oies tiennent de minuscules pancartes `6`, `7` et `RESET`.

### Signature visuelle

- Rose néon : `#FF2DAA`
- Vert Matrix : `#39FF14`
- Rouge du compte à rebours : `#FF2438`
- Blanc cassé des bulles : `#FFFBEA`
- Encre des contours : `#1C1922`
- Style du `67` : bords irréguliers, coulures et léger halo lumineux
- Règle générale de l’interface : **rien n’est un rectangle arrondi propre**. Les plaques du HUD, les notifications et les bulles sont tracées à main levée, avec un bord qui frémit lentement, un coin coupé, une légère inclinaison et parfois un bout de scotch. Les tracés sont déterministes (bruit indexé par frame) pour éviter le grésillement pixel par pixel.

## Garde-fous de conception

Pour que le gag reste amusant et testable :

- afficher au lancement un mode démo explicitement consenti ;
- réserver les hooks globaux ainsi que le déplacement du curseur et des fenêtres à la VM ou à un mode opt-in ;
- fournir une sortie d’urgence toujours active, par exemple `Ctrl + Shift + Esc` maintenu deux secondes ;
- ne jamais lire ou modifier le presse-papiers système ;
- fermer uniquement le Bloc-notes lancé par GooseRot ;
- ne jamais enregistrer les frappes ni lire le contenu du presse-papiers ;
- permettre de désactiver les secousses et les flashes ;
- utiliser uniquement un faux écran de redémarrage ;
- ne jamais demander ou forcer un redémarrage Windows réel.

## Découpage technique envisagé

Le scénario pourra être implémenté comme une machine à états pilotée par une horloge unique :

1. une fenêtre overlay transparente et toujours au premier plan ;
2. un moteur d’animation pour une ou plusieurs oies ;
3. une timeline déclarative contenant les événements horodatés ;
4. un gestionnaire d’interactions contrôlées pour la souris et le clavier ;
5. un système de nettoyage centralisé garantissant une fermeture propre.

La timeline doit rester indépendante du rendu afin de pouvoir modifier facilement les horaires, désactiver un gag ou lancer une phase seule pendant les tests.

## Règle d’autonomie de la timeline

Chaque gag interactif doit posséder une variante de secours autonome :

- mouvement attendu → l’oie simule une accusation et continue ;
- clic attendu → une oie choisit ou clique elle-même après un délai ;
- collage attendu → un gag visuel équivalent apparaît automatiquement ;
- fenêtre attendue → l’oie en choisit une parmi les fenêtres visibles, sinon elle déplace son propre faux panneau ;
- pointeur immobile → l’oie le pousse de 67 pixels, puis les oies dansent autour de sa nouvelle position.

Ainsi, l’interaction ajoute de l’imprévu, mais l’inaction ne crée jamais de temps mort.
