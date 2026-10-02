#!/usr/bin/env python3
"""Turn a written story into a folder the box can read.

Narration goes through Piper by default, a neural voice that runs locally.
--voice google switches to Google Cloud TTS, which returns MP3 at the rate we
ask for, so nothing has to be transcoded.

Source format, one node per line:

    id | texte parle | prev=cible next=cible set=drapeau

The first node is the start. A node with no choice ends the story. This is the
format to hand a model: flat, one line per node, nothing to nest.

    python3 medias/tooling/story_build.py medias/src/histoires/foret.src
    python3 medias/tooling/story_build.py medias/src/histoires/foret.src /media/SD/histoires/foret
    python3 medias/tooling/story_build.py medias/src/histoires/foret.src --voice google

Cle Google : GOOGLE_TTS_API_KEY, ou .env.
"""
import base64, json, os, pathlib, re, subprocess, sys, urllib.request, wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import story

RATE = 22050
GOOGLE_URL = "https://texttospeech.googleapis.com/v1/text:synthesize"
VOICES_URL = "https://texttospeech.googleapis.com/v1/voices"
GOOGLE_VOICE = "fr-FR-Wavenet-G"   # 4 $/M caracteres, 4 M gratuits par mois

# medias/, and the repository root above it: .env stays out of the media tree.
MEDIAS = pathlib.Path(__file__).resolve().parent.parent
ROOT = MEDIAS.parent

VOICE = MEDIAS / "src" / "voix" / "fr.onnx"

def google_key():
    key = os.environ.get("GOOGLE_TTS_API_KEY")
    if key:
        return key
    env = ROOT / ".env"
    if env.is_file():
        for line in env.read_text().splitlines():
            if line.startswith("GOOGLE_TTS_API_KEY="):
                return line.split("=", 1)[1].strip().strip('"\'')
    sys.exit("GOOGLE_TTS_API_KEY absente. Voir la section Voix du README.")

# What each Google voice family costs and accepts. Everything derives from this
# table: the request body, the controls the studio shows, the cache key and the
# estimate. A parameter absent here is dropped rather than sent and ignored.
# cloud.google.com/text-to-speech/pricing and .../docs/list-voices-and-types,
# read 2026-09-21. Chirp3 is matched before Chirp, so it is listed first.
TOUS = ("speakingRate", "pitch", "volumeGainDb", "effectsProfileId")
FAMILLES = {
    "Studio":   {"usd": 160.0, "gratuit": 1_000_000, "ssml": "partiel",
                 "params": ("speakingRate", "volumeGainDb", "effectsProfileId")},
    "Chirp3":   {"usd":  30.0, "gratuit": 1_000_000, "ssml": False,
                 "params": ("volumeGainDb", "effectsProfileId")},
    "Chirp":    {"usd":  30.0, "gratuit": 1_000_000, "ssml": False,
                 "params": ("volumeGainDb", "effectsProfileId")},
    "Neural2":  {"usd":  16.0, "gratuit": 1_000_000, "ssml": True, "params": TOUS},
    "Polyglot": {"usd":  16.0, "gratuit": 1_000_000, "ssml": True, "params": TOUS},
    "Wavenet":  {"usd":   4.0, "gratuit": 4_000_000, "ssml": True, "params": TOUS},
    "Standard": {"usd":   4.0, "gratuit": 4_000_000, "ssml": True, "params": TOUS},
}

# Gemini-TTS is steered by a natural-language prompt and billed per TOKEN, not
# per character: text in, plus audio out at 25 tokens per second of speech.
# Keyed by model id, since its voice names carry no family marker.
# docs.cloud.google.com/text-to-speech/docs/gemini-tts, read 2026-09-21.
MODELES = {
    "gemini-2.5-flash-tts": {"titre": "Gemini 2.5 Flash", "entree": 0.50, "sortie": 10.0},
    "gemini-2.5-pro-tts":   {"titre": "Gemini 2.5 Pro",   "entree": 1.00, "sortie": 20.0},
    "gemini-3.1-flash-tts-preview": {"titre": "Gemini 3.1 Flash (preview)",
                                     "entree": 1.00, "sortie": 20.0},
}
TOKENS_PAR_SECONDE = 25     # audio tokens per second of speech
MAX_OCTETS = 4000           # per field, text and prompt alike

# Only the four the documentation names outright; the field stays free text.
VOIX_GEMINI = ("Kore", "Charon", "Callirrhoe", "Aoede")

for _m in MODELES.values():
    _m.update(ssml=False, params=("prompt",), gratuit=0, usd=None)

# min, max, default -- the ranges the API itself enforces.
BORNES = {"speakingRate": (0.25, 2.0, 1.0),
          "pitch": (-20.0, 20.0, 0.0),
          "volumeGainDb": (-96.0, 16.0, 0.0)}

# Applied in the order given; the box speaks through a small speaker.
PROFILS = ("wearable-class-device", "handset-class-device", "headphone-class-device",
           "small-bluetooth-speaker-class-device", "medium-bluetooth-speaker-class-device",
           "large-home-entertainment-class-device", "large-automotive-class-device",
           "telephony-class-application")

DEFAUT = {"speakingRate": 0.92}      # a story, not an announcement


def voix_disponibles(key, langue="fr-FR"):
    """Asked a name it does not know, Google answers with a default voice and no
    error: three typos would bill three times for the same audio. voices.list is
    free, so the catalogue is read rather than trusted."""
    url = f"{VOICES_URL}?languageCode={langue}&key={key}"
    with urllib.request.urlopen(url, timeout=30) as r:
        return sorted(v["name"] for v in json.load(r).get("voices", []))


def exige_voix(voice, key):
    """Stops before the first billed character when the name does not exist."""
    dispo = voix_disponibles(key, voice[:5] if re.match(r"^[a-z]{2}-[A-Z]{2}-", voice) else "fr-FR")
    if voice in dispo:
        return
    meme = [v for v in dispo if famille(v) == famille(voice)] or dispo
    sys.exit(f"voix inconnue : {voice}\n"
             f"Google repondrait avec une voix par defaut sans le dire.\n"
             f"Disponibles : " + ", ".join(meme))


def famille(voice, modele=""):
    """A Gemini variante is known by its model; the others by their voice name."""
    if modele:
        return modele if modele in MODELES else None
    for nom in FAMILLES:
        if nom.lower() in voice.lower().replace("-", ""):
            return nom
    return None


def capacites(voice, modele=""):
    nom = famille(voice, modele)
    return MODELES.get(nom) or FAMILLES.get(nom or "")


def retenus(voice, params, modele=""):
    """Only what this family accepts; the rest is dropped before the request."""
    f = capacites(voice, modele)
    admis = f["params"] if f else TOUS
    return {k: v for k, v in params.items() if k in admis and v not in (None, "")}


def google_say(text, mp3_path, voice, key, params=None, modele="", langue="fr-FR"):
    """Google encode directement a la frequence demandee."""
    reglages = retenus(voice, DEFAUT if params is None else params, modele)
    # Gemini takes its style instruction beside the text, not in audioConfig.
    prompt = reglages.pop("prompt", "")

    cfg = {"audioEncoding": "MP3", "sampleRateHertz": RATE}
    cfg.update(reglages)

    entree = {"text": text}
    if prompt:
        entree["prompt"] = prompt
    # "fr-FR-Studio-D" carries its language; "Kore" does not.
    porte_langue = re.match(r"^[a-z]{2}-[A-Z]{2}-", voice)
    vox = {"languageCode": voice[:5] if porte_langue else langue, "name": voice}
    if modele:
        vox["model_name"] = modele

    body = json.dumps({"input": entree, "voice": vox, "audioConfig": cfg}).encode()
    req = urllib.request.Request(f"{GOOGLE_URL}?key={key}", data=body,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            audio = json.loads(r.read())["audioContent"]
    except urllib.error.HTTPError as e:
        # Google names the API to enable and the console URL; both have to survive.
        detail = " ".join(e.read().decode().split())
        sys.exit(f"Google a refuse : {e.code} {detail[:600]}")
    mp3_path.write_bytes(base64.b64decode(audio))

def tts_init():
    try:
        from piper import PiperVoice
    except ImportError:
        sys.exit("piper-tts absent. Voir la section \"Voix\" du README.")
    if not VOICE.is_file():
        sys.exit(f"modele introuvable : {VOICE}\nVoir la section \"Voix\" du README.")
    return PiperVoice.load(str(VOICE))

def synth(text, wav_path, voice):
    with wave.open(str(wav_path), "wb") as w:
        voice.synthesize_wav(text, w)
    with wave.open(str(wav_path)) as w:
        return w.getnframes() / w.getframerate()

def encode(wav_path, mp3_path):
    subprocess.run(
        ["gst-launch-1.0", "-q", "filesrc", f"location={wav_path}", "!", "wavparse",
         "!", "audioconvert", "!", "audioresample", "!", "audio/x-raw,rate=22050,channels=1",
         "!", "lamemp3enc", "bitrate=64", "!", "filesink", f"location={mp3_path}"],
        check=True, capture_output=True)

def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    engine, voice = "piper", GOOGLE_VOICE
    if "--voice" in sys.argv:
        spec = sys.argv[sys.argv.index("--voice") + 1]
        engine, _, name = spec.partition(":")
        if name:
            voice = name
    if engine not in ("piper", "google"):
        sys.exit(f"moteur inconnu : {engine}")
    sys.argv = [sys.argv[0]] + args

    src = pathlib.Path(sys.argv[1])
    # Audio is regenerated from the .src: medias/dist/ is ignored by git.
    out = (pathlib.Path(sys.argv[2]) if len(sys.argv) == 3
           else MEDIAS / "dist" / "histoires" / src.stem)
    out.mkdir(parents=True, exist_ok=True)
    key = google_key() if engine == "google" else None
    if key:
        exige_voix(voice, key)
    piper = tts_init() if engine == "piper" else None
    print(f"  moteur : {engine}" + (f" ({voice})" if engine == "google" else ""))

    try:
        nodes = story.parse(src)
    except ValueError as e:
        sys.exit(str(e))
    # Said before a single character is billed, not after.
    for problem in story.check(nodes):
        print(f"  ! {problem}")

    manifest, total = [], 0.0
    for n in nodes:
        mp3 = f"{n.id}.mp3"
        if engine == "google":
            google_say(n.text, out / mp3, voice, key)
            dur = n.seconds
        else:
            wav = out / f"{n.id}.wav"
            dur = synth(n.text, wav, piper)
            encode(wav, out / mp3)
            wav.unlink()
        total += dur
        print(f"  {n.id:12s} {dur:5.1f} s  {(out / mp3).stat().st_size:6d} o")

        manifest.append(n.manifest(mp3))

    (out / "histoire.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    print(f"\n{len(manifest)} noeuds, {total:.0f} s de narration -> {out}")

if __name__ == "__main__":
    main()
