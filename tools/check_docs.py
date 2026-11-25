#!/usr/bin/env python3
"""Checks that the documentation matches the code.

The code is the reference: src/main.cpp's ITEMS[] table lists the games
(GAME(...) and HEAVY(...) entries) and the apps (APP(...) entries); the Big
Games cartridge's table in src/cart.h is added when a heavy game is missing
from ITEMS[]. Then README.md, assets/*.svg and docs/*.html are scanned:

  * every count such as "60 games", "60 two-button games", "Games (60)",
    "12 apps", "12 pocket apps", "Pocket Apps (12)" or "10 tools" must match;
  * README's Games table must have exactly one row per game and its Pocket
    Apps table one row per app, in the device's alphabetical order.

Prints a report and exits 1 on any mismatch (the CI runs it before building).

  python3 tools/check_docs.py                    # counts from the code
  python3 tools/check_docs.py --expect-games 60  # a game is being added: expect 60
"""

import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def code_items():
    """(games, apps) as lists of names, from ITEMS[] (+ cart.h's heavy games)."""
    src = strip_comments(read("src/main.cpp"))
    m = re.search(r"\bITEMS\s*\[\s*\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        sys.exit("check_docs: ITEMS[] not found in src/main.cpp")
    table = m.group(1)
    games = re.findall(r'\b(?:GAME|HEAVY)\s*\(\s*"([^"]+)"', table)
    apps = re.findall(r'\bAPP\s*\(\s*"([^"]+)"', table)
    try:
        cart = strip_comments(read("src/cart.h"))
        for name in re.findall(r'\{\s*"([^"]+)"\s*,\s*"[a-z0-9_]+_hi"\s*\}', cart):
            if name not in games:
                games.append(name)
    except FileNotFoundError:
        pass
    return games, apps


def device_order(names):
    # the launcher sorts case-insensitively (digits come first)
    return sorted(names, key=lambda s: s.lower())


# ---------------- count mentions ----------------
WORD = r"(?:\*\*|[A-Za-z][\w'-]*,?|&amp;|&)"           # words allowed between the number and the noun
COUNT_PATTERNS = [
    ("games", re.compile(r"(?<![\w.])(\d+)\**\s+(?:%s\s+){0,5}?games\b" % WORD, re.I)),
    ("games", re.compile(r"\bgames\**\s*\((\d+)\)", re.I)),
    ("apps", re.compile(r"(?<![\w.])(\d+)\**\s+(?:%s\s+){0,2}?(?:apps|tools)\b" % WORD, re.I)),
    ("apps", re.compile(r"\bapps\**\s*\((\d+)\)", re.I)),
]


def count_mentions(text):
    """[(line number, kind, number, snippet)]"""
    out = []
    for no, line in enumerate(text.splitlines(), 1):
        seen = set()
        for kind, rx in COUNT_PATTERNS:
            for m in rx.finditer(line):
                key = (kind, m.start(1))
                if key in seen:
                    continue
                seen.add(key)
                out.append((no, kind, int(m.group(1)), m.group(0).strip()))
    return out


# ---------------- README tables ----------------
def section(text, heading_rx):
    m = re.search(r"^##\s+[^\n]*" + heading_rx + r"[^\n]*$", text, re.M | re.I)
    if not m:
        return None
    nxt = re.search(r"^##\s", text[m.end():], re.M)
    return text[m.end(): m.end() + nxt.start() if nxt else len(text)]


def table_names(sec):
    names = []
    for line in sec.splitlines():
        m = re.match(r"\|\s*\*\*(.+?)\*\*", line)
        if m:
            names.append(m.group(1).strip())
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--expect-games", type=int, help="override the game count (a game not merged yet)")
    ap.add_argument("--expect-apps", type=int, help="override the app count")
    args = ap.parse_args()

    games, apps = code_items()
    n_games = args.expect_games if args.expect_games is not None else len(games)
    n_apps = args.expect_apps if args.expect_apps is not None else len(apps)
    expected = {"games": n_games, "apps": n_apps}
    errors, notes = [], []

    print("Code: %d games, %d apps (src/main.cpp ITEMS[] + src/cart.h)" % (len(games), len(apps)))
    if n_games != len(games):
        notes.append("--expect-games %d (the code has %d)" % (n_games, len(games)))
    if n_apps != len(apps):
        notes.append("--expect-apps %d (the code has %d)" % (n_apps, len(apps)))

    files = ["README.md"] + sorted(
        os.path.relpath(p, ROOT).replace(os.sep, "/")
        for p in glob.glob(os.path.join(ROOT, "assets", "*.svg")) + glob.glob(os.path.join(ROOT, "docs", "*.html")))
    checked = 0
    for rel in files:
        text = read(rel)
        for no, kind, num, snip in count_mentions(text):
            checked += 1
            if num != expected[kind]:
                errors.append("%s:%d: says %d %s, the code has %d: \"%s\"" % (rel, no, num, kind, expected[kind], snip))
    print("Checked %d count mentions in %d files" % (checked, len(files)))

    readme = read("README.md")
    for title, rx, names, want in (("Games", r"\bGames\b", games, n_games), ("Pocket Apps", r"\bApps\b", apps, n_apps)):
        sec = section(readme, rx)
        if sec is None:
            errors.append("README.md: no \"## ... %s\" section" % title)
            continue
        rows = table_names(sec)
        print("README %s table: %d rows" % (title, len(rows)))
        for dup in sorted({r for r in rows if rows.count(r) > 1}):
            errors.append("README.md %s table: \"%s\" has %d rows" % (title, dup, rows.count(dup)))
        missing = [n for n in names if n not in rows]
        extra = [r for r in rows if r not in names]
        for n in missing:
            errors.append("README.md %s table: no row for \"%s\"" % (title, n))
        pending = want - len(names)           # rows allowed ahead of the code (--expect-*)
        for r in extra:
            if pending > 0:
                notes.append("README.md %s table: \"%s\" is not in the code yet (expected)" % (title, r))
                pending -= 1
            else:
                errors.append("README.md %s table: \"%s\" is not in the code" % (title, r))
        if len(set(rows)) != want:
            errors.append("README.md %s table: %d different rows, expected %d" % (title, len(set(rows)), want))
        if rows != device_order(rows):
            errors.append("README.md %s table: rows are not in alphabetical order (device order: %s)"
                          % (title, ", ".join(device_order(rows))))

    for n in notes:
        print("note: " + n)
    if errors:
        print("\nThe docs do not match the code (%d problem%s):" % (len(errors), "s" if len(errors) > 1 else ""))
        for e in errors:
            print("  - " + e)
        print("\nFix README.md / docs / assets (re-render assets/tigo-logo.png after editing its SVG).")
        return 1
    print("OK: the docs match the code (%d games, %d apps)." % (n_games, n_apps))
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")
    except AttributeError:
        pass
    sys.exit(main())
