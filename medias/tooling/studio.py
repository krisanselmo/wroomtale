#!/usr/bin/env python3
"""Serve the story studio: walk a .src the way the box does, hear every node.

A variante is one set of synthesis settings: engine, voice, and whatever that
voice family accepts. Several exist at once, so the same node can be heard
under each and compared side by side. Variantes are created from the page.

story_build.FAMILLES is the single source of truth for what a family costs and
accepts; a parameter it does not list is dropped before the request rather than
sent and silently ignored.

Piper runs locally and costs nothing, so it is the default and the only engine
until --google is passed: a click in a web page must not be able to bill.

Renders land in medias/src/voix/studio/<histoire>/<variante>/. Each MP3 carries
the fingerprint of the text AND of the settings that produced it, so changing
either marks the take stale instead of replaying it.

    python3 medias/tooling/studio.py            ->  http://localhost:8081
    python3 medias/tooling/studio.py --google --voix fr-FR-Studio-D

Cle Google : GOOGLE_TTS_API_KEY, ou .env.
"""
import argparse, hashlib, json, sys, threading, urllib.error, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))
import story as model
import story_build

PAGE = Path(__file__).with_name("studio.html")
MEDIAS = Path(__file__).resolve().parent.parent
SOURCES = MEDIAS / "src" / "histoires"
BUILD = MEDIAS / "src" / "voix" / "studio"

PAYANT = False          # raised by --google; no variante can bill without it


def tarif(voice, modele=""):
    """Two billing shapes: per character for the classic voices, per token for
    Gemini -- text in, plus audio out at 25 tokens per second of speech."""
    nom = story_build.famille(voice, modele)
    f = story_build.capacites(voice, modele)
    if not f:
        return {"famille": "inconnue", "facturation": "caractere",
                "usdParMillion": None, "gratuitParMois": 0, "params": [], "ssml": False}
    base = {"famille": f.get("titre", nom), "params": list(f["params"]), "ssml": f["ssml"]}
    if nom in story_build.MODELES:
        return dict(base, facturation="token", usdEntree=f["entree"], usdSortie=f["sortie"],
                    tokensParSeconde=story_build.TOKENS_PAR_SECONDE,
                    usdParMillion=None, gratuitParMois=0)
    return dict(base, facturation="caractere", usdParMillion=f["usd"],
                gratuitParMois=f["gratuit"])


# Loading Piper costs seconds, so handles are shared by every variante and
# synthesis is serialised: one model in memory, one take at a time.
_handles, _handle_lock, _say_lock = {}, threading.Lock(), threading.Lock()


def handle(engine):
    with _handle_lock:
        if engine not in _handles:
            try:
                _handles[engine] = (story_build.google_key() if engine == "google"
                                    else story_build.tts_init())
            except SystemExit as e:              # story_build exits; a server may not
                raise RuntimeError(str(e)) from e
        return _handles[engine]


_voix, _voix_lock = None, threading.Lock()


def catalogue():
    """The real names, or an empty list when Google cannot be reached."""
    if not PAYANT:
        return []
    global _voix
    with _voix_lock:
        if _voix is None:
            try:
                _voix = story_build.voix_disponibles(handle("google"))
            except (RuntimeError, urllib.error.URLError, OSError, ValueError, KeyError):
                return []
        return _voix


def nombre(x):
    """0.92 -> "0.92", 1.0 -> "1": keeps folder names short and stable."""
    return f"{x:g}"


class Variante:
    """One set of synthesis settings, and the folder its takes live in."""

    ABREGE = {"speakingRate": "d", "pitch": "h", "volumeGainDb": "v",
              "effectsProfileId": "p", "prompt": "c"}

    def __init__(self, engine, voice="", params=None, modele="", langue="fr-FR"):
        if engine not in ("piper", "google"):
            raise ValueError(f"moteur inconnu : {engine}")
        if engine == "google":
            if not PAYANT:
                raise ValueError("voix Google verrouillee, relancer avec --google")
            if not voice:
                raise ValueError("une variante Google demande une voix")
        if modele and modele not in story_build.MODELES:
            raise ValueError(f"modele inconnu : {modele}")
        # Google accepts an unknown name and answers with a default voice, so a
        # typo would quietly produce the same audio under three variantes.
        dispo = catalogue() if engine == "google" and not modele else []
        if dispo and voice not in dispo:
            raise ValueError(f"voix inconnue : {voice}")
        self.engine = engine
        self.voice = voice if engine == "google" else ""
        self.modele = modele if engine == "google" else ""
        self.langue = langue
        self.demande = dict(params or {}) if engine == "google" else {}
        prompt = self.demande.get("prompt", "")
        if len(prompt.encode("utf-8")) > story_build.MAX_OCTETS:
            raise ValueError(f"consigne de plus de {story_build.MAX_OCTETS} octets")
        for key, val in self.demande.items():
            borne = story_build.BORNES.get(key)
            if borne and not borne[0] <= val <= borne[1]:
                raise ValueError(f"{key} hors bornes [{borne[0]}, {borne[1]}] : {val}")
            if key == "effectsProfileId":
                for prof in val:
                    if prof not in story_build.PROFILS:
                        raise ValueError(f"profil audio inconnu : {prof}")

    @property
    def reglages(self):
        """What the request carries beyond the text; part of the cache key."""
        if self.engine != "google":
            return {}
        return story_build.retenus(self.voice, self.demande, self.modele)

    @property
    def ignores(self):
        """Asked for, but not supported by this family."""
        return sorted(set(self.demande) - set(self.reglages))

    @property
    def id(self):
        """Folder name and API key; two settings must never share one."""
        if self.engine != "google":
            return self.engine
        bits = [self.engine, self.modele or "", self.voice]
        for key, val in sorted(self.reglages.items()):
            court = self.ABREGE.get(key, key)
            if key == "prompt":     # a whole sentence: keep a short stable digest
                bits.append(court + hashlib.sha1(val.encode("utf-8")).hexdigest()[:6])
            else:
                bits.append(court + ("".join(p[:2] for p in val)
                                     if isinstance(val, list) else nombre(val)))
        return "-".join(b for b in bits if b)

    @property
    def payant(self):
        return self.engine == "google"

    def describe(self):
        return {"id": self.id, "moteur": self.engine, "voix": self.voice,
                "modele": self.modele, "payant": self.payant,
                "reglages": self.reglages, "ignores": self.ignores,
                "tarif": tarif(self.voice, self.modele) if self.payant else None}

    def say(self, text, mp3):
        with _say_lock:
            h = handle(self.engine)
            try:
                if self.engine == "google":
                    story_build.google_say(text, mp3, self.voice, h, self.demande,
                                           self.modele, self.langue)
                else:
                    wav = mp3.with_suffix(".wav")
                    story_build.synth(text, wav, h)
                    story_build.encode(wav, mp3)
                    wav.unlink(missing_ok=True)
            except SystemExit as e:      # story_build exits; a server may not
                raise RuntimeError(str(e)) from e


VARIANTES = {}


def ajouter(v):
    VARIANTES[v.id] = v
    return v


def fingerprint(text, variante=None):
    seed = text + "|" + json.dumps(getattr(variante, "reglages", {}), sort_keys=True)
    return hashlib.sha1(seed.encode("utf-8")).hexdigest()[:12]


def audio_dir(name, variante_id):
    return BUILD / name / variante_id


def audio_state(name, variante, node):
    """absent, perime (text or settings changed since), or pret."""
    folder = audio_dir(name, variante.id)
    mp3, stamp = folder / f"{node.id}.mp3", folder / f"{node.id}.sha"
    if not mp3.is_file():
        return "absent", 0
    if not stamp.is_file() or stamp.read_text().strip() != fingerprint(node.text, variante):
        return "perime", mp3.stat().st_size
    return "pret", mp3.stat().st_size


def load(name):
    src = SOURCES / f"{name}.src"
    if not src.is_file():
        raise FileNotFoundError(name)
    return model.parse(src)


def describe(name):
    """What the story list shows: enough to choose without opening it."""
    try:
        nodes = load(name)
    except (ValueError, FileNotFoundError) as e:
        return {"nom": name, "erreur": str(e), "noeuds": 0, "secondes": 0,
                "drapeaux": 0, "problemes": 1}
    return {
        "nom": name,
        "noeuds": len(nodes),
        "mots": sum(n.words for n in nodes),
        "caracteres": sum(len(n.text) for n in nodes),
        "secondes": round(sum(n.seconds for n in nodes)),
        "drapeaux": len({n.sets for n in nodes if n.sets}),
        "problemes": len(model.check(nodes)),
    }


def detail(name):
    nodes = load(name)
    out = []
    for n in nodes:
        audio = {}
        for vid, v in VARIANTES.items():
            state, size = audio_state(name, v, n)
            audio[vid] = {"etat": state, "octets": size}
        out.append({
            "id": n.id,
            "texte": n.text,
            "mots": n.words,
            "caracteres": len(n.text),
            "secondes": round(n.seconds, 1),
            "accorde": n.sets,
            "choix": [{"bouton": b, "couleur": model.COLORS[b], "nom": model.NAMES[b],
                       "drapeau": flag, "cible": target}
                      for b, (flag, target) in n.choices.items()],
            "audio": audio,
        })
    return {
        "nom": name,
        "depart": nodes[0].id if nodes else "",
        "noeuds": out,
        "problemes": model.check(nodes),
        "variantes": [v.describe() for v in VARIANTES.values()],
        "googleOuvert": PAYANT,
        "voixDefaut": story_build.GOOGLE_VOICE,
        "familles": story_build.FAMILLES,
        "modeles": story_build.MODELES,
        "voixGemini": list(story_build.VOIX_GEMINI),
        "voixGoogle": catalogue(),
        "bornes": story_build.BORNES,
        "profils": list(story_build.PROFILS),
        "caracteres": sum(len(n.text) for n in nodes),
        "mots": sum(n.words for n in nodes),
    }


def synth(name, node_id, variante_id):
    v = VARIANTES.get(variante_id)
    if v is None:
        raise RuntimeError(f"variante {variante_id} inconnue")
    node = next((n for n in load(name) if n.id == node_id), None)
    if node is None:
        raise RuntimeError(f"noeud {node_id} introuvable")
    folder = audio_dir(name, v.id)
    folder.mkdir(parents=True, exist_ok=True)
    mp3 = folder / f"{node.id}.mp3"
    v.say(node.text, mp3)
    (folder / f"{node.id}.sha").write_text(fingerprint(node.text, v))
    return {"id": node.id, "variante": v.id, "octets": mp3.stat().st_size, "etat": "pret"}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def _send(self, code, ctype, body=b""):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, payload, code=200):
        self._send(code, "application/json", json.dumps(payload, ensure_ascii=False).encode())

    def do_GET(self):
        url = urlparse(self.path)
        arg = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path in ("/", "/index.html"):
            return self._send(200, "text/html; charset=utf-8", PAGE.read_bytes())
        if url.path == "/api/histoires":
            return self._json([describe(p.stem) for p in model.stories(SOURCES)])
        if url.path == "/api/histoire":
            try:
                return self._json(detail(arg.get("nom", "")))
            except (FileNotFoundError, ValueError) as e:
                return self._json({"erreur": str(e)}, 404)
        if url.path.startswith("/audio/"):
            rel = url.path[len("/audio/"):].split("/")
            if len(rel) != 3 or any(part in ("", "..") for part in rel):
                return self._send(400, "text/plain", b"chemin invalide")
            mp3 = BUILD / rel[0] / rel[1] / rel[2]
            if not mp3.is_file():
                return self._send(404, "text/plain", b"pas encore genere")
            return self._send(200, "audio/mpeg", mp3.read_bytes())
        self._send(404, "text/plain", b"not found")

    def do_POST(self):
        url = urlparse(self.path)
        arg = {k: v[0] for k, v in parse_qs(url.query).items()}
        try:
            if url.path == "/api/synth":
                return self._json(synth(arg.get("nom", ""), arg.get("noeud", ""),
                                        arg.get("variante", "piper")))
            if url.path == "/api/variante":
                params = {}
                for key in story_build.BORNES:
                    val = arg.get(key, "")
                    if val not in ("", "aucun"):
                        params[key] = float(val)
                profil = arg.get("effectsProfileId", "")
                if profil not in ("", "aucun"):
                    params["effectsProfileId"] = [profil]
                if arg.get("prompt", "").strip():
                    params["prompt"] = arg["prompt"].strip()
                v = ajouter(Variante(arg.get("moteur", "piper"), arg.get("voix", ""),
                                     params, arg.get("modele", "")))
                return self._json(v.describe())
        except (RuntimeError, FileNotFoundError, ValueError) as e:
            return self._json({"erreur": str(e)}, 400)
        self._send(404, "text/plain")

    def do_DELETE(self):
        url = urlparse(self.path)
        arg = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path != "/api/variante":
            return self._send(404, "text/plain")
        vid = arg.get("id", "")
        if vid == "piper":
            return self._json({"erreur": "la variante locale ne s'enleve pas"}, 400)
        VARIANTES.pop(vid, None)
        return self._json({"id": vid, "retiree": True})


def main():
    global PAYANT
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--google", action="store_true",
                    help="autorise les variantes Google, facturees au caractere")
    ap.add_argument("--voix", default=story_build.GOOGLE_VOICE)
    ap.add_argument("--debit", type=float, default=0.92, metavar="X",
                    help="speakingRate de la variante Google de depart")
    ap.add_argument("--hauteur", type=float, default=-1.0, metavar="X",
                    help="pitch en demi-tons ; ignore par Studio et Chirp")
    args = ap.parse_args()

    PAYANT = args.google
    ajouter(Variante("piper"))
    if args.google:
        try:
            ajouter(Variante("google", args.voix,
                             {"speakingRate": args.debit, "pitch": args.hauteur}))
        except ValueError as e:
            meme = [v for v in catalogue()
                    if story_build.famille(v) == story_build.famille(args.voix)]
            sys.exit(f"{e}\nDisponibles : " + ", ".join(meme or catalogue()[:8] or ["?"]))

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"WroomTale studio -> http://localhost:{args.port}")
    print(f"  histoires : {SOURCES}")
    print(f"  rendu     : {BUILD}")
    for v in VARIANTES.values():
        if v.payant:
            t = tarif(v.voice)
            prix = f"{t['usdParMillion']:g} $/M car." if t["usdParMillion"] else "tarif inconnu"
            print(f"  variante  : {v.id} — {prix}, {v.reglages}")
        else:
            print(f"  variante  : {v.id} — local, gratuit")
    if not args.google:
        print("  (--google pour deverrouiller les voix payantes)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
