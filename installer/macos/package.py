"""Package CoolSynth for macOS (D234): the app, the VST3 and the CLAP as zips, and the installer, an unsigned .pkg.

usage: python3 installer/macos/package.py <artefacts folder> <output folder> <name>
   e.g. python3 installer/macos/package.py build/ci/CoolSynth_artefacts/Release build/ci/packages v0.6.0

The bundles are copied, the licence and notices put in the app, and each bundle signed ad hoc as a whole (the linker
signs only the executable; Apple silicon runs nothing unsigned). Then, in the output folder:
- CoolSynth-macos-arm64-{standalone,vst3,clap}-<name>.zip
- CoolSynth-Setup-<name>.pkg: installs for the current user only, with no administrator password, into their home
  folder: the app in ~/Applications, the plugins in ~/Library/Audio/Plug-Ins/VST3 and CLAP, the MiniLab 3 scripts
  where FL Studio, Ableton Live and Bitwig look, and each recorded-instrument family ticked, downloaded from Hugging
  Face into ~/Music/CoolSynth (the same zips and checksums as the Windows installer: installer/Instruments.iss).
  Unsigned, so the player clicks Open Anyway once; what it installs carries no quarantine flag.
- CoolSynth-macos-arm64-sha256-<name>.txt: the checksums of the other files.
"""
import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape, quoteattr

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
ID = "com.tomcoolpxl.coolsynth"


def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, check=True)


def project_version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as h:
        return re.search(r"project\(CoolSynth VERSION (\d+\.\d+\.\d+)", h.read()).group(1)


def instruments():
    """The instrument families of installer/Instruments.iss: key, title, what is inside, zip name, URL, size, SHA-256."""
    with open(os.path.join(ROOT, "installer", "Instruments.iss"), encoding="utf-8") as h:
        text = h.read()
    descriptions = dict(re.findall(r'Name: "instruments\\(\w+)"; Description: "([^"]+)"', text))
    families = []
    for line in text.splitlines():
        m = re.match(r'Source: "([^"]+)"; .*DestName: "([^"]+)"; ExternalSize: (\d+); Hash: "([0-9a-f]{64})"; '
                     r'Components: instruments\\(\w+);', line)
        if m:
            url, name, size, sha, key = m.groups()
            title, inside = descriptions[key].split(": ", 1)
            families.append(dict(key=key, title=title, inside=inside, name=name, url=url, size=int(size), sha=sha))
    assert families, "no instruments in Instruments.iss"
    return families


def sign(bundle):
    run("codesign", "--force", "--sign", "-", "--timestamp=none", bundle)
    run("codesign", "--verify", "--strict", bundle)


def zip_bundle(bundle, path):
    # ditto keeps a bundle's symlinks and signature, as Finder's own zips do.
    run("ditto", "-c", "-k", "--keepParent", bundle, path)


def payload_package(stage, identifier, bundle, location, version, out):
    """A component package laying down one bundle at location (a path inside the home folder)."""
    root = os.path.join(stage, "roots", identifier)
    os.makedirs(root)
    shutil.copytree(bundle, os.path.join(root, os.path.basename(bundle)), symlinks=True)
    plist = os.path.join(stage, identifier + ".plist")
    run("pkgbuild", "--analyze", "--root", root, plist)
    with open(plist, "rb") as h:
        components = plistlib.load(h)
    for c in components:
        c["BundleIsRelocatable"] = False  # an update replaces this copy, not one the player moved elsewhere
        c["BundleOverwriteAction"] = "upgrade"
    with open(plist, "wb") as h:
        plistlib.dump(components, h)
    run("pkgbuild", "--root", root, "--component-plist", plist, "--identifier", identifier, "--version", version,
        "--install-location", location, out)


def scripts_package(identifier, scripts, version, out):
    """A package with no payload, whose postinstall script does the work."""
    run("pkgbuild", "--nopayload", "--scripts", scripts, "--identifier", identifier, "--version", version, out)


def main():
    artefacts, output, name = sys.argv[1], sys.argv[2], sys.argv[3]
    version = project_version()
    families = instruments()
    stage = os.path.join(output, ".staging")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "pkgs"))

    # The bundles, signed whole.
    app = os.path.join(stage, "CoolSynth.app")
    vst3 = os.path.join(stage, "CoolSynth.vst3")
    clap = os.path.join(stage, "CoolSynth.clap")
    shutil.copytree(os.path.join(artefacts, "Standalone", "CoolSynth.app"), app, symlinks=True)
    shutil.copytree(os.path.join(artefacts, "VST3", "CoolSynth.vst3"), vst3, symlinks=True)
    shutil.copytree(os.path.join(artefacts, "CLAP", "CoolSynth.clap"), clap, symlinks=True)
    resources = os.path.join(app, "Contents", "Resources")
    shutil.copy(os.path.join(ROOT, "LICENSE"), os.path.join(resources, "LICENSE.txt"))
    shutil.copy(os.path.join(ROOT, "THIRD_PARTY_NOTICES.txt"), resources)
    for bundle in (app, vst3, clap):
        sign(bundle)

    files = []
    for kind, bundle in (("standalone", app), ("vst3", vst3), ("clap", clap)):
        path = os.path.join(output, f"CoolSynth-macos-arm64-{kind}-{name}.zip")
        zip_bundle(bundle, path)
        files.append(path)

    # The installer's packages, one per choice.
    pkgs = os.path.join(stage, "pkgs")
    payload_package(stage, f"{ID}.app", app, "/Applications", version, os.path.join(pkgs, "app.pkg"))
    payload_package(stage, f"{ID}.vst3", vst3, "/Library/Audio/Plug-Ins/VST3", version, os.path.join(pkgs, "vst3.pkg"))
    payload_package(stage, f"{ID}.clap", clap, "/Library/Audio/Plug-Ins/CLAP", version, os.path.join(pkgs, "clap.pkg"))

    minilab = os.path.join(stage, "scripts", "minilab")
    os.makedirs(minilab)
    shutil.copy(os.path.join(HERE, "install-minilab-scripts.sh"), os.path.join(minilab, "postinstall"))
    for folder, pattern in (("fl-studio/CoolSynth MiniLab", ".py"), ("ableton/CoolSynth_MiniLab", ".py"),
                            ("bitwig/CoolSynth", ".control.js")):
        source = os.path.join(ROOT, "resources", "daw-scripts", folder)
        target = os.path.join(minilab, "daw-scripts", folder)
        os.makedirs(target)
        for f in os.listdir(source):
            if f.endswith(pattern):
                shutil.copy(os.path.join(source, f), target)
    os.chmod(os.path.join(minilab, "postinstall"), 0o755)
    scripts_package(f"{ID}.minilab", minilab, version, os.path.join(pkgs, "minilab.pkg"))

    for f in families:
        scripts = os.path.join(stage, "scripts", "instruments-" + f["key"])
        os.makedirs(scripts)
        shutil.copy(os.path.join(HERE, "download-instruments.sh"), os.path.join(scripts, "postinstall"))
        os.chmod(os.path.join(scripts, "postinstall"), 0o755)
        with open(os.path.join(scripts, "instrument.conf"), "w", encoding="utf-8") as h:
            h.write(f"NAME='{f['name']}'\nURL='{f['url']}'\nSHA256='{f['sha']}'\n")
        scripts_package(f"{ID}.instruments.{f['key']}", scripts, version,
                        os.path.join(pkgs, f"instruments-{f['key']}.pkg"))

    # The distribution: what the player sees and picks.
    def choice(cid, title, description, pkg=None):
        ref = f'<pkg-ref id="{ID}.{pkg}"/>' if pkg else ""
        return (f'  <choice id="{cid}" title={quoteattr(title)} description={quoteattr(description)}>'
                f'{ref}</choice>\n')

    total = sum(f["size"] for f in families)
    outline = ['    <line choice="app"/>', '    <line choice="vst3"/>', '    <line choice="clap"/>',
               '    <line choice="minilab"/>', '    <line choice="instruments">']
    outline += [f'      <line choice="instruments_{f["key"]}"/>' for f in families]
    outline += ['    </line>']
    choices = (choice("app", "Standalone app", "CoolSynth on its own, in the Applications folder of your home folder.", "app")
               + choice("vst3", "VST3 plugin", "To play CoolSynth in your music software: Ableton Live, FL Studio, Reaper, "
                        "Bitwig, Cubase, Studio One.", "vst3")
               + choice("clap", "CLAP plugin", "For music software that supports CLAP: Bitwig, Reaper, FL Studio, Studio One.",
                        "clap")
               + choice("minilab", "Arturia MiniLab 3 support", "CoolSynth's MiniLab 3 scripts for FL Studio, Ableton Live "
                        "and Bitwig Studio. Harmless without a MiniLab.", "minilab")
               + choice("instruments", "Recorded instruments",
                        f"Downloaded while installing, about {total / 1e9:.1f} GB for all of them, into Music/CoolSynth.")
               + "".join(choice(f"instruments_{f['key']}", f["title"],
                                f"{f['inside'][0].upper()}{f['inside'][1:]}. About {f['size'] / 1e6:.0f} MB to download.",
                                f"instruments.{f['key']}") for f in families))
    refs = ([("app", "app.pkg"), ("vst3", "vst3.pkg"), ("clap", "clap.pkg"), ("minilab", "minilab.pkg")]
            + [(f"instruments.{f['key']}", f"instruments-{f['key']}.pkg") for f in families])
    distribution = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<installer-gui-script minSpecVersion="2">\n'
        f'  <title>CoolSynth {escape(version)}</title>\n'
        f'  <product id="{ID}" version="{version}"/>\n'
        '  <welcome file="welcome.html" mime-type="text/html"/>\n'
        '  <options customize="always" require-scripts="false" hostArchitectures="arm64"/>\n'
        '  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>\n'
        '  <volume-check><allowed-os-versions><os-version min="13.0"/></allowed-os-versions></volume-check>\n'
        '  <choices-outline>\n' + "\n".join(outline) + '\n  </choices-outline>\n'
        + choices
        + "".join(f'  <pkg-ref id="{ID}.{r}" version="{version}">{p}</pkg-ref>\n' for r, p in refs)
        + '</installer-gui-script>\n')
    distribution_path = os.path.join(stage, "distribution.xml")
    with open(distribution_path, "w", encoding="utf-8") as h:
        h.write(distribution)
    resources = os.path.join(stage, "resources")
    os.makedirs(resources)
    shutil.copy(os.path.join(HERE, "welcome.html"), resources)

    installer = os.path.join(output, f"CoolSynth-Setup-{name}.pkg")
    run("productbuild", "--distribution", distribution_path, "--package-path", pkgs, "--resources", resources,
        installer)
    files.append(installer)

    with open(os.path.join(output, f"CoolSynth-macos-arm64-sha256-{name}.txt"), "w", encoding="utf-8") as h:
        for path in files:
            digest = hashlib.sha256()
            with open(path, "rb") as f:
                for block in iter(lambda: f.read(1 << 20), b""):
                    digest.update(block)
            h.write(f"{digest.hexdigest()}  {os.path.basename(path)}\n")
    shutil.rmtree(stage)
    print("packaged into", output)


if __name__ == "__main__":
    main()
