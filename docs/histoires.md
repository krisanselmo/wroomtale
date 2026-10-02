# Histoires interactives

Un tag peut lancer une histoire à embranchements au lieu d'un dossier : chaque
nœud est un extrait audio, et à la fin de l'extrait la boîte attend un choix sur
l'un des trois boutons. Le bandeau allume les zones qui mènent quelque part.
L'arbre est parcouru localement, sans réseau.

```
bind <uid> /histoires/foret     histoire (reconnue à son histoire.txt)
bind <uid> /bruitages/train     dossier de pistes
bind <uid> builtin:boot         jingle embarqué
```

Pointer le dossier qui contient les fichiers : `/histoires` ou `/bruitages` ne
donnent rien, les sous-dossiers ne sont pas parcourus.

## Format sur la carte

Un dossier par histoire, contenant les MP3 et un `histoire.txt` :

```
# identifiant  fichier          [prev=cible] [play=cible] [next=cible]
depart   01-foret.mp3    prev=grotte  next=riviere
grotte   02-grotte.mp3   play=tresor
riviere  03-riviere.mp3  prev=ours
tresor   04-tresor.mp3
ours     05-ours.mp3
```

- Le premier nœud est le départ.
- Un nœud sans choix est une fin.
- Les chemins audio sont relatifs au dossier de l'histoire.
- Identifiants : 15 caractères au plus. 32 nœuds au plus (`Story::MAX_NODES`).
- `#` ouvre un commentaire.

### Drapeaux

Un nœud peut accorder un acquis, un choix peut en exiger un :

```
grotte  02-grotte.mp3  set=cle  play=sortie
porte   03-porte.mp3   prev=cle?ouvre  next=repart
```

`prev=cle?ouvre` n'est proposé — ni annoncé par les LED — qu'une fois `cle`
obtenu. 16 acquis au plus (`Story::MAX_FLAGS`).

### Vérifier avant de copier

```bash
python3 medias/tooling/story_check.py /chemin/vers/histoire
```

Signale les fichiers manquants, les choix pointant vers un nœud inexistant, les
nœuds inatteignables et les histoires sans fin. Sur la carte, ces erreurs
n'apparaissent qu'au journal série, au démarrage.

## Écrire une histoire

Le format source est une ligne par nœud, texte compris :

```
depart | Tu marches dans une forêt. Appuie sur le rouge pour... | prev=riviere next=grotte
grotte | Ta main trouve une clé de fer.                        | set=cle play=depart
porte  | Une serrure ronde brille au milieu.                   | prev=cle?tresor next=riviere
tresor | La porte s'ouvre sur un coffre. Bravo.                |
```

```bash
python3 medias/tooling/story_build.py medias/src/histoires/foret.src
```

Le script synthétise chaque texte, encode en MP3 mono 22,05 kHz et écrit
`histoire.txt`. Sans destination, la sortie va dans `medias/dist/histoires/<nom>`.

### Dimensionnement

| | À viser |
|---|---|
| Longueur d'un nœud | 80 à 120 mots, soit 35 à 50 s |
| Nombre de nœuds | 8 à 15 |
| Narration totale | 4 à 8 minutes |

Repères : *Les Odyssées* (France Inter) tient 16 minutes par épisode, *Une
histoire et… Oli* environ 9.

Par nœud : un lieu, une sensation physique, un enjeu, puis le choix. Les
couleurs des boutons sont énoncées dans le texte, jamais sous-entendues.

### Comparer des voix

```bash
python3 medias/tooling/studio.py          # http://localhost:8081
```

Rejoue une `.src` nœud par nœud et rend chaque nœud sous plusieurs jeux de
réglages de synthèse pour les comparer. Piper seul par défaut ; `--google` est
requis pour qu'une variante puisse appeler une API facturée.

## Synthèse vocale

### Piper (défaut)

Synthèse neuronale locale : pas de service, pas de clé, pas de réseau. Sort du
22,05 kHz mono, le format cible, environ dix fois plus vite que le temps réel.

Le modèle n'est pas dans le dépôt (63 Mo) :

```bash
python3 -m venv .venv && .venv/bin/pip install piper-tts
V=https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/siwis/medium
curl -L -o medias/src/voix/fr.onnx      $V/fr_FR-siwis-medium.onnx
curl -L -o medias/src/voix/fr.onnx.json $V/fr_FR-siwis-medium.onnx.json
```

D'autres voix françaises existent au même endroit (`upmc`, `gilles`, `tom`) : il
suffit de remplacer `medias/src/voix/fr.onnx`.

### Google Cloud TTS

```bash
export GOOGLE_TTS_API_KEY="..."
python3 medias/tooling/story_build.py medias/src/histoires/phare.src --voice google
python3 medias/tooling/story_build.py medias/src/histoires/phare.src --voice google:fr-FR-Neural2-D
```

L'API renvoie directement du MP3 à 22,05 kHz, sans transcodage. Le débit est
fixé à 0,92. La hauteur n'est envoyée qu'aux familles qui la gèrent : ni Studio
ni Chirp ne la prennent.

Voix par défaut `fr-FR-Wavenet-G`, couverte par le palier gratuit de quatre
millions de caractères par mois. Une histoire fait environ 4 600 caractères.
Au-delà : 4 $/M en WaveNet, 30 $/M en Chirp 3 HD, 160 $/M en Studio.

Google répond à un nom de voix inconnu par une voix par défaut, sans erreur :
`story_build.py` interroge donc `voices.list` (gratuit) et s'arrête avant le
premier caractère facturé si le nom n'existe pas.

La clé se lit dans l'environnement ou dans `.env`, jamais sur la ligne de
commande où elle resterait dans l'historique du shell. `.env` est ignoré par
git.

## Contenu d'ailleurs

```bash
python3 medias/tooling/podcast_fetch.py odyssees
python3 medias/tooling/podcast_fetch.py odyssees 3 medias/dist/podcasts/odyssees/tresor
```

Récupère un épisode d'un flux public et le transcode en MP3 mono — les flux
Radio France servent du `m4a`, que le firmware ne décode pas.

Raccourcis : `pomme` (Pomme d'Api, 3-7 ans), `encore` (Encore une histoire),
`oli` et `bestioles` (5-7 ans), `odyssees` (7-12 ans). Toute autre URL de flux
fonctionne aussi.

Écoute personnelle uniquement : ces podcasts restent la propriété de leur éditeur.
Rien de ce que produit ce script n'a sa place dans le dépôt.

```bash
python3 medias/tooling/sons_fetch.py ferme
```

Collecte un échantillon thématique de bruitages, un dossier par source. Chaque
dossier reçoit un `SOURCES.txt` donnant titre, page et licence de chaque
fichier : un extrait CC BY-SA embarqué en flash contamine le firmware.

## Sans carte SD

```
story demo
```

Quatre nœuds qui jouent tous le jingle embarqué : de quoi éprouver les
boutons et le bandeau sans carte. `story` affiche l'état,
`story stop` sort.
