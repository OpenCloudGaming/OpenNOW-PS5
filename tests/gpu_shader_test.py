#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import re
import subprocess
import tempfile


source = pathlib.Path("src/stream/native/gpu_presenter.cpp").read_text()
with tempfile.TemporaryDirectory(prefix="opennow-shaders-") as directory:
    for name, extension in (("vertex", "vert"), ("fragment", "frag")):
        match = re.search(r'const char\* ' + name + r'=R"\((.*?)\)";', source, re.S)
        if match is None:
            raise RuntimeError(f"Missing production {name} shader")
        path = pathlib.Path(directory) / f"presenter.{extension}"
        path.write_text(match.group(1))
        subprocess.run(["glslangValidator", str(path)], check=True)
print("Production GPU shaders passed GLSL validation")
