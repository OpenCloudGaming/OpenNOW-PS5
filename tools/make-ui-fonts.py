#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate the TV interface fonts in assets/fonts.

Run with: uv run --with fonttools==4.60.1 python3 tools/make-ui-fonts.py

Sources are pinned to one google/fonts commit and checked by SHA-256. Nunito
is a variable font, so static weights are instanced; every output is subset to
Latin text and stripped of hinting, which the runtime rasterizer ignores. The
IBM Plex Mono subset is renamed "OpenNOW Mono" because its license reserves the
font name "Plex" for unmodified versions.
"""
import hashlib
import io
import pathlib
import sys
import urllib.request

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

COMMIT = "9710da1eacb3be272583c3224dcb70f9da6eadbb"
BASE = f"https://raw.githubusercontent.com/google/fonts/{COMMIT}/ofl"
SOURCES = {
    "nunito": (f"{BASE}/nunito/Nunito%5Bwght%5D.ttf", "bb55a5ca5c2042335b3991af27c4d0705d0ef41cac6164ac737fd8f2a1e85207"),
    "plex": (f"{BASE}/ibmplexmono/IBMPlexMono-Medium.ttf", "a9b4c49bb299e05b5f6c481e7fb5e78943d2793249a0c8874ab574a2d1ea6755"),
    "nunito-ofl": (f"{BASE}/nunito/OFL.txt", "580df76c95a1ec5ab878ceb25bb3d85c6a076804e9c970c8c6972aea775fdf65"),
    "plex-ofl": (f"{BASE}/ibmplexmono/OFL.txt", "7e6b2818edbd8f6a01ae80641cc8f16a51080d08fb4e532be3a0b6f74adb07da"),
}
OUTPUTS = [
    ("nunito", 600, "Nunito-SemiBold.ttf"),
    ("nunito", 700, "Nunito-Bold.ttf"),
    ("nunito", 800, "Nunito-ExtraBold.ttf"),
    ("nunito", 900, "Nunito-Black.ttf"),
    ("plex", None, "OpenNOWMono-Medium.ttf"),
]
UNICODES = (
    list(range(0x20, 0x7F)) + list(range(0xA0, 0x180)) + list(range(0x2010, 0x2028)) +
    list(range(0x2030, 0x203B)) + [0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2248]
)

root = pathlib.Path(__file__).resolve().parent.parent
out = root / "assets" / "fonts"
out.mkdir(parents=True, exist_ok=True)
blobs = {}
for key, (url, digest) in SOURCES.items():
    with urllib.request.urlopen(url, timeout=60) as response:
        blobs[key] = response.read()
    if hashlib.sha256(blobs[key]).hexdigest() != digest:
        sys.exit(f"{key}: SHA-256 mismatch for {url}")

for source, weight, name in OUTPUTS:
    font = TTFont(io.BytesIO(blobs[source]))
    if weight is not None:
        font = instancer.instantiateVariableFont(font, {"wght": weight}, updateFontNames=False)
    options = subset.Options()
    options.hinting = False
    options.layout_features = ["kern"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=UNICODES)
    subsetter.subset(font)
    if source == "plex":
        names = font["name"]
        for record in list(names.names):
            if record.nameID in (1, 3, 4, 6, 16, 17, 18, 21, 22):
                names.removeNames(nameID=record.nameID)
        for name_id, value in ((1, "OpenNOW Mono"), (2, "Medium"), (3, "OpenNOW Mono Medium"),
                               (4, "OpenNOW Mono Medium"), (6, "OpenNOWMono-Medium")):
            names.setName(value, name_id, 3, 1, 0x409)
            names.setName(value, name_id, 1, 0, 0)
    font.recalcTimestamp = False
    font["head"].modified = font["head"].created
    font.save(out / name)
    print(f"{name}: {(out / name).stat().st_size} bytes")

licenses = out / "licenses"
licenses.mkdir(exist_ok=True)
(licenses / "Nunito-OFL.txt").write_bytes(blobs["nunito-ofl"])
(licenses / "IBMPlexMono-OFL.txt").write_bytes(blobs["plex-ofl"])
sys.exit(0)
