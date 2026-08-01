# GooseRot — scénario de référence

> Statut : concept initial enregistré  
> Version : 0.5  
> Durée totale : 7 minutes 30  
> Plateforme cible : Windows, idéalement dans une VM de démonstration  
> Ambiance : desktop goose, bureaucratie absurde, chaos visuel contrôlé en `safe`, destruction réelle de la VM en `lab`, alertes système optionnelles et désactivables

> Profils : `safe`, `normal` et `lab` — voir `docs/PRODUCT_SPEC.md` et `docs/SAFETY_MODEL.md`

## Intention

**Une oie vient inspecter votre bureau.** C’est toute l’histoire, et tout le reste en découle.

Elle arrive, elle se présente, elle fait le tour du propriétaire, elle ouvre un dossier, elle y consigne ses conclusions, elle sort chercher des pièces à conviction, elle peint votre note sur le mur, elle rend son verdict, et elle ferme le dossier — en emportant l’écran avec.

Trois principes tiennent la mise en scène :

1. **La timeline ne dépend jamais de l’utilisateur.** Un bureau totalement inactif reste rempli pendant sept minutes et demie. Les actions de l’utilisateur ajoutent des réactions, elles ne débloquent rien.
2. **Chaque animation a une cause lisible.** Une image ne se matérialise pas : une oie va la chercher hors champ et la rapporte dans son bec. Le dossier ne s’ouvre pas tout seul : l’oie tamponne le bureau et la fenêtre s’ouvre sous son bec. Le compteur d’aura n’existe pas au lancement : il apparaît quand l’inspectrice commence à noter.
3. **Rien n’imite Windows.** GooseRot n’ouvre aucune fausse fenêtre système, ne dessine aucune fausse boîte de dialogue et ne peint aucun faux cadre « Ne répond pas ». Tout ce qui s’affiche appartient visiblement à l’inspection. Le seul décalage acceptable avec Windows est celui que l’oie provoque elle-même.

## Phase 1 — 0:00 à 2:45 : L’inspection

L’ouverture est volontairement lente. Il ne se passe rien de spectaculaire pendant presque une minute, et c’est le sujet : quelqu’un est entré chez vous et prend des notes.

### 0:00 — L’inspectrice arrive

L’oie entre entièrement depuis le bord gauche ou droit, choisi par le seed, et avance sans se presser. Pas de compteur, pas de HUD, pas de fenêtre. Juste une oie sur votre bureau.

> Do not mind me.  
> I am just having a look around.

### 0:20 — Les papiers

Elle s’arrête, cacarde une fois, et annonce ce qu’elle fait là.

> AURA INSPECTION.  
> I am the inspector. Stay where you are.

### 0:45 — La tournée

Elle parcourt le site selon un **itinéraire fixe** — les quatre coins puis le centre — en s’arrêtant à chaque station pour commenter ce qu’elle voit. Ce n’est pas une déambulation aléatoire : c’est un tour d’inspection, et il se lit comme tel.

> Top left. Dust. Noted.  
> Top right. This is where the scorecard goes.  
> Bottom right. Something lives here.  
> Bottom left. No comment. Written down anyway.  
> Centre of the site. This will do.

### 1:15 — Le dossier s’ouvre

Arrivée au centre, l’oie tamponne le bureau. **La fenêtre de dossier s’ouvre sous son bec**, exactement là où elle se trouve — jamais au centre par défaut, jamais sans cause visible.

> Opening the file.  
> Everything from here is written down.

Le dossier est une fenêtre GooseRot intitulée `AURA INSPECTION - case 67`. L’oie y écrit **à la main** : caractère par caractère, avec une cadence irrégulière, un temps d’arrêt après chaque point, un temps plus long après un saut de ligne, et de temps en temps une faute qu’elle remarque une seconde trop tard et qu’elle efface. Ce n’est pas un flux de mots collés, c’est quelqu’un qui rédige.

```
AURA INSPECTION -- CASE 67
Site: this desktop. Inspector: a goose.
Authorisation: not required. I am already inside.

FINDING 1. Arrived on site. Nobody stopped me.
That is, in itself, the first finding.
```

Chaque étape suivante de la timeline ajoute son constat au même dossier. Le texte n’est jamais aléatoire : c’est le compte rendu de ce que le spectateur vient de voir.

Cette fenêtre refuse de se fermer pendant un temps — le `[X]` change son titre, la décale de 67 pixels, et si elle finit détruite l’inspectrice la rouvre en reprenant le texte là où il s’était arrêté. Elle **ne peut pas non plus être rangée dans la barre des tâches** : pas de bouton Réduire, `SC_MINIMIZE` refusé, et toute réduction venue de l’extérieur — bouton de la barre des tâches, Afficher le Bureau, `Win+D` — est annulée à la frame suivante.

### 1:40 — La première déduction

**C’est ici, et pas avant, que le compteur d’aura existe.** Il ne fait pas partie du décor du programme : l’inspectrice vient d’ouvrir une fiche de notation, et la fiche tombe en place en haut à droite, avec un léger dépassement, comme une carte plaquée sur un tableau.

> Baseline aura measured.  
> I am going to need a bigger form.

Une boîte GooseRot demande un accusé de réception :

- Titre : `AURA INSPECTION - Notice of Deduction`
- Texte : `Case 67. Baseline aura recorded at -10,000. Please acknowledge the finding.`
- Boutons : `[ I ACKNOWLEDGE ]` / `[ I AM SORRY ]`

Sans réponse, la boîte se ferme seule et l’inspectrice consigne l’absence de réponse.

### 2:05 — Elle sort chercher des preuves

Le dossier étant ouvert, elle a besoin de pièces à conviction. Elle marche jusqu’au bord et **sort complètement de l’écran**. Le bureau reste seul, avec un dossier qui continue de se remplir tout seul — ce qui est bien plus inquiétant qu’une oie visible.

> I need evidence.  
> Do not touch anything while I am out.

### 2:28 — Elle revient chargée

Elle revient **par le bord opposé**, la première pièce à conviction dans le bec.

## Phase 2 — 2:45 à 3:55 : Relevés sur site

### 2:45 — Mesure du pointeur et des fenêtres

L’oie prend le contrôle à la fois du curseur et des fenêtres, toujours avec le nombre `67` comme règle.

Quand l’utilisateur tente de cliquer sur une icône ou une fenêtre active, l’oie sprinte jusqu’au pointeur, s’y accroche avec son bec et **traîne elle-même le curseur de 67 pixels vers la droite**. La traction dure environ une seconde : un lien rose relie le bec au pointeur, un anneau pulse autour du curseur et le mot `GRABBED` s’affiche dessous. Chaque pas repart de la position réelle du pointeur, donc résister à la souris ne fait que déplacer le point de départ. Si le bord de l’écran bloque, l’oie tire dans l’autre sens, puis abandonne proprement. Elle affiche aussitôt une bulle :

> Pointer seized for measurement. Hold still.  
> Pointer displaced by exactly 67 pixels. Within tolerance. Barely.  
> Sample taken. You may have it back.

En parallèle, l’oie choisit régulièrement une fenêtre visible au hasard, sprinte jusqu’à sa barre de titre, s’y accroche et **déplace elle-même cette fenêtre de exactement 67 pixels** dans une direction choisie au hasard.

À partir de `2:58`, GooseRot lance progressivement jusqu’à six vrais utilitaires Windows parmi Bloc-notes, Paint, Gestionnaire des tâches, Table des caractères, À propos de Windows et une instance séparée de l’Explorateur. Chaque enfant est créé suspendu, assigné au Job Object privé qui se ferme avec GooseRot, puis seulement démarré ; si cette protection échoue, le lancement est annulé. Les plus anciens sont sollicités pour fermeture et tout lancement cesse avant le monologue final. Seuls les PID créés par GooseRot sont suivis ; une instance déjà ouverte par l’utilisateur n’est jamais adoptée.

Chaque déplacement est annoncé par une bulle de BD choisie aléatoirement :

> Window realigned to 67 pixels. Regulation spacing.  
> That window was out of compliance.  
> Corrected. Do not thank me in writing.  
> Noted, moved, logged. Next.

Le déplacement de fenêtres se reproduit automatiquement toutes les quelques secondes, même si l’utilisateur ne touche à rien. Si aucun clic n’a lieu pendant quinze secondes, l’oie va chercher le curseur immobile et prend son relevé quand même :

> Taking a pointer sample. Hold still.

Le curseur reste toujours dans les limites de l’écran. Les fenêtres restent entièrement visibles et reviennent à leur position initiale lors de la fermeture de GooseRot.

Le déplacement des fenêtres tierces s’arrête à `3:55`. La chasse ponctuelle au curseur laisse place à `5:15` à une tempête **par vagues**, tandis que la troupe continue de grandir jusqu’à la fin.

Depuis le début de cette phase, un petit panneau GooseRot est posé **sur le bouton Démarrer** et avale les clics qui l’atteignent : un menu Démarrer ouvert au milieu de la scène recouvrirait tout ce que la timeline est en train de construire. En expérience complète uniquement, un garde clavier temporaire absorbe aussi `Win gauche` et `Win droite`. Aucune fenêtre du shell n’est modifiée et aucun réglage n’est persistant : le garde reste actif jusque pendant les visuels de conclusion, puis rend immédiatement les touches à Windows ; un arrêt d’urgence le retire sans attendre. `Ctrl+Shift+Échap`, `Alt+Tab` et la sortie d’urgence `Échap` ne sont jamais touchés.

> Start menu sealed for the inspection.  
> There is a goose standing on it.

### 3:10 — Les pièces à conviction

Les pièces à conviction — créatures italiennes, visages de chats en réaction et photos d’oies — **sont toujours rapportées par une oie**. Le trajet est visible en entier : une oie libre sort par le bord le plus proche, disparaît, revient avec l’image dans le bec, traverse le bureau et la dépose. Aucune image n’apparaît d’elle-même ; s’il n’y a aucune oie disponible, la livraison attend simplement qu’il y en ait une. Les zones de dépôt protègent le HUD **et l’emplacement du futur tag `67`**. Du texte défile au-dessus de l’oie :

Sept photos d’oies fournies pour le projet rejoignent le même pool de livraison : comme les sept images historiques, chacune est rapportée depuis hors champ par une oie, conserve son ratio et reste posée jusqu’à sa fermeture ou à la fin de l’expérience.

> Tralala la la la... 🎶  
> Tutti frutti cappuccina ☕  
> Bombardino crocodilo 🐊

Chaque image posée porte une petite croix `[x]`, et elle est **réellement cliquable**. La fermer marche — avec des conséquences immédiates :

- l’image se déchire en quelques images, barrée de rose ;
- `-6 700` d’aura ;
- deux oies partent aussitôt en chercher deux autres ;
- le glitch prend un pic, une fausse notification annonce le remplacement ;
- à partir de cinq destructions, l’inspectrice fait venir une oie supplémentaire.

Le compteur `EXHIBITS DESTROYED` s’affiche sous la fiche de notation. Cette interaction n’utilise aucun hook souris : le clic est lu depuis la position du pointeur, puisque l’overlay laisse passer les clics.

### 3:35 — Presse-papiers certifié

Un gag visuel simule un verrouillage du collage sans installer de hook et sans lire ou modifier le presse-papiers :

> +10,000 AURA

Indépendamment de toute tentative de collage, l’oie tamponne aussi un énorme badge `CLIPBOARD CERTIFIED: +10,000 AURA` au milieu de l’écran, puis le traîne dans une zone réservée en bas à gauche. Le badge est rendu devant les images afin de rester lisible.

## Phase 3 — 3:55 à 5:15 : La note sur le mur

### 3:55 — Renforts

La charge dépasse une seule oie. Deux inspectrices supplémentaires arrivent — elles étaient déjà dehors — et la troupe continue ensuite de grandir jusqu’à 67 avant la clôture.

> This site needs more inspectors.  
> They were already outside.

**Aucune fausse fenêtre système n’est ouverte, à aucun moment de l’expérience.** Le décalage entre un faux `Task Manager` dessiné à la main et un vrai composant Windows se voit immédiatement et casse l’illusion ; l’escalade passe donc entièrement par le nombre d’oies, les pièces à conviction accrochées au bureau et la dégradation de l’affichage.

### 4:25 — La note est peinte

- Avant le premier trait, les oies **débarrassent le mur** : toute image posée sur l’emplacement du tag est reprise dans un bec et raccrochée ailleurs.
- Oie 1 : peint un `67` géant en rose néon, **trait par trait, en direct**. Le tag occupe environ la moitié de la hauteur de l’écran. L’oie marche le long du tracé en suivant la buse, la peinture apparaît derrière elle, des coulures se mettent à couler sous les traits déjà posés et l’overspray s’accumule autour. Le tag est peint en dix-huit secondes.
- Oies 2 et 3 : se rangent **de part et d’autre du tag**, face à lui, et se balancent en rythme au lieu de traverser le tracé.

Le tag est dessiné après les images et sous une passe d’encre presque opaque : quel que soit l’écran, le fond du bureau ou le filtre de couleur en cours, le `67` garde une silhouette nette et ne peut pas être recouvert par une pile de photos.

### 4:55 — Droit de recours

Une boîte de dialogue apparaît au centre :

> Are you a Sigma Chad or a NPC?

Boutons : `[ SIGMA CHAD ]` / `[ NPC ]`

Le bouton `[ SIGMA CHAD ]` s’éloigne de 100 pixels dès que le pointeur le survole. Si l’utilisateur ne fait rien, il commence à fuir tout seul toutes les deux secondes tandis que l’oie le poursuit. Après huit secondes sans réponse, une oie appuie elle-même sur `[ NPC ]` et affiche :

> AFK detected. NPC confirmed.

## Phase 4 — 5:15 à 6:50 : Le site se dégrade

### 5:15 — Secousses et tempête par vagues

L’overlay principal déclenche une secousse rapide de 4 pixels toutes les deux secondes, ce qui garde le rythme lisible même si la VM ne restitue pas le son.

En mode complet, de courtes alertes Windows (`SystemHand`, `SystemQuestion`, `SystemExclamation` ou `SystemAsterisk`) ponctuent aussi le chaos. Elles sont asynchrones, espacées par l'horloge réelle et ne modifient jamais le volume système. `--mute` les désactive.

Au même instant, le curseur entre dans une tempête **cyclique**. Chaque cycle de 7,5 secondes comporte une vague où une cible mouvante et un tremblement haute fréquence tirent le pointeur, puis une fenêtre où le pointeur est **entièrement rendu à l’utilisateur** — pas un pixel ne lui est pris. La part saisie monte d’environ un tiers à environ deux tiers du cycle, et la violence de chaque vague monte avec elle, mais il reste toujours au moins deux secondes et demie de contrôle complet entre deux vagues. Le HUD affiche `CURSOR: SEIZED` ou `CURSOR: YOURS` pour que l’alternance soit lisible plutôt que subie. La sortie d’urgence par `Esc` reste disponible en permanence.

Si le menu Démarrer ou la recherche Windows recouvre le tag et les oies, GooseRot adresse `Esc` uniquement à cette surface shell et la masque. Aucun raccourci clavier global n’est synthétisé.

### Tout du long — Dégradation de l’affichage

Une intensité de glitch monte avec la timeline (proche de 0 au début, 0,18 à `3:55`, 0,26 à `4:25`, 0,42 à `5:15`, 0,58 à `5:45`, puis une montée **continue** de 0,70 à 1,00 entre `6:15` et la fin) et chaque constat ajoute un pic qui retombe. Le dernier tiers ne compte que sur l’affichage lui-même : il n’y a aucune fenêtre à empiler. Elle pilote, toujours à l’intérieur de l’overlay :

- des bandes de déchirure horizontales décalées en rose et cyan ;
- des blocs de framebuffer « corrompus » ;
- des scanlines CRT qui défilent ;
- une aberration chromatique rouge/cyan sur les textes lourds ;
- des curseurs fantômes qui se multiplient autour du vrai pointeur ;
- des bandes de perte de signal qui défilent et cisaillent l’image au-delà de 0,62 ;
- de brefs flashs plein écran, cadencés par l’horloge réelle et de plus en plus probables à mesure que l’intensité monte.

### Tout du long — Avis d’inspection

Des avis glissent depuis le coin bas-droit, peints dans l’overlay et jamais envoyés au centre de notifications de Windows. Ils portent tous le même en-tête, `AURA INSPECTION`, et ne se font jamais passer pour un composant du système :

> AURA INSPECTION — Case 67 opened for this desktop. An inspector is already on site.
> AURA INSPECTION — Scorecard attached to case 67. You may now watch it get worse.
> AURA INSPECTION — Case 67 verdict recorded. The site is scheduled for closure.

### 5:45 — Filtre de couleur

Un overlay transparent plein écran, à 15 % d’opacité, alterne la teinte du bureau entre vert Matrix et rose néon, en synchronisation avec les secousses.

### 6:15 — Le verdict

La troupe, déjà bien plus nombreuse que les trois oies initiales, encercle le pointeur pendant que le verdict s’inscrit en travers de l’écran :

> VERDICT: NON-COMPLIANT. SITE CONDEMNED.

Le dossier reçoit sa conclusion, et plus rien de nouveau n’est ouvert sur le site : aucune application n’est lancée après ce point. La dernière minute et quart ne repose que sur la dégradation de l’affichage.

## Phase 5 — 6:50 à 7:30 : La clôture

### 6:50 — Compte à rebours

Un grand compte à rebours rouge apparaît en haut de l’écran :

> 00:40

### 7:10 — Les inspecteurs se déploient

Le nombre d’oies accélère jusqu’à 67. Elles quittent leur spirale autour du pointeur et se répartissent sur une grille mouvante qui remplit tout l’écran.

### 7:18 — La signature

L’inspectrice descend vers un bouton virtuel `[ SIGN HERE ]` posé en bas de l’écran. Il apparaît **juste avant** que l’obturateur ne commence à se refermer, de sorte que l’écran se referme visiblement dessus.

### 7:30 — Le dossier est clos

La fin est un obturateur qui se ferme, pas une explosion.

À partir de `7:21`, la partie visible du bureau se réduit à **un cercle qui rétrécit** : tout ce qui est en dehors passe au noir, et le bord du cercle s’éclaire. Pendant la même descente, **l’exposition est poussée** : ce qu’il reste dans le cercle blanchit de plus en plus, jusqu’à être complètement brûlé au moment où le cercle se referme. Neuf secondes de fermeture progressive, sans coupure.

Puis le crash, en quatre temps :

1. **0,00–0,55 s** — l’image surexposée est écrasée en une ligne horizontale puis pincée en un point, comme un tube cathodique qu’on éteint ;
2. **0,55–1,60 s** — plus rien. Noir complet, assez long pour être inconfortable ;
3. **1,60–4,20 s** — une oie revient, en marchant, depuis le bord gauche ;
4. **4,20 s** — elle parle.

> CASE 67 CLOSED.  
> THE SITE HAS BEEN CONDEMNED.  
> HAVE A NICE DAY.

Le nettoyage et la restauration ont lieu **avant** le crash. La conclusion dure 9,5 secondes réelles, même avec une timeline accélérée. Sans les flashs (`--no-flashes`, `--reduced-motion` ou le profil réduit), la surexposition est plafonnée et le crash reste une extinction douce.

L’appui de deux secondes sur `Esc` est une sortie d’urgence uniquement en `safe`. Il ne doit pas interrompre `normal` ou `lab`.

Le code actuel n’est pas encore aligné avec ce scénario cible : ses effets `lab` restent non destructeurs et l’appui long sur `Esc` y est encore traité.

---

## Pistes créatives ajoutées

Ces idées ne remplacent pas le scénario de base ; elles forment une réserve pour les prochaines versions.

### Un fil rouge : la fiche de notation

La fiche **n’existe pas au lancement**. Elle apparaît à `1:40`, au moment où l’inspectrice ouvre sa notation, et elle tombe en place avec un léger dépassement : c’est un objet que l’oie a apporté, pas un élément d’interface du programme. Elle évolue ensuite à chaque constat, avec des valeurs volontairement absurdes :

- relevé de base : `−10 000`
- fenêtre réalignée : `−67`
- recours rejeté : `−1 000 000`
- presse-papiers certifié sans lecture : `+10 000`
- pièce à conviction détruite par l’occupant : `−6 700`
- clic sur le bouton Démarrer scellé : `−67`

Le compteur `EXHIBITS DESTROYED` se place juste dessous dès la première destruction.

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
- ne bloquer le bouton Démarrer qu’avec une fenêtre appartenant à GooseRot et, après opt-in complet, un hook limité aux deux touches Windows ; détruire les deux gardes au nettoyage, sans modification du shell et sans jamais toucher `Ctrl+Shift+Échap`, `Alt+Tab` ni `Échap` ;
- afficher avant `lab` un avertissement bloquant annonçant la corruption des fichiers, du Registre et du démarrage ;
- interdire `lab` hors d’une VM isolée et jetable ;
- ne jamais lire ou modifier le presse-papiers système ;
- fermer uniquement le dossier d’inspection lancé par GooseRot ;
- n’ouvrir aucune fausse fenêtre système, aucun faux cadre « Ne répond pas » et aucune notification se faisant passer pour un composant Windows : tout ce qui s’affiche s’annonce comme appartenant à l’inspection ;
- ne jamais faire apparaître une pièce à conviction sans qu’une oie l’ait rapportée, ni ouvrir le dossier sans que l’oie l’ait tamponné ;
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
