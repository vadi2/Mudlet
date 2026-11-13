# getqt.py <outdir> <addon>... : installs official Qt 6.12.0 msvc2022_64 (aqt 3.3 cannot read the 6.12 repo layout)
import os, subprocess, sys, urllib.request, xml.etree.ElementTree as ET
base = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64"
out, addons = sys.argv[1], sys.argv[2:]
os.makedirs(out, exist_ok=True)
root = ET.fromstring(urllib.request.urlopen(base + "/Updates.xml").read())
want = {"qt.qt6.6120.win64_msvc2022_64"} | {f"qt.qt6.6120.addons.{a}.win64_msvc2022_64" for a in addons}
found = set()
for p in root.findall("PackageUpdate"):
    name = p.findtext("Name")
    if name not in want:
        continue
    found.add(name)
    ver = p.findtext("Version")
    for arc in (p.findtext("DownloadableArchives") or "").split(","):
        arc = arc.strip()
        if not arc or arc.startswith("qtdoc"):
            continue
        url = f"{base}/{name}/{ver}{arc}"
        dest = os.path.join(out, arc)
        print("get", url, flush=True)
        for attempt in range(5):
            try:
                urllib.request.urlretrieve(url, dest)
                break
            except Exception as e:
                print("retry", e, flush=True)
        else:
            sys.exit(f"failed {url}")
        subprocess.run(["7z", "x", "-y", "-bd", "-o" + out, dest], check=True, stdout=subprocess.DEVNULL)
        os.remove(dest)
missing = want - found
if missing:
    sys.exit(f"missing packages {missing}")
with open(os.path.join(out, "bin", "qt.conf"), "w") as f:
    f.write("[Paths]\nPrefix=..\n")
print("Qt installed in", out)
