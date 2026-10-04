#!/usr/bin/env python3
"""index.html + strings.json -> src/PortalPage.h, gzipped, in one language.

Runs as a PlatformIO pre-build script, and standalone for a look at the size:

    python3 firmware/web/build_page.py [fr|en]

The language comes from WROOMTALE_LANG, else from `custom_lang` in
platformio.ini, else the argument; French by default. Gzip mtime is pinned so
the same page gives the same bytes.
"""

import gzip
import html
import json
import os
import re
import sys
from pathlib import Path

try:
    WEB = Path(__file__).resolve().parent
    SCONS = None
except NameError:
    # SCons execs the script with no __file__, but hands it the build env.
    Import("env")  # noqa: F821
    SCONS = env  # noqa: F821
    WEB = Path(SCONS["PROJECT_DIR"]) / "web"

SRC = WEB.parent / "src" / "PortalPage.h"
DEFAULT_LANG = "fr"


def strings():
    table = json.loads((WEB / "strings.json").read_text())
    langs = set(table["lang"])
    for key, by_lang in table.items():
        if set(by_lang) != langs:
            raise ValueError("strings.json: %s has %s, expected %s" % (key, sorted(by_lang), sorted(langs)))
    return table, sorted(langs)


def render(lang):
    """The page with {{key}} filled in and the JS table T set, for `lang`."""
    table, langs = strings()
    if lang not in langs:
        raise ValueError("unknown language %r, strings.json has %s" % (lang, langs))
    page = (WEB / "index.html").read_text()
    T = {key: by_lang[lang] for key, by_lang in table.items()}

    missing = sorted(set(re.findall(r"\{\{(\w+)\}\}", page) + re.findall(r"\bT\.(\w+)", page)) - set(T))
    if missing:
        raise ValueError("index.html uses keys strings.json lacks: %s" % ", ".join(missing))

    page = re.sub(r"\{\{(\w+)\}\}", lambda m: html.escape(T[m.group(1)]), page)
    # "</" inside a script would close it.
    table_js = json.dumps(T, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    return page.replace("/*@T*/{}", table_js, 1)


def build(lang):
    raw = render(lang).encode()
    packed = gzip.compress(raw, compresslevel=9, mtime=0)

    rows = [
        ", ".join("0x%02x" % b for b in packed[i:i + 12])
        for i in range(0, len(packed), 12)
    ]
    header = (
        "#pragma once\n"
        "// Generated from web/index.html (%s) by web/build_page.py -- do not edit.\n"
        "#include <Arduino.h>\n"
        "\n"
        "// Served with Content-Encoding: gzip; every browser that reaches the\n"
        "// portal speaks it, and %d bytes of flash stay free.\n"
        "constexpr size_t PORTAL_PAGE_GZ_LEN = %d;\n"
        "const uint8_t PORTAL_PAGE_GZ[] PROGMEM = {\n    %s\n};\n"
    ) % (lang, len(raw) - len(packed), len(packed), ",\n    ".join(rows))

    # Rewriting an identical header would rebuild the world for nothing.
    if not SRC.exists() or SRC.read_text() != header:
        SRC.write_text(header)
    return len(raw), len(packed)


def chosen_lang():
    if os.environ.get("WROOMTALE_LANG"):
        return os.environ["WROOMTALE_LANG"]
    if SCONS is not None:
        return SCONS.GetProjectOption("custom_lang", DEFAULT_LANG)
    return sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LANG


if SCONS is not None or __name__ == "__main__":
    lang = chosen_lang()
    raw, packed = build(lang)
    print("PortalPage.h (%s): %d B gzipped from %d B (%.0f %%)" % (lang, packed, raw, 100 * packed / raw))
