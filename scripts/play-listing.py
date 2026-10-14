#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
"""Puts the Google Play listing together in the layout fastlane supply reads.

    play-listing.py stage <dir>    write the listing to <dir>
    play-listing.py check          stage it to a temp dir and hold it to Play's limits

The pieces stay where they already live, so nothing is kept twice:

    packaging/android/play/<locale>/*.txt     the title and the two descriptions
    packaging/icons/climat-512.png            the icon
    packaging/android/play/feature-graphic.png
    packaging/android/play/screenshots/*.png  rendered by scripts/play-shots.sh
    packaging/linux/climat.metainfo.xml.in    the release note, which becomes
                                              Play's "What's new" text, unless
    packaging/android/play/<locale>/changelogs/<version code>.txt  is there

The release note is the one a person writes in the release pull request, so
Play's "What's new" and a Linux software centre say the same thing, and `check`
runs on that pull request: a note too long for Play fails there, not on the
release. Plain string work, no XML parser, for the reason
scripts/android-icon-layers.py gives: the devshell's python has no expat.
"""
import os
import re
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAY = os.path.join(ROOT, "packaging", "android", "play")
ICON = os.path.join(ROOT, "packaging", "icons", "climat-512.png")
FEATURE = os.path.join(PLAY, "feature-graphic.png")
SCREENSHOTS = os.path.join(PLAY, "screenshots")
METAINFO = os.path.join(ROOT, "packaging", "linux", "climat.metainfo.xml.in")
CMAKELISTS = os.path.join(ROOT, "CMakeLists.txt")

# The listing's language and the one the release note is written in.
DEFAULT_LOCALE = "en-CA"

# Play Console's limits, in characters.
TEXT_LIMITS = {"title": 30, "short_description": 80, "full_description": 4000}
CHANGELOG_LIMIT = 500


def project_version():
    with open(CMAKELISTS, encoding="utf-8") as f:
        match = re.search(r"^\s*VERSION\s+(\d+)\.(\d+)\.(\d+)", f.read(), re.M)
    if not match:
        sys.exit("play-listing: no VERSION in CMakeLists.txt")
    return tuple(int(part) for part in match.groups())


def version_code(version):
    # The same sum app/CMakeLists.txt gives Qt: 0.1.0 is 100, 1.2.3 is 10203.
    major, minor, patch = version
    return major * 10000 + minor * 100 + patch


def release_note(version):
    """The <release> note for `version`, as plain text Play shows as it is."""
    with open(METAINFO, encoding="utf-8") as f:
        text = f.read()
    wanted = ".".join(str(part) for part in version)
    match = re.search(r'<release version="%s"[^>]*>(.*?)</release>' % re.escape(wanted), text, re.S)
    if not match:
        sys.exit(f"play-listing: no <release version=\"{wanted}\"> in {os.path.relpath(METAINFO, ROOT)}")
    lines = []
    for tag, body in re.findall(r"<(p|li)>(.*?)</\1>", match.group(1), re.S):
        body = " ".join(re.sub(r"<[^>]+>", "", body).split())
        body = body.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&")
        lines.append(("• " if tag == "li" else "") + body)
    if not lines:
        sys.exit(f"play-listing: the <release version=\"{wanted}\"> note has no <p> or <li>")
    return "\n".join(lines)


def png_facts(path):
    """Width, height and whether it can hold transparency, from the header."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    width, height, _depth, colour = struct.unpack(">IIBB", data[16:26])
    alpha = colour in (4, 6) or b"tRNS" in data[:data.find(b"IDAT")]
    return width, height, alpha


def locales():
    found = sorted(name for name in os.listdir(PLAY)
                   if re.fullmatch(r"[a-z]{2,3}(-[A-Z]{2})?", name)
                   and os.path.isfile(os.path.join(PLAY, name, "title.txt")))
    if DEFAULT_LOCALE not in found:
        sys.exit(f"play-listing: no {DEFAULT_LOCALE}/title.txt in {os.path.relpath(PLAY, ROOT)}")
    return found


def stage(dest):
    version = project_version()
    code = version_code(version)
    for locale in locales():
        out = os.path.join(dest, locale)
        os.makedirs(out, exist_ok=True)
        for field in TEXT_LIMITS:
            source = os.path.join(PLAY, locale, f"{field}.txt")
            if os.path.isfile(source):
                with open(source, encoding="utf-8") as f:
                    text = f.read().strip()
                with open(os.path.join(out, f"{field}.txt"), "w", encoding="utf-8") as f:
                    f.write(text)

    out = os.path.join(dest, DEFAULT_LOCALE)
    images = os.path.join(out, "images")
    phone = os.path.join(images, "phoneScreenshots")
    os.makedirs(phone, exist_ok=True)
    shutil.copyfile(ICON, os.path.join(images, "icon.png"))
    shutil.copyfile(FEATURE, os.path.join(images, "featureGraphic.png"))
    for name in sorted(os.listdir(SCREENSHOTS)):
        if name.endswith(".png") and not name.endswith(".actual.png"):
            shutil.copyfile(os.path.join(SCREENSHOTS, name), os.path.join(phone, name))

    # A note written for Play alone wins, for a release whose AppStream note
    # talks about the desktop.
    changelogs = os.path.join(out, "changelogs")
    os.makedirs(changelogs, exist_ok=True)
    override = os.path.join(PLAY, DEFAULT_LOCALE, "changelogs", f"{code}.txt")
    if os.path.isfile(override):
        with open(override, encoding="utf-8") as f:
            note = f.read().strip()
    else:
        note = release_note(version)
    with open(os.path.join(changelogs, f"{code}.txt"), "w", encoding="utf-8") as f:
        f.write(note)
    return code


def check(dest, code):
    problems = []

    def rel(path):
        return os.path.relpath(path, dest)

    for locale in sorted(os.listdir(dest)):
        for field, limit in TEXT_LIMITS.items():
            path = os.path.join(dest, locale, f"{field}.txt")
            if not os.path.isfile(path):
                if locale == DEFAULT_LOCALE:
                    problems.append(f"{rel(path)} is missing")
                continue
            with open(path, encoding="utf-8") as f:
                text = f.read()
            if not text:
                problems.append(f"{rel(path)} is empty")
            elif len(text) > limit:
                problems.append(f"{rel(path)} is {len(text)} characters, Play takes {limit}")
            if field != "full_description" and "\n" in text:
                problems.append(f"{rel(path)} has a line break, Play wants one line")

    out = os.path.join(dest, DEFAULT_LOCALE)
    note = os.path.join(out, "changelogs", f"{code}.txt")
    with open(note, encoding="utf-8") as f:
        length = len(f.read())
    if length > CHANGELOG_LIMIT:
        problems.append(f"the release note is {length} characters, Play's \"What's new\" takes "
                        f"{CHANGELOG_LIMIT}: shorten it in packaging/linux/climat.metainfo.xml.in")

    # Play's rules for each picture: exact sizes for the icon and the feature
    # graphic, no transparency except in the icon, and for a phone screenshot
    # every side 320 to 3840 px with the long side at most twice the short one.
    images = os.path.join(out, "images")
    facts = png_facts(os.path.join(images, "icon.png"))
    if facts is None or facts[:2] != (512, 512):
        problems.append("images/icon.png must be a 512 by 512 PNG")
    if os.path.getsize(os.path.join(images, "icon.png")) > 1024 * 1024:
        problems.append("images/icon.png is over 1 MB")
    facts = png_facts(os.path.join(images, "featureGraphic.png"))
    if facts is None or facts[:2] != (1024, 500):
        problems.append("images/featureGraphic.png must be a 1024 by 500 PNG")
    elif facts[2]:
        problems.append("images/featureGraphic.png has an alpha channel, Play refuses it")

    shots = sorted(os.listdir(os.path.join(images, "phoneScreenshots")))
    if not 2 <= len(shots) <= 8:
        problems.append(f"{len(shots)} phone screenshots, Play takes 2 to 8")
    for name in shots:
        path = os.path.join(images, "phoneScreenshots", name)
        facts = png_facts(path)
        if facts is None:
            problems.append(f"{rel(path)} is not a PNG")
            continue
        width, height, alpha = facts
        if alpha:
            problems.append(f"{rel(path)} has an alpha channel, Play refuses it")
        if min(width, height) < 320 or max(width, height) > 3840:
            problems.append(f"{rel(path)} is {width} by {height}, every side must be 320 to 3840")
        if max(width, height) > 2 * min(width, height):
            problems.append(f"{rel(path)} is {width} by {height}, longer than 2:1")
        if os.path.getsize(path) > 8 * 1024 * 1024:
            problems.append(f"{rel(path)} is over 8 MB")

    for problem in problems:
        print(f"play-listing: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"play-listing: the listing fits Play's limits ({len(shots)} screenshots, "
          f"a {length}-character note for version code {code})")
    return 0


def main(argv):
    if len(argv) == 3 and argv[1] == "stage":
        code = stage(argv[2])
        print(f"play-listing: staged in {argv[2]} for version code {code}")
        return 0
    if len(argv) == 2 and argv[1] == "check":
        with tempfile.TemporaryDirectory() as dest:
            return check(dest, stage(dest))
    print(__doc__.split("\n\n")[1], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
