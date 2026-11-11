#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The DM32Mayhem Authors
"""Generate the DM32Mayhem flasher site: the data the RadioFoundry flasher modules read, the fixed firmware image and
its manifest, the English string catalog, and index.html.

    python3 web/tools/gen_site.py [--version v26.11]

The flasher itself (web/site/js) is a vendored, patched copy of RadioFoundry's MIT web flasher. This script reads the
firmware tree for the board and flash tables and writes web/site/data and web/site/firmware; it does not touch the JS.
Requires a built firmware image at firmware/build/bbs/rfoundry_dm32.bin.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FW = os.path.join(ROOT, "firmware")
SITE = os.path.join(ROOT, "web", "site")
SITE_PY = os.path.join(FW, "site")
LANDING = os.path.join(HERE, "landing.html")
sys.path.insert(0, SITE_PY)

from rfoundrysite import repo  # noqa: E402

PREFIXES = ("flash.", "state.", "status.", "variant.", "group.", "feature.", "report.", "editor.qdmr.note.")
BOARD = "dm32"


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()


def write(name, data):
    path = os.path.join(SITE, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        if isinstance(data, str):
            f.write(data)
        else:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.write("\n")
    print("wrote", os.path.relpath(path, ROOT))


def firmware(version):
    """Build the release manifest from the current image (tools/release.py) and put it at the fixed names the page
    loads. The image stays codec-less: the flasher merges the radio's own codec."""
    image = os.path.join(FW, "build", "bbs", "rfoundry_dm32.bin")
    if not os.path.exists(image):
        raise SystemExit("no %s: run tools/build-bbs.sh first" % os.path.relpath(image, ROOT))
    tmp = os.path.join(SITE, "firmware", "_release")
    os.makedirs(tmp, exist_ok=True)
    subprocess.run([sys.executable, os.path.join(FW, "tools", "release.py"), "--board", BOARD, "--bin", image,
                    "--version", version, "--out", tmp], cwd=FW, check=True)
    src = os.path.join(tmp, "rfoundry-%s-%s" % (BOARD, version))
    with open(src + ".json", encoding="utf-8") as f:
        man = json.load(f)
    man["image"]["file"] = "dm32mayhem.bin"
    man["version"] = version
    shutil.copyfile(src + ".bin", os.path.join(SITE, "firmware", "dm32mayhem.bin"))
    write("firmware/dm32mayhem.json", man)
    shutil.rmtree(tmp)
    with open(os.path.join(SITE, "firmware", "dm32mayhem.bin"), "rb") as f:
        data = f.read()
    return man, data


def radio():
    """The one radio the page offers, from the board registry, with the fields the site build adds."""
    reg = repo.registry("site")
    b = next(x for x in reg["boards"] if x["id"] == BOARD)
    b["status"] = "supported"
    b["support"] = b["summary"]
    b["unsupported_reasons"] = b["unsupported"]
    b["unsupported"] = list(b["unsupported"])
    b["not_written_reasons"] = b["not_written"]
    b["not_written"] = list(b["not_written"])
    b["required"] = []
    b["config"] = {"full": []}
    b["image"] = "img/dm32.svg"
    b["frame_image"] = None
    b["frame_geometry"] = None
    b["screens"] = []
    b["gallery"] = None
    return b


def strings():
    with open(os.path.join(FW, "site", "i18n", "en.json"), encoding="utf-8") as f:
        cat = json.load(f)
    out = {}
    for k, v in cat.items():
        if not k.startswith(PREFIXES) or k.startswith("flash.beta."):
            continue
        if isinstance(v, str):
            v = v.replace("RadioFoundry", "DM32Mayhem").replace("radiofoundry.org", "dm32mayhem.com")
        out[k] = v
    out.update({
        "flash.title": "DM32Mayhem Flasher",
        "flash.lead": "Back up, install and restore DM32Mayhem on the Baofeng DM-32UV over the programming cable. "
                       "Requires Chrome or Edge on a desktop computer, or Chrome on Android.",
        "flash.task.install": "Install DM32Mayhem",
        "flash.task.install.text": "Install DM32Mayhem on the radio. The original firmware and the radio's codec are "
                                    "backed up first.",
        "flash.task.backup": "Back Up",
        "flash.task.backup.text": "Read the radio's program memory and data flash into a file you keep.",
        "flash.task.restore": "Restore",
        "flash.task.restore.text": "Put a radio back from a backup bundle, exactly as it was.",
        "flash.task.help": "Fix a Radio",
        "flash.task.help.text": "What to try when the radio does not answer or an install went wrong.",
        "flash.start.privacy": "The firmware comes from dm32mayhem.com. Your stock firmware file, backups and radio "
                               "data stay on your computer. The flash log (an automatic report) records the browser's "
                               "language, the cable chip and what happened, never the radio's contents or the "
                               "firmware file's bytes.",
        "flash.radio.title": "Connect the DM-32UV",
        "flash.c7.write.starting": "Starting the installation…",
        "flash.c7.write.preparing": "Preparing the radio…",
        "flash.c7.write.comparing": "Comparing the radio's memory with the firmware. This can take a minute.",
        "flash.c7.write.writing_lead": "Writing the firmware to the radio…",
    })
    return out


def landing(version, short):
    """The DM32Mayhem home page: the showcase, with the flasher one click away at /flasher/."""
    with open(LANDING, encoding="utf-8") as f:
        html = f.read()
    return html.replace("{{version}}", version).replace("{{git}}", short)


def index(cat):
    catalog = json.dumps(cat, ensure_ascii=False).replace("</", "<\\/")
    return """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DM32Mayhem Flasher</title>
<meta name="description" content="Back up, install and restore DM32Mayhem on the Baofeng DM-32UV over the programming cable.">
<link rel="icon" href="/img/dm32.svg" type="image/svg+xml">
<link rel="stylesheet" href="/css/tokens.css">
<link rel="stylesheet" href="/css/site.css">
<link rel="stylesheet" href="/css/dm32.css">
</head>
<body data-root="/" data-home="/">
<div id="js-error" class="dm-error" role="alert" hidden></div>
<script>
(function () {
  // An error the flasher did not turn into a notice: say it, so a dead button is not a silent failure.
  var el = document.getElementById("js-error");
  function show(e) {
    var m = e && (e.message || e.reason && e.reason.message || e.reason);
    if (!m) return;
    el.textContent = "Page error: " + m;
    el.hidden = false;
    console.error(e);
  }
  addEventListener("error", function (e) { show(e.error || e); });
  addEventListener("unhandledrejection", function (e) { show(e.reason || e); });
})();
</script>
<header class="dm-head">
  <a class="dm-brand" href="/">DM32<span>Mayhem</span></a>
  <nav><a href="/">Home</a><a href="https://github.com/SarahRoseLives/DM32Mayhem">Source</a></nav>
</header>
<main class="dm-main">
  <section class="dm-intro">
    <h1>DM32Mayhem Flasher</h1>
    <p>Back up, install and restore DM32Mayhem on the <strong>Baofeng DM-32UV</strong>. Works in Chrome or Edge on a
    desktop computer, or Chrome on Android, with the radio's programming cable. Nothing is uploaded: the firmware
    comes from this site, and your backups stay in the browser or on your computer.</p>
    <p class="dm-warn"><strong>Keep the radio connected and this page open</strong> while it is being read or written.</p>
  </section>
  <div id="flasher" class="flasher" aria-live="polite">
    <p class="muted">Loading…</p>
  </div>
</main>
<footer class="dm-foot">
  <p>DM32Mayhem is MIT-licensed. The flasher is based on RadioFoundry's MIT web flasher; the backup, install and
  restore code is shared with it. <a href="https://github.com/SarahRoseLives/DM32Mayhem">Source on GitHub</a>.</p>
</footer>
<script type="application/json" id="i18n">%s</script>
<script type="module" src="/js/flasher/app.js"></script>
</body>
</html>
""" % catalog


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default=os.environ.get("DM32_VERSION", "v26.11"),
                    help="release name written into the manifest (default v26.11)")
    args = ap.parse_args()

    os.makedirs(os.path.join(SITE, "data"), exist_ok=True)
    man, image = firmware(args.version)
    b = radio()
    features = repo.features()
    groups = []
    for f in features:
        if f.get("group") and f["group"] not in groups:
            groups.append(f["group"])

    sha = hashlib.sha256(image).hexdigest()
    short = git("rev-parse", "--short=8", "HEAD")
    full = git("rev-parse", "HEAD")
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()

    write("data/radios.json", {"radios": [b], "planned": []})
    write("data/features.json", {"groups": groups, "features": features, "reported": repo.reported_features()})
    write("data/variants.json", {"variants": repo.variants()})
    write("data/flash-boards.json", repo.flash_boards())
    write("data/c7000-stub.json", repo.c7000_stub())
    write("data/releases.json", {"releases": [{
        "board": BOARD, "variant": "full", "version": args.version, "git": short,
        "git_time": int(git("show", "-s", "--format=%ct", "HEAD")),
        "features": man.get("features", ""), "off": man.get("off"),
        "image": "firmware/dm32mayhem.bin", "manifest": "firmware/dm32mayhem.json",
        "size": len(image), "sha256": sha}]})
    write("data/site.json", {
        "format": "rfoundry-site/1", "git": short, "version": short, "built": now, "release": args.version,
        "beta": None, "branch": "main", "repo": "https://github.com/SarahRoseLives/DM32Mayhem", "community": "",
        "languages": [{"code": "en", "name": "English"}], "default_language": "en",
        "reports": {"sentry_dsn": None}, "tool": None})
    cat = strings()
    write("data/i18n.json", cat)
    write("index.html", landing(args.version, short))
    write("flasher/index.html", index(cat))
    print("image %s (%d bytes, %s)" % (args.version, len(image), sha))
    print("commit %s" % full)


if __name__ == "__main__":
    main()
