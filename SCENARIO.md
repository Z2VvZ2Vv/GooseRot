# GooseRot — scénario de référence

> Statut : concept initial enregistré  
> Version : 0.4  
> Durée totale : 6 minutes  
> Plateforme cible : Windows, idéalement dans une VM de démonstration  
> Ambiance : desktop goose, brainrot TikTok, chaos visuel contrôlé en `safe`, destruction réelle de la VM en `lab`, alertes système optionnelles et désactivables

> Profils : `safe`, `normal` et `lab` — voir `docs/PRODUCT_SPEC.md` et `docs/SAFETY_MODEL.md`

## Intention

GooseRot est un programme comique Windows mettant en scène une oie qui infiltre progressivement le bureau pendant six minutes. En `safe` et `normal`, l’expérience commence comme une petite nuisance absurde, escalade vers un chaos visuel contrôlé, puis se termine proprement. En `lab`, la même mise en scène masque une phase destructive réelle et se termine sur une VM saccagée.

Principe essentiel : **l’oie provoque elle-même le chaos**. Les actions de l’utilisateur peuvent déclencher des réactions bonus, mais la timeline ne doit jamais attendre un mouvement, un clic ou une réponse pour continuer. Même devant un bureau totalement inactif, les six minutes doivent rester remplies d’animations, de fenêtres déplacées, de textes et de gags autonomes.

Deuxième principe, ajouté en 0.4 : **chaque animation doit avoir une cause lisible**. Une image ne se matérialise pas, une oie va la chercher hors champ et la rapporte dans son bec. Le curseur n’est pas continuellement inutilisable, il est saisi par vagues et rendu entre deux. Les fenêtres ne s’accumulent pas jusqu’à la fin, elles sont dévorées par le glitch dans le dernier tiers.

## Phase 1 — 0:00 à 1:20 : The Mewing Infiltration

### 0:00 — Passive Entrance

Après le consentement, l’oie entre entièrement depuis le bord gauche ou droit, choisi par le seed, puis marche vers le premier tiers de l’écran avec une bulle :

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

### 0:35 — Elle s’en va vraiment

Vexée, l’oie annonce qu’elle va chercher du renfort, marche jusqu’au bord latéral et **sort complètement de l’écran**. Le bureau reste seul.

> FINE. THIS DESKTOP HAS NO AURA.  
> I AM GETTING REINFORCEMENTS.

Cette absence n’est pas un temps mort : c’est pendant qu’il n’y a personne que le faux Bloc-notes s’ouvre tout seul et se met à écrire.

### 0:45 — Auto-Typing (et la fenêtre qui refuse de partir)

Un faux Bloc-notes appartenant au processus GooseRot s’ouvre **sans oie à l’écran** et y compose rapidement un flux aléatoire de brainrot :

> skibidi rizzler alpha male grindset non-stop no cap fr fr...  
> ohio sigma mewing streak aura farming level 67...  
> fanum tax detected. jawline protocol activated...

Le texte est recomposé en permanence depuis une banque de mots, de nombres et de fragments de lettres. À `1:20`, la cadence ralentit mais ne s’arrête plus : GooseRot continue d’écrire environ chaque seconde dans son propre Bloc-notes, puis accélère fortement à partir de `4:00`. Aucune touche n’est injectée dans l’application active de l’utilisateur.

Tenter de fermer cette fenêtre ne marche pas durablement. Le `[X]` change son titre, la décale de 67 pixels et finit par la détruire, mais le moteur de timeline la recrée tant que la frappe chaotique est active. Le nettoyage final la détruit directement.

Cette fenêtre **ne peut pas non plus être rangée dans la barre des tâches** : elle n’a pas de bouton Réduire, `SC_MINIMIZE` est refusé, et toute réduction venant de l’extérieur — bouton de la barre des tâches, Afficher le Bureau, `Win+D` — est annulée à la frame suivante. L’oie commente chaque tentative.

### 1:05 — Le retour

L’oie revient **par le bord opposé**, en cacardant, et elle ne revient pas les ailes vides : la première image brainrot de la partie arrive dans son bec.

> I WENT TO GET SUPPLIES.  
> LOOK WHAT I FOUND.

## Phase 2 — 1:20 à 2:40 : Cursor & Window Hijack

### 1:20 — Sprint & Double Offset

L’oie prend le contrôle à la fois du curseur et des fenêtres, toujours avec le nombre `67` comme règle.

Quand l’utilisateur tente de cliquer sur une icône ou une fenêtre active, l’oie sprinte jusqu’au pointeur, s’y accroche avec son bec et **traîne elle-même le curseur de 67 pixels vers la droite**. La traction dure environ une seconde : un lien rose relie le bec au pointeur, un anneau pulse autour du curseur et le mot `GRABBED` s’affiche dessous. Chaque pas repart de la position réelle du pointeur, donc résister à la souris ne fait que déplacer le point de départ. Si le bord de l’écran bloque, l’oie tire dans l’autre sens, puis abandonne proprement. Elle affiche aussitôt une bulle :

> NO CLICK. ONLY 67.  
> Cursor privileges revoked.  
> Your aim had negative aura.

En parallèle, l’oie choisit régulièrement une fenêtre visible au hasard, sprinte jusqu’à sa barre de titre, s’y accroche et **déplace elle-même cette fenêtre de exactement 67 pixels** dans une direction choisie au hasard.

À partir de `1:35`, GooseRot lance aussi progressivement jusqu’à six vrais utilitaires Windows parmi Bloc-notes, Paint, Gestionnaire des tâches, Table des caractères, Invite de commandes et une instance séparée de l’Explorateur. Seuls les PID créés par GooseRot sont repositionnés aléatoirement et sollicités pour fermeture pendant le nettoyage ; une instance déjà ouverte par l’utilisateur n’est jamais adoptée.

Chaque déplacement est annoncé par une bulle de BD choisie aléatoirement :

> 67 PIXELS. PERFECTLY CALCULATED.  
> Your window had negative aura.  
> Interior design by Goose.  
> I put it there. Don't question the grindset.

Le déplacement de fenêtres se reproduit automatiquement toutes les quelques secondes, même si l’utilisateur ne touche à rien. Si aucun clic n’a lieu pendant quinze secondes, l’oie va chercher le curseur immobile et le pousse quand même de 67 pixels en annonçant :

> AFK IS NOT A DEFENSE.

Le curseur reste toujours dans les limites de l’écran. Les fenêtres restent entièrement visibles et reviennent à leur position initiale lors de la fermeture de GooseRot.

Le déplacement des fenêtres tierces s’arrête à `2:40`. La chasse ponctuelle au curseur laisse place à `4:00` à une tempête **par vagues**, tandis que la troupe continue de grandir jusqu’à la fin.

Depuis le début de cette phase, un petit panneau GooseRot est posé **sur le bouton Démarrer** et avale les clics qui l’atteignent : un menu Démarrer ouvert au milieu de la scène recouvrirait tout ce que la timeline est en train de construire. Aucun hook n’est installé, aucune fenêtre du shell n’est modifiée, et détruire ce panneau au nettoyage rend immédiatement le bouton à Windows. `Ctrl+Shift+Échap`, `Alt+Tab` et la sortie d’urgence `Échap` ne sont jamais touchés.

> START MENU: REVOKED.  
> There is a goose standing on it.

### 1:50 — Brainrot Subtitles

Des images brainrot — créatures italiennes et visages de chats en réaction — **sont toujours rapportées par une oie**. Le trajet est visible en entier : une oie libre sort par le bord le plus proche, disparaît, revient avec l’image dans le bec, traverse le bureau et la dépose. Aucune image n’apparaît d’elle-même ; s’il n’y a aucune oie disponible, la livraison attend simplement qu’il y en ait une. Les zones de dépôt protègent le HUD **et l’emplacement du futur tag `67`**. Du texte défile au-dessus de l’oie :

> Tralala la la la... 🎶  
> Tutti frutti cappuccina ☕  
> Bombardino crocodilo 🐊

Chaque image posée porte une petite croix `[x]`, et elle est **réellement cliquable**. La fermer marche — avec des conséquences immédiates :

- l’image se déchire en quelques images, barrée de rose ;
- `-6 700` d’aura ;
- deux oies partent aussitôt en chercher deux autres ;
- le glitch prend un pic, une fausse notification annonce le remplacement ;
- une fermeture sur trois réveille l’essaim de popups, et à partir de cinq fermetures la troupe gagne une oie supplémentaire.

Le compteur `PHOTOS TORN` s’affiche dans le HUD. Aucun hook clavier ou souris n’est installé : le clic est lu depuis la position du pointeur, puisque l’overlay laisse passer les clics.

### 2:20 — Clipboard Certified

Un gag visuel simule un verrouillage du collage sans installer de hook et sans lire ou modifier le presse-papiers :

> +10,000 AURA

Indépendamment de toute tentative de collage, l’oie tamponne aussi un énorme badge `CLIPBOARD CERTIFIED: +10,000 AURA` au milieu de l’écran, puis le traîne dans une zone réservée en bas à gauche. Le badge est rendu devant les images afin de rester lisible.

## Phase 3 — 2:40 à 4:00 : The “Spray-Painted 67” Squad

### 2:40 — Duplication

L’oie tremble puis se divise en trois oies distinctes, toutes cacardent, et l’essaim de popups commence.

Des fenêtres GooseRot imitent aléatoirement `Task Manager`, `File Explorer`, `Untitled - Notepad`, `Windows Security`, `Command Prompt` et d’autres outils système. **En fermer une en fait apparaître deux autres.** Le compteur de fenêtres ouvertes s’affiche dans le HUD. Au plafond protecteur de 67 fenêtres, toutes les fermetures ordinaires sont refusées ; seul le nettoyage de fin ou `Esc` maintenu deux secondes les détruit directement.

### 3:10 — The Graffiti & Vibe

- Avant le premier trait, les oies **débarrassent le mur** : toute image posée sur l’emplacement du tag est reprise dans un bec et raccrochée ailleurs.
- Oie 1 : peint un `67` géant en rose néon, **trait par trait, en direct**. Le tag occupe environ la moitié de la hauteur de l’écran. L’oie marche le long du tracé en suivant la buse, la peinture apparaît derrière elle, des coulures se mettent à couler sous les traits déjà posés et l’overspray s’accumule autour. Le tag est peint en dix-huit secondes.
- Oies 2 et 3 : se rangent **de part et d’autre du tag**, face à lui, et se balancent en rythme au lieu de traverser le tracé.

Le tag est dessiné après les images et sous une passe d’encre presque opaque : quel que soit l’écran, le fond du bureau ou le filtre de couleur en cours, le `67` garde une silhouette nette et ne peut pas être recouvert par une pile de photos.

### 3:36 — The Sigma Trap

Une boîte de dialogue apparaît au centre :

> Are you a Sigma Chad or a NPC?

Boutons : `[ SIGMA CHAD ]` / `[ NPC ]`

Le bouton `[ SIGMA CHAD ]` s’éloigne de 100 pixels dès que le pointeur le survole. Si l’utilisateur ne fait rien, il commence à fuir tout seul toutes les deux secondes tandis que l’oie le poursuit. Après huit secondes sans réponse, une oie appuie elle-même sur `[ NPC ]` et affiche :

> AFK detected. NPC confirmed.

## Phase 4 — 4:00 à 5:30 : Visual Chaos & Error Chorus

### 4:00 — Screen Shake et tempête par vagues

L’overlay principal déclenche une secousse rapide de 4 pixels toutes les deux secondes, ce qui garde le rythme lisible même si la VM ne restitue pas le son.

En mode complet, de courtes alertes Windows (`SystemHand`, `SystemQuestion`, `SystemExclamation` ou `SystemAsterisk`) ponctuent aussi le chaos. Elles sont asynchrones, espacées par l'horloge réelle et ne modifient jamais le volume système. `--mute` les désactive.

Au même instant, le curseur entre dans une tempête **cyclique**. Chaque cycle de 7,5 secondes comporte une vague où une cible mouvante et un tremblement haute fréquence tirent le pointeur, puis une fenêtre où le pointeur est **entièrement rendu à l’utilisateur** — pas un pixel ne lui est pris. La part saisie monte d’environ un tiers à environ deux tiers du cycle, et la violence de chaque vague monte avec elle, mais il reste toujours au moins deux secondes et demie de contrôle complet entre deux vagues. Le HUD affiche `CURSOR: SEIZED` ou `CURSOR: YOURS` pour que l’alternance soit lisible plutôt que subie. La sortie d’urgence par `Esc` reste disponible en permanence.

Si le menu Démarrer ou la recherche Windows recouvre le tag et les oies, GooseRot adresse `Esc` uniquement à cette surface shell et la masque. Aucun raccourci clavier global n’est synthétisé.

### Tout du long — Dégradation de l’affichage

Une intensité de glitch monte avec la timeline (proche de 0 au début, 0,18 à `2:40`, 0,26 à `3:10`, 0,42 à `4:00`, 0,58 à `4:30`, puis une montée **continue** de 0,70 à 1,00 entre `5:00` et la fin) et chaque gros gag ajoute un pic qui retombe. Le dernier tiers troque volontairement les fenêtres contre de la dégradation. Elle pilote, toujours à l’intérieur de l’overlay :

- des bandes de déchirure horizontales décalées en rose et cyan ;
- des blocs de framebuffer « corrompus » ;
- des scanlines CRT qui défilent ;
- une aberration chromatique rouge/cyan sur les textes lourds ;
- des curseurs fantômes qui se multiplient autour du vrai pointeur ;
- un faux cadre `explorer.exe — (Ne répond pas)` qui dérive ;
- des bandes de perte de signal qui défilent et cisaillent l’image au-delà de 0,62 ;
- de brefs flashs plein écran, cadencés par l’horloge réelle et de plus en plus probables à mesure que l’intensité monte.

### Tout du long — Fausses notifications

Des notifications style Windows glissent depuis le coin bas-droit, peintes dans l’overlay et jamais envoyées au centre de notifications :

> Sécurité Windows — Menace détectée : negative rizz.
> Windows Update — Installation de 67 mises à jour de l’aura…
> Explorateur de fichiers — explorer.exe ne répond plus. *(mensonge, il va très bien)*

### 4:30 — Color Filter

Un overlay transparent plein écran, à 15 % d’opacité, alterne la teinte du bureau entre vert Matrix et rose néon, en synchronisation avec les secousses.

### 5:00 — Final Monologue

La troupe, déjà bien plus nombreuse que les trois oies initiales, encercle le pointeur et affiche :

> CRITICAL ERROR: MAXIMUM BRAINROT REACHED.

À partir de cet instant, plus aucune fenêtre n’est ouverte. L’essaim, à son maximum de 67, est **dévoré par le glitch** trois fenêtres à la fois jusqu’à disparaître complètement vers `5:45`, chaque disparition ajoutant un pic de dégradation. La fin n’est plus un mur de boîtes de dialogue, c’est un affichage qui s’effondre.

> NO MORE WINDOWS.  
> ONLY DAMAGE.

## Phase 5 — 5:30 à 6:00 : The Final Countdown

### 5:30 — Red Timer

Un grand compte à rebours rouge apparaît en haut de l’écran :

> 00:30

### 5:45 — The Circle Dance

Le nombre d’oies accélère jusqu’à 67. Elles quittent leur spirale autour du pointeur et se répartissent sur une grille mouvante qui remplit tout l’écran.

### 5:58 — The Final Click

L’oie principale marche sur un bouton virtuel `[ DO NOT PRESS ]`.

### 6:00 — Fin selon le profil

Dans l’implémentation actuelle, une explosion rose/verte dévore l’overlay, l’écran devient noir, puis une dernière oie entre depuis un coin et annonce `GOODBYE, DUDE. YOU SHOULD'VE LISTENED.` avant la fermeture propre. Le nettoyage et la restauration ont lieu avant cette conclusion visuelle. Le contrat cible destructeur de `lab` décrit plus bas reste volontairement non implémenté.

L’appui de deux secondes sur `Esc` est une sortie d’urgence uniquement en `safe`. Il ne doit pas interrompre `normal` ou `lab`.

Le code actuel n’est pas encore aligné avec ce scénario cible : ses effets `lab` restent non destructeurs et l’appui long sur `Esc` y est encore traité.

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

À `5:58`, le bouton `[ DO NOT PRESS ]` déclenche l’explosion du noyau d’Aura au lieu de remettre le compteur à zéro.

- fermeture d’une image brainrot : `−6 700`
- clic sur le bouton Démarrer condamné : `−67`

### Micro-gags possibles

- L’oie vérifie brièvement sa jawline dans un faux miroir avant de lancer le Bloc-notes.
- Le titre du Bloc-notes devient momentanément `Untitled - Grindset`.
- Une notification factice annonce : `Windows Defender has detected: negative rizz`.
- Après avoir choisi `[ NPC ]`, la boîte répond : `Honesty bonus: +2 Aura`.
- Le bouton `[ SIGMA CHAD ]` fuit dans une direction différente à chaque survol, mais reste toujours visible.
- Pendant le compte à rebours, quelques oies tiennent de minuscules pancartes `6`, `7` et `TOO LATE`.

### Signature visuelle

- Rose néon : `#FF2DAA`
- Vert Matrix : `#39FF14`
- Rouge du compte à rebours : `#FF2438`
- Blanc cassé des bulles : `#FFFBEA`
- Encre des contours : `#1C1922`
- Style du `67` : bords irréguliers, coulures et léger halo lumineux
- Règle générale de l’interface : **rien n’est un rectangle arrondi propre**. Les plaques du HUD, les notifications et les bulles sont tracées à main levée, avec un bord qui frémit lentement, un coin coupé, une légère inclinaison et parfois un bout de scotch. Les tracés sont déterministes (bruit indexé par frame) pour éviter le grésillement pixel par pixel.

## Garde-fous de conception

Pour que le gag reste amusant et testable en `safe`, et que le risque de `lab` soit sans ambiguïté :

- afficher au lancement un mode démo explicitement consenti ;
- réserver les hooks globaux ainsi que le déplacement du curseur et des fenêtres à la VM ou à un mode opt-in ;
- fournir en `safe` une sortie d’urgence par `Esc` maintenu deux secondes ; ne pas exposer cette sortie dans `normal` ou `lab` ;
- ne jamais rendre le pointeur inutilisable en continu : chaque cycle de tempête doit comporter une fenêtre où il est intégralement rendu ;
- ne bloquer le bouton Démarrer qu’avec une fenêtre appartenant à GooseRot, destructible au nettoyage, sans hook ni modification du shell, et sans jamais toucher `Ctrl+Shift+Échap`, `Alt+Tab` ni `Échap` ;
- afficher avant `lab` un avertissement bloquant annonçant la corruption des fichiers, du Registre et du démarrage ;
- interdire `lab` hors d’une VM isolée et jetable ;
- ne jamais lire ou modifier le presse-papiers système ;
- fermer uniquement le Bloc-notes lancé par GooseRot ;
- ne jamais enregistrer les frappes ni lire le contenu du presse-papiers ;
- permettre de désactiver les sons, les secousses et les flashes avec `--mute`, `--reduced-motion` et `--no-flashes` ;
- utiliser uniquement un faux écran de redémarrage en `safe` et `normal`.

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
- pointeur immobile → l’oie le pousse de 67 pixels, puis les oies dansent autour de sa nouvelle position ;
- image fermée par l’utilisateur → deux oies partent immédiatement en rechercher deux autres ;
- aucune oie libre pour une livraison → l’image attend plutôt que d’apparaître seule.

Ainsi, l’interaction ajoute de l’imprévu, mais l’inaction ne crée jamais de temps mort.
