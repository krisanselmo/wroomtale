#!/usr/bin/env python3
"""index.html -> src/PortalPage.h, gzipped.

Runs as a PlatformIO pre-build script, and standalone for a look at the size.
The header is committed like the generated sounds: a clone builds without it
having to run. Gzip mtime is pinned so the same page gives the same bytes.
"""

import gzip
from pathlib import Path

try:
    WEB = Path(__file__).resolve().parent
except NameError:
    # SCons execs the script with no __file__, but hands it the build env.
    Import("env")  # noqa: F821
    WEB = Path(env["PROJECT_DIR"]) / "web"  # noqa: F821

SRC = WEB.parent / "src" / "PortalPage.h"


def build():
    raw = (WEB / "index.html").read_bytes()
    packed = gzip.compress(raw, compresslevel=9, mtime=0)

    rows = [
        ", ".join("0x%02x" % b for b in packed[i:i + 12])
        for i in range(0, len(packed), 12)
    ]
    header = (
        "#pragma once\n"
        "// Generated from web/index.html by web/build_page.py -- do not edit.\n"
        "#include <Arduino.h>\n"
        "\n"
        "// Served with Content-Encoding: gzip; every browser that reaches the\n"
        "// portal speaks it, and %d bytes of flash stay free.\n"
        "constexpr size_t PORTAL_PAGE_GZ_LEN = %d;\n"
        "const uint8_t PORTAL_PAGE_GZ[] PROGMEM = {\n    %s\n};\n"
    ) % (len(raw) - len(packed), len(packed), ",\n    ".join(rows))

    # Rewriting an identical header would rebuild the world for nothing.
    if not SRC.exists() or SRC.read_text() != header:
        SRC.write_text(header)
    return len(raw), len(packed)


raw, packed = build()
print("PortalPage.h: %d B gzipped from %d B (%.0f %%)" % (packed, raw, 100 * packed / raw))
