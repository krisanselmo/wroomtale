"""python3 -m unittest discover -s firmware/web"""
import gzip
import json
import pathlib
import re
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from unittest.mock import patch

import build_page
import mock

WEB = pathlib.Path(__file__).resolve().parent
SRC = WEB.parent / "src"


def portal_routes():
    code = (SRC / "ConfigPortal.cpp").read_text()
    return {(m, p) for p, m in re.findall(r'g_server\.on\("(/api/\w+)", HTTP_(GET|POST)', code)}


def mock_routes():
    code = (WEB / "mock.py").read_text()
    get, post = code.split("def do_POST", 1)
    get = get.split("def do_GET", 1)[1]
    found = set()
    for method, body in (("GET", get), ("POST", post)):
        found |= {(method, p) for p in re.findall(r'"(/api/\w+)"', body)}
    return found


class Mock(unittest.TestCase):
    def test_serves_the_portal_routes(self):
        portal = portal_routes()
        self.assertGreater(len(portal), 10)  # the regex still reads the portal
        self.assertEqual(mock_routes(), portal)


class Files(unittest.TestCase):
    """The file manager against the mock's card."""

    def setUp(self):
        mock.LIBRARY.clear()
        mock.LIBRARY.update({f["chemin"]: mock.tracks_of(f["chemin"], f["pistes"])
                             for f in mock.FOLDERS if not f["chemin"].startswith("builtin:")})
        mock.Handler.board = mock.Board(False)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), mock.Handler)
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    def get(self, path):
        with urllib.request.urlopen(self.base + path) as r:
            return json.load(r)

    def post(self, path, body=b"", ctype=None):
        req = urllib.request.Request(self.base + path, data=body, method="POST")
        if ctype:
            req.add_header("Content-Type", ctype)
        try:
            with urllib.request.urlopen(req) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code

    def upload(self, folder, name, data=b"ID3"):
        body = (b"--B\r\nContent-Disposition: form-data; name=\"f\"; filename=\"" + name.encode() +
                b"\"\r\nContent-Type: audio/mpeg\r\n\r\n" + data + b"\r\n--B--\r\n")
        return self.post("/api/upload?dir=" + folder, body, "multipart/form-data; boundary=B")

    def names(self, folder):
        return [e["nom"] for e in self.get("/api/files?dir=" + folder)["entries"]]

    def test_folders_list_before_files(self):
        entries = self.get("/api/files?dir=/histoires/foret")["entries"]
        self.assertEqual(entries[-1]["nom"], "histoire.txt")
        root = self.get("/api/files?dir=/")["entries"]
        self.assertTrue(all(e["dossier"] for e in root))

    def test_an_uploaded_folder_becomes_a_target(self):
        self.assertEqual(self.post("/api/mkdir?path=/histoires/neuve"), 200)
        self.assertEqual(self.upload("/histoires/neuve", "01-debut.mp3", b"x" * 500), 200)
        self.assertEqual(self.get("/api/files?dir=/histoires/neuve")["entries"],
                         [{"nom": "01-debut.mp3", "dossier": False, "taille": 500}])
        targets = [f["chemin"] for f in self.get("/api/state")["folders"]]
        self.assertIn("/histoires/neuve", targets)

    def test_rename_moves_a_folder_and_its_files(self):
        self.assertEqual(self.post("/api/rename?from=/bruitages/train&to=/bruitages/tgv"), 200)
        self.assertNotIn("train", self.names("/bruitages"))
        self.assertEqual(len(self.names("/bruitages/tgv")), 3)
        self.assertEqual(self.post("/api/rename?from=/bruitages/tgv&to=/bruitages/moteur"), 409)

    def test_delete_is_recursive_but_never_the_root(self):
        self.assertEqual(self.post("/api/delete?path=/podcasts"), 200)
        self.assertNotIn("podcasts", self.names("/"))
        self.assertEqual(self.post("/api/delete?path=/"), 400)

    def test_download_gives_the_file_under_its_name(self):
        self.assertEqual(self.upload("/logs", "\u00e9t\u00e9.mp3", b"x" * 42), 200)
        with urllib.request.urlopen(self.base + "/api/download?path=/logs/%C3%A9t%C3%A9.mp3") as r:
            self.assertEqual(len(r.read()), 42)
            self.assertIn("filename*=UTF-8''%C3%A9t%C3%A9.mp3", r.headers["Content-Disposition"])
        for path, code in (("/logs", 404), ("/nope.mp3", 404), ("/../x", 400)):
            with self.assertRaises(urllib.error.HTTPError) as e:
                urllib.request.urlopen(self.base + "/api/download?path=" + path)
            self.assertEqual(e.exception.code, code, path)

    def test_parent_steps_are_refused(self):
        for route in ("/api/delete?path=/histoires/../logs", "/api/mkdir?path=/a/..",
                      "/api/rename?from=/logs&to=/../x", "/api/mkdir?path=relative"):
            self.assertEqual(self.post(route), 400, route)
        self.assertEqual(self.upload("/../", "x.mp3"), 400)

    def test_no_card_means_503(self):
        mock.Handler.board.sd = False
        self.assertEqual(self.post("/api/mkdir?path=/x"), 503)
        with self.assertRaises(urllib.error.HTTPError) as e:
            self.get("/api/files?dir=/")
        self.assertEqual(e.exception.code, 503)


class PortalPage(unittest.TestCase):
    def test_every_language_fills_every_slot(self):
        for lang in build_page.strings()[1]:
            page = build_page.render(lang)
            self.assertNotIn("{{", page, lang)
            self.assertNotIn("/*@T*/", page, lang)
            self.assertIn("<html lang=%s>" % lang, page)

    def test_languages_differ(self):
        self.assertNotEqual(build_page.render("fr"), build_page.render("en"))

    def test_header_holds_the_rendered_page(self):
        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(build_page, "SRC", pathlib.Path(tmp) / "PortalPage.h"):
            build_page.build("en")
            header = build_page.SRC.read_text()
        packed = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", header.split("{", 1)[1]))
        length = int(re.search(r"PORTAL_PAGE_GZ_LEN = (\d+);", header).group(1))
        self.assertEqual(len(packed), length)
        self.assertEqual(gzip.decompress(packed), build_page.render("en").encode())

    def test_unknown_key_fails_the_build(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(build_page, "WEB", pathlib.Path(tmp)):
            (build_page.WEB / "strings.json").write_text((WEB / "strings.json").read_text())
            (build_page.WEB / "index.html").write_text("<p>{{no_such_key}}</p>")
            with self.assertRaises(ValueError):
                build_page.render("fr")


if __name__ == "__main__":
    unittest.main()
