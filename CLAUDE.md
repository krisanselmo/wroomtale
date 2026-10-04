# WroomTale

Boîte à histoires interactives sur ESP32-WROOM-32 : 4 Mo de flash, **pas de
PSRAM**. Cette contrainte décide du reste — le choix d'ESP8266Audio, l'absence
d'OTA, la séparation des modes. Avant de proposer une bibliothèque ou une
fonction, vérifier qu'elle tient sans PSRAM.

## Compiler et flasher

```bash
cd firmware                      # platformio.ini vit la
pio run -t upload                # la boite : un seul build fait tout
pio run -e wroomtale-volume -t upload   # boutons axes sur le volume
pio run -e bt -t upload          # spike A2DP, hors du build par defaut
```

Les tests tournent sur le PC, jamais sur la carte : la logique sans matériel
(cibles, graphe d'histoire, schéma de boutons, JSON) et les outils Python.

```bash
pio test -e native -e native-volume          # depuis firmware/
python3 -m unittest discover -s medias/tooling
```

Un seul environnement, `wroomtale`, sans profils d'usage : ils
économiseraient 132 Ko sur une partition de 3,87 Mo remplie à 36 %. Éteindre la
radio est une décision d'exécution, pas de compilation — une pile WiFi
inutilisée coûte du flash, jamais du courant.

Les volets sont déclarés dans `firmware/include/Features.h` pour dégraisser
une carte à la main. Un volet coupé voit sa source retirée du build et son
en-tête fournit des fonctions vides en ligne : les appels restent inchangés,
aucun `#ifdef` n'encombre `main.cpp`. Aucun profil livré ne s'en sert.

FastLED est figé à la version exacte : 3.10.5 refuse de compiler contre le
pilote RMT4 que la boîte demande, et un `libdeps` neuf le prend en silence.

La boîte démarre en lecture, radio éteinte. Pour entrer en config : PLAY
maintenu 2 s au démarrage, une touche sur la console dans les 600 ms, ou le
bouton du panneau qui rend le mode config persistant.

## Vérifier sur la carte

**Un firmware qui compile ne prouve rien.** Tout changement de comportement se
vérifie sur le matériel avant d'être annoncé fait.

Se fier au **code de retour**, jamais à un motif cherché dans la sortie : le
proxy rtk la filtre, et un `grep` qui ne trouve rien ressemble à un succès.

```bash
pio run -d firmware -t upload > /tmp/up.log 2>&1; echo $?
```

Lire la console avec pyserial : reset par DTR/RTS, puis capture. Vider le
tampon avant de mesurer — un octet en attente interrompt l'auto-test et fausse
le relevé.

Un chiffre étonnamment bon mérite autant de méfiance qu'une erreur : « 2 cibles
trouvées en 6 ms » signalait que le parcours de la carte ne démarrait pas.

## Page de configuration

La page servie par le portail vit dans `firmware/web/index.html`, ses textes
dans `firmware/web/strings.json` (`fr` et `en` pour chaque clé). Un texte
visible s'écrit `{{clé}}` dans le HTML ou `T.clé` dans le script, jamais en dur.
`firmware/src/PortalPage.h` en est la version gzip en PROGMEM dans la langue du
build (`custom_lang`, ou `WROOMTALE_LANG`), régénérée par `web/build_page.py`
avant chaque build et ignorée par git : ne pas l'éditer.

`firmware/web/mock.py` est un outil de mise au point à garder, pas un
échafaudage : il rejoue le portail sans ESP branché, pour retoucher l'interface,
reproduire un bug ou voir un cas que le matériel ne donne pas sous la main
(carte SD absente, batterie à plat, bouton coincé).

```bash
python3 firmware/web/mock.py     # http://localhost:8080, faux matériel
```

Il sert les mêmes routes et le même JSON que `ConfigPortal.cpp` — c'est tout son
intérêt, donc **une route ajoutée au portail se double d'une route ajoutée au
mock**. Taper une lettre dans le terminal bouge la carte (tag présenté, bouton
enfoncé, carte SD retirée, batterie qui tombe, dossier lancé). `h` liste les
commandes.

## Synthèse vocale

**Ne jamais lancer `medias/tooling/story_build.py` sans demander.** Les appels Google sont
facturés, et les voix Studio coûtent environ seize fois le tarif WaveNet.
Annoncer le nombre de caractères et le coût estimé, puis attendre le feu vert.

## Rangement

Trois familles, une par dossier de premier niveau :

| Dossier | Contenu |
|---|---|
| `firmware/` | ce qui tourne sur l'ESP32 : `src/`, `include/`, `platformio.ini` |
| `hardware/` | le montage et son schéma : `wiring.html`, les générateurs, les PNG |
| `medias/` | le contenu de la carte SD et les outils qui le fabriquent |

Sous `medias/`, `src/` est ce qu'on écrit (les `.src` des histoires, suivis par
git ; le modèle de voix, ignoré) et `dist/` le miroir exact de la carte SD :
`histoires/`, `podcasts/`, `bruitages/`, ignoré par git car régénérable ou
téléchargé. Copier `dist/` à la racine de la carte suffit.

## Secrets

Clés d'API dans `.env`. Le mot de passe WiFi vit en NVS sur la carte, jamais
dans le dépôt ni dans la conversation.

## Audio embarqué

`firmware/src/sounds/*.h` sont générés, ne pas les éditer : le jingle de
démarrage sort de `medias/tooling/jingle.py`. Un extrait CC BY-SA embarqué en
flash imposerait sa licence au firmware entier.

## README

**Le README n'est pas un CHANGELOG.** Il dit ce qu'est la boîte aujourd'hui,
jamais ce qui a changé ni pourquoi : pas de « a été retiré », pas de « désormais »,
pas de chiffres d'arbitrage. Le détail va dans `docs/`, l'historique dans les
commits et les PR.

Une PR ne touche le README que si elle y rend quelque chose faux (une commande
qui n'existe plus, un câblage déplacé). Une option, un réglage ou une commande
de plus ne s'y ajoute pas : sa place est dans `docs/`.

Rien de spécifique à une installation : pas d'adresse IP, pas de réseau.

## Pull requests

Branche : `type/sujet-court`.
Titre : `type: ce que ça change`, 60 caractères au plus, sans point final.

Types : `feat`, `fix`, `refactor`, `perf`, `chore`, `docs`.

```
feat: play a story from a folder holding a manifest
fix: a folder plays once instead of looping forever
chore: cut comments that say nothing the code does not
```

En anglais, comme les commits, les commentaires et la description du dépôt.
Le README existe en deux versions, `README.md` en anglais et `README.fr.md` en
français : toucher l'un, c'est toucher l'autre. `docs/` et le contenu des
histoires restent en français.

Le titre dit **ce qui change**, pas le fichier touché. `fix: Player.cpp` ne
renseigne personne.

Le corps dit **pourquoi**, et **ce qui a été vérifié sur la carte**. Donner les
mesures quand il y en a : tailles de flash, tas libre, durées.

Une PR, un sujet. Empiler avec `--base <branche précédente>` plutôt qu'élargir.

Jamais de push direct sur `master`.
