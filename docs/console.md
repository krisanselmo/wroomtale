# Console série et portail

Moniteur série à 115200. Chaque commande se valide par Entrée.

```
p  play/pause     n  suivant      b  précédent    s  stop
+  volume +       -  volume -     o  son de boot  e  son d'erreur
r  redémarrer     ?  aide et état     t  tonalité 1 kHz (3 s)
level             crête audio mesurée sur une seconde

tags                       liste les associations
bind <uid> <cible>         associe un tag (dossier ou builtin:)
tag <uid>                  simule la présentation d'un tag
play <cible>               joue sans passer par un tag
story demo                 histoire de démonstration sur le jingle embarqué
story / story stop         état du moteur d'histoire / sortie

batt                       tension et charge de la cellule
batt cal <mV>              recale la lecture sur un multimètre (NVS)
batt clear                 oublie la correction
battlog [flush|clear]      journal de la batterie sur la carte SD
journal [flush|clear]      journal des événements, numéro de démarrage, horloge

volmax <0-21>              plafond de volume, boutons compris (NVS)
volmin <0-21>              plancher de volume, pour le réglage (NVS)
selftest on|off            auto-test audio au démarrage (NVS)
sfx                        liste les sons de retour, (off) pour les coupés
sfx <nom> on|off           coupe ou rétablit un son (NVS)
config on|off              démarrer directement en mode config (NVS, off)
idle                       délai de mise en veille
idle <0-240>               minutes d'inactivité avant la veille, 0 = jamais (NVS, 10)
sleep                      veille immédiate, PLAY réveille

ssid <nom>                 réseau WiFi domestique
pass <secret>              mot de passe (jamais journalisé)
appass <secret>            mot de passe du point d'accès, 8 à 63 caractères
appass clear               revient au mot de passe par défaut
wifi                       SSID enregistré et état du point d'accès
wifi clear                 oublie les identifiants, point d'accès compris
```

## Sons de retour

Synthétisés à la volée (sinus plus enveloppe), aucun fichier ni octet de flash.

| Action | Son |
|---|---|
| Démarrage | arpège do–mi–sol |
| Play/pause | deux notes montantes |
| Stop (appui long) | deux notes descendantes |
| Suivant / précédent | blip montant / descendant |
| Volume +/- | note aiguë / grave |
| Erreur (pas de carte) | double note grave |
| Carte reconnue | deux notes montantes, avant la lecture |
| Prête pour une carte | quarte montante, dès que le lecteur écoute |
| Batterie à plat | quatre notes descendantes, avant la veille |

Les tags sont lus dès la fin de la fenêtre console, environ une seconde après
la mise sous tension. En mode config, ils le sont aussi pendant la connexion
WiFi et la lecture de la carte SD. Une carte posée pendant le jingle le coupe.
Le son « prête » suit le jingle ; sans lecteur RFID, il ne sonne pas.

Chaque son se coupe dans le portail (carte *Sons*) ou par `sfx <nom> off`, et
le réglage survit au redémarrage. Noms : `boot`, `ready`, `tag`, `error`,
`play`, `stop`, `next`, `prev`, `volup`, `voldown`, `lowbatt`. Un son rétabli depuis le
portail se joue une fois. La tonalité de l'auto-test ne se coupe pas.

## Cibles d'un tag

Un tag peut porter trois choses :

| Cible | Exemple | Comportement |
|---|---|---|
| un dossier | `/bruitages/train` | playlist : les 43 pistes s'enchaînent, suivant/précédent les parcourent |
| **un fichier** | `/bruitages/train/13-locomotive-train-horn-3.mp3` | **un seul son**, hors playlist : il joue et s'arrête |
| le jingle embarqué | `builtin:boot` | le clip en flash |

Un dossier contenant un `histoire.txt` est reconnu comme histoire, quelle que
soit la façon dont on l'associe.

Dans le portail, la ligne d'un dossier porte les deux gestes : **Associer**
prend le dossier entier en playlist, **Explorer ›** entre dedans pour associer
un son en particulier. Un dossier s'ouvre dès qu'il contient des pistes, même
sans sous-dossier — sinon `/bruitages/avion` et ses 37 fichiers ne pourraient être
associés qu'en bloc. À la console, `bind <uid> <chemin>` accepte les deux
formes.

## Schémas de boutons

Deux lectures des trois boutons, choisies à la compilation (`BTN_SCHEME` dans
`firmware/include/Features.h`). La console annonce celle qui tourne au
démarrage et dans l'aide.

| | classique (défaut) | volume d'abord |
|---|---|---|
| Rouge, clic | piste précédente | volume − |
| Rouge, maintenu | volume − en continu | volume − en continu |
| Vert, clic | play/pause | play/pause |
| Vert, double clic | — | piste suivante |
| Vert, maintenu | stop | stop |
| Bleu, clic | piste suivante | volume + |
| Bleu, maintenu | volume + en continu | volume + en continu |

```bash
pio run -d firmware -e wroomtale-volume -t upload
```

**Une histoire chargée reprend la main sur les trois couleurs**, quel que soit
le schéma : le même bouton choisit la même branche. Pendant qu'un segment se
joue, rouge et bleu ne font rien — l'histoire se répond quand elle le demande —
et seul le vert garde pause/reprise. Les gestes maintenus restent au schéma,
donc le volume est toujours réglable au milieu d'un conte.

Le second schéma n'a pas de piste précédente : les deux extrémités sont prises
par le volume. Le double clic coûte 280 ms d'attente au clic simple du vert —
il faut être sûr qu'un second ne vient pas. Seul le bouton qui déclare un
double le paie ; en schéma classique la détection n'est jamais armée et le clic
répond tout de suite.

Maintenir le bouton de volume le fait défiler en continu, un cran toutes les
120 ms — environ 3 s d'un bout à l'autre. Le blip ne sonne qu'au premier cran :
pendant la rampe, c'est la musique elle-même qui renseigne.

Deux bornes gardées en NVS arrêtent la course, réglables au portail comme à la
console. `volmax` protège les oreilles. `volmin` sert au réglage : le gain est
au carré, donc les premiers crans sont presque muets, et à quel point dépend du
haut-parleur — le monter jusqu'à ce que le cran le plus bas reste audible. Le
plancher ne passe jamais le plafond ; baisser le plafond dessous le tire avec
lui.

## Mode config

Entrée : PLAY maintenu 2 s au démarrage, ou une touche console pendant les 600
premières ms du boot. `config on` le rend persistant.

Coupé par défaut : la boîte démarre en lecture, radio éteinte. La radio coûte
environ trois fois le courant de repos, et le portail ne rend jamais la main —
tant qu'il était persistant, l'arpège de démarrage n'était jamais joué.
`config on` ne sert donc qu'au temps d'une session de réglage.

Un reset ne perd aucun réglage — SSID, mots de passe, volume, correction de
batterie et associations de tags vivent en NVS.

### Point d'accès ou réseau domestique

Si un SSID est enregistré, le mode config rejoint ce réseau au lieu de créer son
point d'accès. L'adresse est journalisée au démarrage ; la carte s'annonce aussi
en mDNS sous `wroomtale.local` (`MDNS_NAME` dans `firmware/include/Config.h`).

Sans identifiants, ou après 15 s d'échec, l'AP WPA2 `WroomTale` prend le relais.
Le mot de passe par défaut `wroomtale` est dans le dépôt ; `appass <secret>` en
enregistre un autre en NVS. Sans cela, toute personne à portée peut réécrire les
associations de tags.

### Tableau de bord

Affiche : modèle de puce, flash, PSRAM, tas libre et plus gros bloc contigu,
présence du PN532 (avec sa version de firmware) et de la carte SD (type, taille,
occupation). L'ampli I2S est listé non détectable, le bus étant unidirectionnel.

Les trois boutons ont un indicateur allumé pendant l'appui et marqué en vert
après un premier appui — un appui bref passerait sinon entre deux
interrogations de la page.

La liste d'association parcourt la carte jusqu'à trois niveaux et ne retient que
les cibles jouables — dossier contenant des MP3, ou dossier contenant un
manifeste — avec leur nature et leur nombre de pistes. Le jingle embarqué y
figure. Le parcours est fait une fois au démarrage du portail ; **Relire la
carte** le refait.

### Fichiers

La carte *Fichiers* parcourt la carte SD un dossier à la fois : envoyer des
fichiers dans le dossier ouvert, en télécharger un, créer un dossier, renommer,
supprimer (un dossier part avec tout son contenu, jusqu'à six niveaux). Un
envoi arrête la
lecture : le décodeur et l'écriture se partageraient le bus SPI. Il s'écrit
dans `nom.part`, renommé à la fin ; une connexion coupée ne laisse pas de MP3
tronqué. La liste des cibles est relue après chaque modification.
Une carte associée à un dossier renommé ou supprimé pointe toujours vers
l'ancien chemin et sonne l'erreur : la réassocier depuis la bibliothèque.

Le débit est celui du WiFi de l'ESP32 : pratique pour ajouter une histoire,
lent pour remplir une carte, qui se copie plus vite depuis un ordinateur.

| Route | Rôle |
|---|---|
| `GET /api/files?dir=` | entrées du dossier, 200 au plus |
| `GET /api/download?path=` | le fichier, en pièce jointe |
| `POST /api/upload?dir=` | envoi multipart, un ou plusieurs fichiers |
| `POST /api/mkdir?path=` | nouveau dossier |
| `POST /api/rename?from=&to=` | renomme ou déplace |
| `POST /api/delete?path=` | supprime, récursif, jamais `/` |

Les chemins commencent par `/` et refusent `..` et `\` (400). Sans carte SD,
503 ; nom déjà pris, 409.

La page vit dans `firmware/web/index.html` ; `firmware/src/PortalPage.h` en est
la version gzip en PROGMEM, régénérée avant chaque build.

### Mock sans ESP32

```bash
python3 firmware/web/mock.py     # http://localhost:8080
```

Sert les mêmes routes et le même JSON que `ConfigPortal.cpp`, avec faux
matériel : carte SD absente, batterie à plat, bouton coincé. Taper une lettre
change l'état ; `h` liste les commandes.

Une route ajoutée au portail se double d'une route ajoutée au mock.

## Veille

En lecture, la boîte passe en deep sleep après 10 minutes sans bouton, sans
carte présentée et sans son joué. Une pause ou une histoire qui attend un choix
comptent comme de l'inactivité. Délai réglable par `idle <min>` ou dans la tuile
*Veille* du portail, 0 pour jamais ; il vit en NVS. Le mode config garde son
propre délai de 5 minutes sans client, suivi d'un redémarrage en lecture. En
mode config persistant, ce redémarrage ramènerait au portail : la boîte
s'endort à la place.

Sous 3,3 V pendant 20 s (`BATT_CRITICAL_MV`), la lecture s'arrête, le son
`lowbatt` et trois pulsations rouges préviennent, puis la boîte s'endort quel que
soit le délai. Une tension qui remonte de 30 mV pendant ces 20 s passe pour un
chargeur branché, et le décompte repart. Au réveil par PLAY, une cellule
toujours sous le seuil la rendort aussitôt, avant l'audio ; une mise sous
tension (interrupteur, USB, chargeur) démarre quelle que soit la tension. Le
mode config n'applique pas ce seuil : la boîte n'y distingue pas la cellule
de l'USB qui l'alimente, et son propre délai l'endort. Sans pont diviseur,
aucune tension n'est lue et seul le délai s'applique.

Un appui sur PLAY réveille la boîte : c'est un démarrage complet, jingle
compris. Poser une carte ne la réveille pas, l'IRQ du PN532 n'étant pas câblée.

## Auto-test audio

`selftest on` : au démarrage, tonalité continue de 1 kHz répétée sans limite au
lieu de l'arpège, interrompue par un appui bouton ou une touche console. Actif
dans les deux modes, portail compris. Réglage en NVS, `selftest off` pour
revenir à l'arpège.

## Journaux

Deux fichiers CSV sous `/logs/` sur la carte SD, téléchargeables depuis la carte
*Fichiers* du portail. Chaque ligne commence par les mêmes colonnes :

| Colonne | Contenu |
|---|---|
| `boot` | numéro de démarrage, compté en NVS |
| `uptime_s` | secondes depuis ce démarrage |
| `time` | heure UTC, vide quand la boîte ne la connaît pas |

`batt.csv` ajoute `raw_mv`, `filtered_mv`, `percent` et `state` (`idle`,
`playing`, `story_wait`, `wifi_portal`), une ligne toutes les 30 s.

`events.csv` ajoute `event` et `detail` :

| Événement | Détail |
|---|---|
| `boot` | cause : `power-on`, `wake`, `restart`, `reset pin`, `brownout`, `panic`, `watchdog` |
| `mode` | `play` ou `config` |
| `button` | `prev`, `play`, `next`, suivi de `held` ou `double` ; une rampe de volume compte une fois |
| `tag` | UID et cible, ou `unknown` |
| `play`, `track`, `stop` | cible demandée, piste qui démarre, fin de lecture |
| `upload`, `download`, `mkdir`, `rename`, `delete` | chemin |
| `portal` | réseau rejoint, clients du point d'accès, délai écoulé |
| `clock` | source de l'heure : `ntp`, `web`, ou `kept` au réveil |
| `battery` | passage sous 3,4 V |
| `sleep` | raison de la mise en veille |

L'heure vient de NTP quand le portail rejoint un réseau, sinon du navigateur
qui ouvre le tableau de bord (`POST /api/clock?t=`). La veille la conserve avec
la dérive de l'oscillateur RTC, de l'ordre de 2 % ; un redémarrage ou une
coupure à l'interrupteur la perd, et la colonne reste vide jusqu'à la
prochaine synchronisation.

Les lignes attendent en RAM et s'écrivent quand la lecture laisse le bus SPI
libre, au plus tard toutes les 5 minutes. Un fichier écrit avec d'autres
colonnes est renommé `nom-old.csv` au premier ajout.
