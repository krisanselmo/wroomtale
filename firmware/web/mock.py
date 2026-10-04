#!/usr/bin/env python3
"""Serve the config portal page with a fake board behind it.

    python3 firmware/web/mock.py [--lang en]   ->  http://localhost:8080

Same routes and same JSON shape as ConfigPortal.cpp, so index.html cannot tell
the difference. Type a letter in the terminal to move the board: `h` lists them.
"""

import argparse
import json
import posixpath
import random
import sys
import threading
import time
from email.parser import BytesParser
from email.policy import HTTP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlparse

import build_page

VOLUME_MAX = 21
IDLE_MAX_MIN = 240
BTN = [
    {"nom": "rouge", "couleur": "#FF1818"},
    {"nom": "vert", "couleur": "#18C838"},
    {"nom": "bleu", "couleur": "#2060FF"},
]

FOLDERS = [
    {"chemin": "/histoires/foret", "type": "histoire", "pistes": 14},
    {"chemin": "/histoires/phare", "type": "histoire", "pistes": 9},
    {"chemin": "/bruitages/moteur", "type": "dossier", "pistes": 4},
    {"chemin": "/bruitages/train", "type": "dossier", "pistes": 3},
    # MP3 a la racine du dossier *et* des sous-dossiers : le cas mixte, que
    # l'explorateur doit rendre jouable et ouvrable a la fois.
    {"chemin": "/podcasts/petit-oli", "type": "dossier", "pistes": 5},
    {"chemin": "/podcasts/petit-oli/jojo-le-ninja", "type": "dossier", "pistes": 23},
    {"chemin": "/podcasts/petit-oli/gigi-sourina-pipo", "type": "dossier", "pistes": 7},
    {"chemin": "/podcasts/petit-oli/doudou-lapin", "type": "dossier", "pistes": 11},
    {"chemin": "builtin:boot", "type": "embarque", "pistes": 1},
]

# What scanFolder would find on the card: sorted .mp3, full path in the player.
TITLES = ["le-depart", "la-clairiere", "le-vieux-chene", "la-riviere", "le-terrier",
          "la-nuit-tombe", "le-hibou", "les-lucioles", "le-sentier", "la-cabane",
          "l-orage", "le-matin", "le-retour", "la-chanson", "l-echo", "le-pont",
          "la-source", "le-renard", "la-clairiere-2", "le-depart-2", "la-fin",
          "le-silence", "la-suite"]


def tracks_of(path, count):
    return ["%02d-%s.mp3" % (i + 1, TITLES[i % len(TITLES)]) for i in range(count)]


LIBRARY = {f["chemin"]: tracks_of(f["chemin"], f["pistes"])
           for f in FOLDERS if not f["chemin"].startswith("builtin:")}

# A card filled to the firmware's MAX_TARGETS: the case that makes the panel's
# lists unusable, and the one no card on the desk happens to reproduce.
THEMES = ["ferme", "foret", "mer", "ville", "gare", "cirque", "ecole", "jungle",
          "montagne", "desert", "espace", "chateau"]
BIG = list(FOLDERS)
for _theme in THEMES:
    for _n in range(3):
        _p = "/bruitages/%s/piste-%d" % (_theme, _n + 1)
        BIG.insert(-1, {"chemin": _p, "type": "dossier", "pistes": 2 + _n * 3})
        LIBRARY[_p] = tracks_of(_p, 2 + _n * 3)

# ConfigPortal.cpp caps a listing at this, and a deletion at this depth.
MAX_FILES = 200
MAX_DELETE_DEPTH = 6


def safe_path(p):
    """safePath() in ConfigPortal.cpp: absolute, no parent step, no backslash."""
    return bool(p) and p.startswith("/") and ".." not in p and "\\" not in p


def card_tree():
    """The card behind FOLDERS: the same tracks, a manifest per story."""
    dirs, files = {"/", "/logs"}, {"/logs/batt.csv": 18_432}
    for folder, tracks in LIBRARY.items():
        parts = folder.strip("/").split("/")
        for n in range(1, len(parts) + 1):
            dirs.add("/" + "/".join(parts[:n]))
        for i, t in enumerate(tracks):
            files[folder + "/" + t] = 900_000 + 37_000 * i
    for f in FOLDERS:
        if f["type"] == "histoire":
            files[f["chemin"] + "/histoire.txt"] = 1_200
    return dirs, files


class Board:
    def __init__(self, station: bool):
        self.lock = threading.Lock()
        self.station = station
        self.volume = 8
        self.volume_cap = VOLUME_MAX
        self.volume_floor = 0
        self.sticky = True
        self.idle_min = 10
        self.clock = 0  # seconds since 1970, as the page last sent them
        # Tones.cpp order, without the self-test tone, as the board sends them.
        self.sfx = {k: True for k in ("boot", "play", "stop", "next", "prev", "volup",
                                      "voldown", "error", "tag", "ready", "lowbatt")}
        self.live = False
        self.sd = True
        self.rfid = True
        self.batt = True
        self.mv = 3980
        self.btn_down = 0
        self.btn_seen = 0
        self.last = ""
        self.last_seq = 0
        self.truncated = False
        self.folder = ""       # the queue the player holds, as in Player.cpp
        self.index = -1
        self.playing = ""
        self.tags = {
            "066fe8ad": "/podcasts/petit-oli/jojo-le-ninja",
            "04f66739de2a81": "builtin:boot",
            "04f56739de2a81": "/podcasts/petit-oli/doudou-lapin",
            "0a3b18c2": "/histoires/foret",
        }
        self.folders = list(FOLDERS)
        self.dirs, self.files = card_tree()

    # --- state seen by the page -----------------------------------------
    def hardware(self):
        return {
            "chip": "ESP32-D0WD-V3 v3.01",
            "cores": 2,
            "mhz": 240,
            "flashKB": 4096,
            "psramKB": 0,
            "heapKB": 142,
            "blockKB": 110,
            "rfid": {"ok": self.rfid, "fw": "1.6" if self.rfid else ""},
            "sd": {
                "ok": self.sd,
                "type": "SDHC" if self.sd else "aucune",
                "sizeMB": 15193 if self.sd else 0,
                "usedMB": 2841 if self.sd else 0,
            },
            "batt": {
                "ok": self.batt,
                "mv": self.mv if self.batt else 0,
                "pct": max(0, min(100, (self.mv - 3300) * 100 // 900)) if self.batt else 0,
                "low": self.batt and self.mv < 3400,
            },
            "net": {
                "mode": "station" if self.station else "point d'acces",
                "ssid": "Maison" if self.station else "WroomTale",
                "url": "http://192.168.1.42" if self.station else "http://192.168.4.1",
            },
            "volume": self.volume,
            "volumeMax": VOLUME_MAX,
            "volumeCap": self.volume_cap,
            "volumeFloor": self.volume_floor,
            "playing": self.playing,
            "live": self.live,
            "queue": {"folder": self.folder, "index": self.index,
                      "count": len(LIBRARY.get(self.folder, []))},
            "sticky": self.sticky,
            "idleMin": self.idle_min,
            "sons": [{"id": k, "on": v} for k, v in self.sfx.items()],
            "btn": BTN,
            "btnDown": self.btn_down,
            "btnSeen": self.btn_seen,
        }

    def state(self):
        return {
            "hw": self.hardware(),
            "last": self.last,
            "lastSeq": self.last_seq,
            "tronque": self.truncated and self.sd,
            "folders": self.folders if self.sd else FOLDERS[-2:],
            "tags": [{"uid": u, "folder": f} for u, f in self.tags.items()],
        }

    # --- what the page can ask -------------------------------------------
    def volume_step(self, up):
        self.volume = max(self.volume_floor,
                          min(self.volume_cap, self.volume + (1 if up else -1)))

    # Comme clampToLimits() dans Player.cpp : le plancher ne passe pas le plafond.
    def set_volume_limits(self, cap=None, floor=None):
        if cap is not None:
            self.volume_cap = max(0, min(VOLUME_MAX, cap))
        if floor is not None:
            self.volume_floor = max(0, min(VOLUME_MAX, floor))
        self.volume_floor = min(self.volume_floor, self.volume_cap)
        self.volume = max(self.volume_floor, min(self.volume, self.volume_cap))

    def play(self, folder, index=0):
        if folder.startswith("builtin:"):
            self.folder, self.index = "", -1
            self.playing, self.live = folder, True
            return True
        # Une cible fichier joue seule, hors de toute playlist -- comme
        # Player::playFile() cote carte.
        if folder.lower().endswith(".mp3"):
            self.folder, self.index = "", -1
            self.playing, self.live = folder, True
            return True
        tracks = LIBRARY.get(folder)
        if not tracks:
            return False
        self.folder = folder
        self.index = max(0, min(index, len(tracks) - 1))
        self.playing = folder + "/" + tracks[self.index]
        self.live = True
        return True

    def stop(self):
        self.folder, self.index, self.playing, self.live = "", -1, "", False

    def transport(self, cmd):
        if cmd == "play":
            if self.playing:
                self.live = not self.live      # pause keeps the queue loaded
            return True
        if cmd not in ("next", "prev"):
            return False
        tracks = LIBRARY.get(self.folder, [])
        if not tracks:
            return True
        step = 1 if cmd == "next" else -1
        return self.play(self.folder, (self.index + step) % len(tracks))

    # --- the card, as the file manager sees it ---------------------------
    def children(self, d):
        prefix = d.rstrip("/") + "/"
        names = {}
        for p in self.dirs:
            if p != d and p.startswith(prefix) and "/" not in p[len(prefix):]:
                names[p[len(prefix):]] = (True, 0)
        for p, size in self.files.items():
            if p.startswith(prefix) and "/" not in p[len(prefix):]:
                names[p[len(prefix):]] = (False, size)
        return names

    def exists(self, p):
        return p in self.dirs or p in self.files

    def rescan(self):
        """rescanTargets(): what the card holds now, not what it held at boot."""
        LIBRARY.clear()
        found = []
        for d in sorted(self.dirs):
            kids = self.children(d)
            tracks = sorted(n for n, (isdir, _) in kids.items()
                            if not isdir and n.lower().endswith(".mp3"))
            story = "histoire.txt" in kids
            if d == "/" or not (story or tracks):
                continue
            LIBRARY[d] = tracks
            found.append({"chemin": d, "type": "histoire" if story else "dossier",
                          "pistes": len(tracks)})
        self.folders = found + [FOLDERS[-1]]

    def upload(self, d, name, size):
        name = posixpath.basename(name or "")
        if not name or name.startswith(".") or not safe_path(posixpath.join(d, name)):
            return 400
        if d not in self.dirs:
            return 404
        self.stop()  # the decoder and the writer would share the bus
        self.files[posixpath.join(d, name)] = size
        self.rescan()
        return 200

    def mkdir(self, p):
        parent = posixpath.dirname(p)
        if parent not in self.dirs or self.exists(p):
            return 409
        self.dirs.add(p)
        return 200

    def rename(self, src, dst):
        if not self.exists(src) or self.exists(dst) or posixpath.dirname(dst) not in self.dirs:
            return 409
        if src in self.files:
            self.files[dst] = self.files.pop(src)
        else:
            if dst.startswith(src + "/"):
                return 409

            def moved(p):
                return dst + p[len(src):] if p == src or p.startswith(src + "/") else p

            self.dirs = {moved(p) for p in self.dirs}
            self.files = {moved(p): v for p, v in self.files.items()}
        self.rescan()
        return 200

    def delete(self, p):
        if p == "/" or not self.exists(p):
            return 404 if p != "/" else 400
        if p in self.files:
            del self.files[p]
        else:
            if any(x.count("/") - p.count("/") > MAX_DELETE_DEPTH
                   for x in list(self.dirs) + list(self.files) if x.startswith(p + "/")):
                return 507
            self.dirs = {x for x in self.dirs if x != p and not x.startswith(p + "/")}
            self.files = {x: v for x, v in self.files.items() if not x.startswith(p + "/")}
        self.rescan()
        return 200

    # --- what the terminal can ask ---------------------------------------
    def present(self, uid=None):
        if uid is None:
            uid = "".join("%02x" % random.randrange(256) for _ in range(7))
        self.last = uid
        self.last_seq += 1
        return uid

    def press(self, index):
        self.btn_down |= 1 << index
        self.btn_seen |= 1 << index
        threading.Timer(0.4, self._release, [index]).start()

    def _release(self, index):
        with self.lock:
            self.btn_down &= ~(1 << index)


class Handler(BaseHTTPRequestHandler):
    board: Board
    lang = build_page.DEFAULT_LANG

    def log_message(self, *_):
        pass  # the interesting log is the one we print ourselves

    def _send(self, code, ctype, body=b""):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            # Rendered on every request, so an edit shows on reload.
            self._send(200, "text/html; charset=utf-8", build_page.render(self.lang).encode())
        elif path == "/api/queue":
            with self.board.lock:
                b = self.board
                body = json.dumps({"folder": b.folder, "index": b.index,
                                   "tracks": LIBRARY.get(b.folder, [])}).encode()
            self._send(200, "application/json", body)
        elif path == "/api/tracks":
            folder = parse_qs(urlparse(self.path).query).get("folder", [""])[0]
            if not folder:
                return self._send(400, "text/plain", b"folder required")
            with self.board.lock:
                body = json.dumps({"folder": folder,
                                   "tracks": LIBRARY.get(folder, [])}).encode()
            self._send(200, "application/json", body)
        elif path == "/api/files":
            d = parse_qs(urlparse(self.path).query).get("dir", ["/"])[0]
            with self.board.lock:
                b = self.board
                if not b.sd:
                    return self._send(503, "text/plain", b"no SD card")
                if not safe_path(d):
                    return self._send(400, "text/plain", b"bad path")
                if d not in b.dirs:
                    return self._send(404, "text/plain", b"no such folder")
                kids = sorted(b.children(d).items(), key=lambda kv: (not kv[1][0], kv[0].lower()))
                entries = [{"nom": n, "dossier": isdir, "taille": size}
                           for n, (isdir, size) in kids[:MAX_FILES]]
                body = json.dumps({"dir": d, "entries": entries,
                                   "tronque": len(kids) > MAX_FILES}).encode()
            self._send(200, "application/json", body)
        elif path == "/api/download":
            f = parse_qs(urlparse(self.path).query).get("path", [""])[0]
            with self.board.lock:
                b = self.board
                if not b.sd:
                    return self._send(503, "text/plain", b"no SD card")
                if not safe_path(f):
                    return self._send(400, "text/plain", b"bad path")
                if f not in b.files:
                    return self._send(404, "text/plain", b"no such file")
                body = b"\0" * b.files[f]  # the mock keeps sizes, not contents
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition",
                             "attachment; filename*=UTF-8''" + quote(posixpath.basename(f), safe=""))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/api/state":
            with self.board.lock:
                body = json.dumps(self.board.state()).encode()
            self._send(200, "application/json", body)
        else:
            self._send(404, "text/plain", b"not found")

    def do_POST(self):
        url = urlparse(self.path)
        arg = {k: v[0] for k, v in parse_qs(url.query).items()}
        b = self.board
        upload = None
        if url.path == "/api/upload":
            length = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(length)
            msg = BytesParser(policy=HTTP).parsebytes(
                b"Content-Type: " + self.headers.get("Content-Type", "").encode() + b"\r\n\r\n" + raw)
            upload = [(part.get_filename(), len(part.get_payload(decode=True) or b""))
                      for part in msg.iter_parts() if part.get_filename()] if msg.is_multipart() else []
        with b.lock:
            files_route = url.path in ("/api/upload", "/api/mkdir", "/api/rename", "/api/delete")
            if files_route and not b.sd:
                return self._send(503, "text/plain", b"no SD card")
            paths = [arg.get(k, "") for k in ("dir", "path", "from", "to") if k in arg]
            if files_route and (not paths or not all(safe_path(p) for p in paths)):
                return self._send(400, "text/plain", b"bad path")
            if url.path == "/api/upload":
                if not upload:
                    return self._send(400, "text/plain", b"no file")
                for name, size in upload:
                    code = b.upload(arg["dir"], name, size)
                    if code != 200:
                        return self._send(code, "text/plain")
            elif url.path == "/api/mkdir":
                code = b.mkdir(arg["path"])
                if code != 200:
                    return self._send(code, "text/plain")
            elif url.path == "/api/rename":
                if "from" not in arg or "to" not in arg:
                    return self._send(400, "text/plain", b"from and to required")
                code = b.rename(arg["from"], arg["to"])
                if code != 200:
                    return self._send(code, "text/plain")
            elif url.path == "/api/delete":
                code = b.delete(arg["path"])
                if code != 200:
                    return self._send(code, "text/plain")
            elif url.path == "/api/clock":
                if int(arg.get("t", "0") or 0) <= 0:
                    return self._send(400, "text/plain", b"t wants seconds since 1970")
                b.clock = int(arg["t"])
            elif url.path == "/api/idle":
                try:
                    v = int(arg.get("v", ""))
                except ValueError:
                    return self._send(400, "text/plain", b"v out of range")
                if not 0 <= v <= IDLE_MAX_MIN:
                    return self._send(400, "text/plain", b"v out of range")
                b.idle_min = v
            elif url.path == "/api/volume":
                b.volume_step(arg.get("d") == "1")
            elif url.path in ("/api/volcap", "/api/volfloor"):
                try:
                    v = int(arg.get("v", ""))
                except ValueError:
                    return self._send(400, "text/plain", b"v out of range")
                if not 0 <= v <= VOLUME_MAX:
                    return self._send(400, "text/plain", b"v out of range")
                if url.path.endswith("cap"):
                    b.set_volume_limits(cap=v)
                else:
                    b.set_volume_limits(floor=v)
            elif url.path == "/api/sfx":
                if arg.get("id") not in b.sfx:
                    return self._send(400, "text/plain", b"unknown id")
                b.sfx[arg["id"]] = arg.get("on") == "1"
            elif url.path == "/api/sticky":
                b.sticky = arg.get("on") == "1"
            elif url.path == "/api/transport":
                if not b.transport(arg.get("cmd", "")):
                    return self._send(400, "text/plain")
            elif url.path == "/api/play":
                if not arg.get("folder"):
                    return self._send(400, "text/plain", b"folder required")
                b.play(arg["folder"], int(arg.get("i", 0)))
            elif url.path == "/api/stop":
                b.stop()
            elif url.path == "/api/btnreset":
                b.btn_seen = 0
            elif url.path == "/api/rescan":
                time.sleep(0.3)  # the board walks the card here
            elif url.path == "/api/assign":
                if not arg.get("uid") or not arg.get("folder"):
                    return self._send(400, "text/plain", b"uid and folder required")
                b.tags[arg["uid"]] = arg["folder"]
            elif url.path == "/api/forget":
                b.tags.pop(arg.get("uid", ""), None)
            else:
                return self._send(404, "text/plain")
        self._send(200, "text/plain")


HELP = """commandes :
  t  presenter un tag inconnu        k  represente un tag deja associe
  1  bouton rouge   2  vert   3  bleu
  s  carte SD dedans/dehors          r  lecteur RFID present/absent
  b  batterie branchee/debranchee    d  decharger la batterie de 100 mV
  p  jouer un dossier                 f  carte SD pleine/normale
  h  cette aide                       q  quitter"""


def console(board):
    for line in sys.stdin:
        cmd = line.strip()[:1].lower()
        with board.lock:
            if cmd == "t":
                print("tag", board.present())
            elif cmd == "k":
                uid = next(iter(board.tags), None)
                print("tag", board.present(uid) if uid else "(aucun tag connu)")
            elif cmd and cmd in "123":
                board.press(int(cmd) - 1)
            elif cmd == "s":
                board.sd = not board.sd
                print("carte SD", "dedans" if board.sd else "dehors")
            elif cmd == "r":
                board.rfid = not board.rfid
                print("RFID", "present" if board.rfid else "absent")
            elif cmd == "b":
                board.batt = not board.batt
                print("batterie", "branchee" if board.batt else "debranchee")
            elif cmd == "d":
                board.mv = max(3200, board.mv - 100)
                print("batterie", board.mv, "mV")
            elif cmd == "p":
                board.play("/podcasts/petit-oli/jojo-le-ninja", 2)
            elif cmd == "f":
                board.truncated = not board.truncated
                board.folders = BIG if board.truncated else list(FOLDERS)
                print("carte SD", "pleine (%d dossiers)" % len(board.folders)
                      if board.truncated else "normale")
            elif cmd == "h":
                print(HELP)
            elif cmd == "q":
                break
    print("bye")
    sys.exit(0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--station", action="store_true", help="joined the home WiFi instead of serving an AP")
    ap.add_argument("--lang", default=build_page.DEFAULT_LANG, choices=build_page.strings()[1])
    args = ap.parse_args()

    Handler.board = Board(args.station)
    Handler.lang = args.lang
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print("WroomTale mock -> http://localhost:%d" % args.port)
    print(HELP)
    threading.Thread(target=console, args=(Handler.board,), daemon=True).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
