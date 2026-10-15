"""Fail unless every version source matches -v VERSION."""
import argparse
import re
import sys

parser = argparse.ArgumentParser()
parser.add_argument('-v', '--version', type=str, required=True)
args = parser.parse_args()

v = args.version
quad = v + ".0"
comma = quad.replace(".", ",")

CHECKS = [
    ("SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/manifest.json",
     r'"version"\s*:\s*"([^"]+)"', v),
    ("SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml",
     r'^\s*version:\s*"([^"]+)"', v),
    ("SKSE_Source/CMakeLists.txt",
     r'project\(Injury_Engine\s+VERSION\s+([0-9.]+)', v),
    ("SKSE_Source/version.rc", r'FILEVERSION\s+([0-9,]+)', comma),
    ("SKSE_Source/version.rc", r'PRODUCTVERSION\s+([0-9,]+)', comma),
    ("SKSE_Source/version.rc", r'"FileVersion",\s*"([^"]+)"', quad),
    ("SKSE_Source/version.rc", r'"ProductVersion",\s*"([^"]+)"', quad),
]

bad = 0
for path, pattern, want in CHECKS:
    with open(path, encoding="utf-8") as f:
        m = re.search(pattern, f.read(), re.M)
    got = m.group(1) if m else None
    if got != want:
        bad += 1
        print(f"MISMATCH {path}: found {got!r}, want {want!r}")
if bad:
    sys.exit(1)
print(f"all version sources match {v}")
