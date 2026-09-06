#!/usr/bin/env python3
"""Build the Spectrum-style media deeplink package before embedding in the ELF."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
pkg = root / "pkg"
inputs = ["ps5torrent.gp5", "eboot.bin", "sce_sys/param.json", "sce_sys/icon0.png",
          "sce_sys/pic0.png", "sce_sys/pic1.png"]

def hashes():
    return {name: hashlib.sha256((pkg / name).read_bytes()).hexdigest()
            for name in inputs + ["PS5Torrent.pkg"]}

metadata = json.loads((pkg / "sce_sys/param.json").read_text())
installer = (root / "src/ps5_tile.c").read_text()
for symbol, key in [("TITLE_ID", "titleId"), ("TILE_VERSION", "contentVersion")]:
    value = re.search(r'#define ' + symbol + r' "([^"]+)"', installer).group(1)
    if value != metadata[key]:
        sys.exit(f"Installer {symbol} differs from package {key}.")

if "--check" in sys.argv:
    expected = json.loads((pkg / "media-package.sha256.json").read_text())
    if hashes() != expected:
        sys.exit("Media package is stale: run scripts/build_media_pkg.py, then make.")
    print("Media package and embedded artwork verified.")
else:
    tool = os.environ.get("PUB_CMD") or shutil.which("prospero-pub-cmd.exe")
    default = Path(r"C:\Program Files (x86)\SCE\Prospero\Tools\Publishing Tools\bin\prospero-pub-cmd.exe")
    if not tool and default.exists():
        tool = str(default)
    if not tool:
        sys.exit("Set PUB_CMD to prospero-pub-cmd. The prebuilt media PKG can be used without this tool.")
    for name in ["ps5torrent.gp5", "sce_sys/param.json"]:
        source = pkg / name
        source.write_bytes(source.read_bytes().replace(b"\r\n", b"\n"))
    (pkg / "eboot.bin").write_bytes(b"")  # Media deeplink tile; service lives in the ELF.
    subprocess.run([tool, "img_create", "--no_progress_bar",
                    "ps5torrent.gp5", "PS5Torrent.pkg"], cwd=pkg, check=True)
    (pkg / "media-package.sha256.json").write_text(json.dumps(hashes(), indent=2) + "\n")
