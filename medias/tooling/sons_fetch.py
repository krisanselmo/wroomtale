#!/usr/bin/env python3
"""Collect a themed sample of sound effects, one folder per source.

Rerunnable: a file already on disk is never fetched twice, so the script can be
relaunched to widen a theme or to add a source. Each folder carries a
SOURCES.txt listing, for every file, its title, page and licence -- the box
embeds sounds in flash, and a CC BY-SA extract contaminates the firmware.

    python3 medias/tooling/sons_fetch.py                     # etat des lieux
    python3 medias/tooling/sons_fetch.py ferme               # echantillon complet
    python3 medias/tooling/sons_fetch.py ferme --source bigsoundbank
    python3 medias/tooling/sons_fetch.py ferme --par-requete 4

Le resultat atterrit dans medias/dist/bruitages/<theme>/<source>/, un sous-dossier par
source pour comparer. Le firmware ne descend pas dans les sous-dossiers : une
fois le choix fait, remonter les MP3 retenus dans medias/dist/bruitages/<theme>/.

Cle Freesound : FREESOUND_API_KEY, ou .env. Gratuite et immediate sur
https://freesound.org/apiv2/apply/ -- sans elle, la source est sautee.
"""
import argparse, html, json, os, pathlib, re, subprocess, unicodedata, urllib.error, urllib.parse, urllib.request

RATE, BITRATE = 22050, 64
UA = "Mozilla/5.0 (X11; Linux x86_64) wroomtale/sons_fetch"

MEDIAS = pathlib.Path(__file__).resolve().parent.parent
ROOT = MEDIAS.parent

# A query per sound wanted, in the source sites' language: they index in English.
THEMES = {
    "ferme": ["cow", "sheep", "goat", "pig", "rooster", "hen", "duck",
              "horse", "donkey", "tractor", "barn", "farm"],
    "chantier": ["excavator", "jackhammer", "drill", "hammer", "saw", "bulldozer",
                 "crane", "dump truck", "concrete mixer", "digger", "welding",
                 "construction site"],
    "pompiers": ["siren", "fire engine", "police car", "ambulance", "fire alarm",
                 "fire", "water hose", "klaxon", "walkie talkie", "emergency",
                 "rescue helicopter", "police whistle"],
    "train": ["steam train", "train whistle", "locomotive", "train station",
              "level crossing", "train horn", "subway", "railway", "train brakes",
              "diesel engine", "platform", "tram"],
    "avion": ["airplane", "jet", "propeller", "helicopter", "airport", "takeoff",
              "landing", "cockpit", "turbine", "glider", "hot air balloon", "radar"],
    "bateau": ["boat", "ship horn", "seagull", "waves", "harbour", "sailboat",
               "motorboat", "anchor", "foghorn", "rowing", "sonar", "port"],
    "zoo": ["lion", "elephant", "monkey", "wolf", "bear", "snake", "parrot",
            "penguin", "frog", "crocodile", "tiger", "whale"],
    "chateau": ["sword", "dragon", "drawbridge", "castle", "gallop", "armour",
                "fanfare", "catapult", "torch", "church bell", "arrow", "chains"],
    "espace": ["rocket", "spaceship", "laser", "robot", "countdown", "alien",
               "computer beep", "explosion", "teleport", "radio static",
               "engine hum", "airlock"],
    "maison": ["doorbell", "telephone", "vacuum cleaner", "microwave",
               "toilet flush", "alarm clock", "washing machine", "door knock",
               "keys", "clock", "dog", "cat"],
    "meteo": ["rain", "thunder", "wind", "storm", "stream", "birds", "forest",
              "snow", "waterfall", "crickets", "sea waves", "crackling fire"],
    "fete": ["applause", "party horn", "balloon pop", "children laughing",
             "music box", "jingle bells", "drum roll", "cheering", "birthday",
             "carousel", "toy", "kazoo"],
}

def slugify(title):
    # The card is vfat and the box logs paths over serial: ASCII only.
    ascii_only = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-zA-Z0-9]+", "-", ascii_only).strip("-").lower()[:40].strip("-")

def get(url, data=None, headers=None):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": UA, **(headers or {})})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()

def freesound_key():
    key = os.environ.get("FREESOUND_API_KEY")
    if key:
        return key
    env = ROOT / ".env"
    if env.is_file():
        for line in env.read_text().splitlines():
            if line.startswith("FREESOUND_API_KEY="):
                return line.split("=", 1)[1].strip().strip("\"'")
    return None

# --- Sources -----------------------------------------------------------------
# Each returns [{titre, page, auteur, licence, url}], the best `n` for a query.

def src_bigsoundbank(query, n):
    page = get(f"https://bigsoundbank.com/search?q={urllib.parse.quote(query)}").decode("utf-8", "replace")
    hits = re.findall(r"<div class='resultat_\d+'>.*?<a href='/([^']+?)-s(\d+)\.html'>(.*?)</a>", page, re.S)
    out = []
    for slug, num, title in hits[:n]:
        out.append({
            "titre": html.unescape(re.sub(r"<[^>]+>", "", title)).strip(),
            "page": f"https://bigsoundbank.com/{slug}-s{num}.html",
            "auteur": "Joseph SARDIN - BigSoundBank.com",
            "licence": "CC0 1.0 (domaine public)",
            "url": f"https://bigsoundbank.com/UPLOAD/mp3/{num}.mp3",
        })
    return out

def src_freesound(query, n, key):
    params = urllib.parse.urlencode({
        "query": query,
        "filter": 'license:"Creative Commons 0"',
        "fields": "id,name,username,url,license,previews,duration",
        "sort": "rating_desc",
        "page_size": n,
        "token": key,
    })
    data = json.loads(get(f"https://freesound.org/apiv2/search/text/?{params}"))
    return [{
        "titre": s["name"],
        "page": s["url"],
        "auteur": s["username"],
        "licence": "CC0 1.0 (domaine public)",
        "url": s["previews"]["preview-hq-mp3"],
    } for s in data.get("results", [])]

def src_bbc(query, n):
    body = json.dumps({"criteria": {"from": 0, "size": n, "query": query, "tags": None,
                                    "categories": None, "durations": None, "continents": None,
                                    "sortBy": None, "source": None, "recordist": None,
                                    "habitat": None}}).encode()
    data = json.loads(get("https://sound-effects-api.bbcrewind.co.uk/api/sfx/search",
                          data=body, headers={"Content-Type": "application/json"}))
    return [{
        "titre": s["description"],
        "page": f"https://sound-effects.bbcrewind.co.uk/search?q={urllib.parse.quote(query)}",
        "auteur": "BBC Archive",
        "licence": "RemArc -- personnel, educatif et recherche SEULEMENT, pas de commercial",
        "url": f"https://sound-effects-media.bbcrewind.co.uk/mp3/{s['id']}.mp3",
    } for s in data.get("results", [])]

def src_pixabay(folder):
    """Pixabay ne publie pas d'API audio et bloque les robots : URL a la main."""
    liste = folder / "urls.txt"
    if not liste.is_file():
        folder.mkdir(parents=True, exist_ok=True)
        liste.write_text(
            "# Une URL par ligne, copiee depuis le bouton Download de\n"
            "# https://pixabay.com/sound-effects/search/farm/\n"
            "# Format : <url mp3> | <titre> | <page pixabay>\n")
        return []
    out = []
    for line in liste.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        champs = [c.strip() for c in line.split("|")]
        url = champs[0]
        out.append({
            "titre": champs[1] if len(champs) > 1 else slugify(url.rsplit("/", 1)[-1]),
            "page": champs[2] if len(champs) > 2 else url,
            "auteur": "voir la page Pixabay",
            "licence": "Licence Contenu Pixabay -- libre, sans attribution",
            "url": url,
        })
    return out

# --- Ecriture ----------------------------------------------------------------

def transcode(src, dst):
    """Mono 22,05 kHz : ce que sort la carte, et six fois plus leger."""
    subprocess.run(
        ["gst-launch-1.0", "-q", "filesrc", f"location={src}", "!", "decodebin",
         "!", "audioconvert", "!", "audioresample", "!",
         f"audio/x-raw,rate={RATE},channels=1",
         "!", "lamemp3enc", f"bitrate={BITRATE}", "!", "filesink", f"location={dst}"],
        check=True, capture_output=True)

def write_sources(folder, source, fiches):
    lignes = [f"Source : {source}", ""]
    for nom in sorted(fiches):
        f = fiches[nom]
        lignes += [nom,
                   f"    titre    : {f['titre']}",
                   f"    auteur   : {f['auteur']}",
                   f"    licence  : {f['licence']}",
                   f"    page     : {f['page']}",
                   f"    fichier  : {f['url']}",
                   ""]
    (folder / "SOURCES.txt").write_text("\n".join(lignes))
    (folder / ".fetch.json").write_text(json.dumps(fiches, indent=2, ensure_ascii=False))

def collecte(folder, source, sons, query):
    folder.mkdir(parents=True, exist_ok=True)
    fiche_path = folder / ".fetch.json"
    fiches = json.loads(fiche_path.read_text()) if fiche_path.is_file() else {}
    brut = folder / ".brut"
    neufs = 0

    for s in sons:
        nom = f"{slugify(query)}-{slugify(s['titre'])}.mp3"
        dst = folder / nom
        if dst.is_file():                       # rejouable : rien a refaire
            fiches.setdefault(nom, s)
            continue
        try:
            brut.write_bytes(get(s["url"]))
            transcode(brut, dst)
        except (urllib.error.URLError, urllib.error.HTTPError, subprocess.CalledProcessError) as e:
            print(f"    ! {nom} : {e}")
            continue
        finally:
            brut.unlink(missing_ok=True)
        fiches[nom] = s
        neufs += 1
        print(f"    + {nom}  ({dst.stat().st_size // 1024} Ko)")

    write_sources(folder, source, fiches)
    return neufs

def etat(theme):
    base = MEDIAS / "dist" / "bruitages" / theme
    if not base.is_dir():
        return
    print(f"\nmedias/dist/bruitages/{theme}/")
    for folder in sorted(p for p in base.iterdir() if p.is_dir()):
        mp3 = list(folder.glob("*.mp3"))
        ko = sum(p.stat().st_size for p in mp3) // 1024
        print(f"  {folder.name:15s} {len(mp3):3d} sons   {ko:5d} Ko")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("theme", nargs="?", choices=sorted(THEMES))
    ap.add_argument("--source", choices=["bigsoundbank", "freesound", "bbc", "pixabay"],
                    action="append", help="par defaut : toutes")
    ap.add_argument("--par-requete", type=int, default=2, metavar="N",
                    help="sons retenus par mot-cle (2 par defaut)")
    args = ap.parse_args()

    if not args.theme:
        print(__doc__)
        print("Themes :", ", ".join(sorted(THEMES)))
        for t in sorted(THEMES):
            etat(t)
        return

    queries = THEMES[args.theme]
    sources = args.source or ["bigsoundbank", "freesound", "bbc", "pixabay"]
    base = MEDIAS / "dist" / "bruitages" / args.theme
    key = freesound_key() if "freesound" in sources else None
    if "freesound" in sources and not key:
        print("! FREESOUND_API_KEY absente : source sautee "
              "(cle gratuite sur https://freesound.org/apiv2/apply/)")
        sources = [s for s in sources if s != "freesound"]

    total = 0
    for source in sources:
        folder = base / source
        print(f"\n{source}")
        if source == "pixabay":
            sons = src_pixabay(folder)
            if not sons:
                print(f"    coller les URL dans {folder / 'urls.txt'}")
                continue
            total += collecte(folder, source, sons, args.theme)
            continue
        for q in queries:
            print(f"  {q}")
            try:
                if source == "bigsoundbank":
                    sons = src_bigsoundbank(q, args.par_requete)
                elif source == "freesound":
                    sons = src_freesound(q, args.par_requete, key)
                else:
                    sons = src_bbc(q, args.par_requete)
            except (urllib.error.URLError, urllib.error.HTTPError, ValueError, KeyError) as e:
                print(f"    ! recherche impossible : {e}")
                continue
            if not sons:
                print("    aucun resultat")
                continue
            total += collecte(folder, source, sons, q)

    print(f"\n{total} nouveaux sons")
    etat(args.theme)
    print(f"\n  ecouter : medias/dist/bruitages/{args.theme}/<source>/")
    print(f"  retenir : deplacer les MP3 gardes dans medias/dist/bruitages/{args.theme}/, a plat")

if __name__ == "__main__":
    main()
