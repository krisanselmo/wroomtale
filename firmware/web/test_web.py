"""python3 -m unittest discover -s firmware/web"""
import gzip
import pathlib
import re
import unittest

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


class PortalPage(unittest.TestCase):
    def test_header_holds_index_html(self):
        header = (SRC / "PortalPage.h").read_text()
        packed = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", header.split("{", 1)[1]))
        length = int(re.search(r"PORTAL_PAGE_GZ_LEN = (\d+);", header).group(1))
        self.assertEqual(len(packed), length)
        self.assertEqual(gzip.decompress(packed), (WEB / "index.html").read_bytes(),
                         "PortalPage.h is stale: run python3 firmware/web/build_page.py")


if __name__ == "__main__":
    unittest.main()
