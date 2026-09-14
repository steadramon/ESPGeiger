#!/usr/bin/env python3
# Regenerates src/EGTimeZoneTable.h and extras/zones.json from the IANA tz
# database. NOT a build step: run it when tzdb releases, review the diff, commit.
#
# Source is the `tzdata` PyPI package, never the system zoneinfo tree, which
# varies per machine and per OS vendor.
#
# Aliases are emitted too. Names are not stored on device, only a hash and a
# rule offset, so full coverage is nearly free and every legacy name a user
# might carry keeps resolving across a tzdb rename.

import argparse
import collections
import difflib
import json
import os
import sys

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(LIB, "src", "EGTimeZoneTable.h")
ZONES_JSON = os.path.join(LIB, "extras", "zones.json")

LICENCE = """/*
  EGTimeZoneTable.h - Generated from the IANA tz database.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/"""

# Must match EGTimeZone.cpp.
FNV_PRIME = 16777619
OFFSET_BASIS = 2166136261
HASH_MASK = 0x1FFFFF

SKIP_DIRS = {"right", "posix"}
SKIP_NAMES = {"posixrules", "Factory", "localtime"}

def fnv_hash(name):
    h = OFFSET_BASIS
    for b in name.encode():
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h & HASH_MASK


def find_source():
    try:
        import tzdata
        from importlib.resources import files
    except ImportError:
        sys.exit("pip install tzdata (the system zoneinfo tree is not used: it "
                 "differs per machine)")
    return str(files("tzdata") / "zoneinfo"), tzdata.IANA_VERSION


def posix_rule(path):
    """The POSIX TZ rule in a TZif v2+ footer, or None."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"TZif") or data[4:5] == b"\0":
        return None                      # v1 files carry no footer
    parts = data.rsplit(b"\n", 2)
    if len(parts) != 3 or not parts[1]:
        return None
    return parts[1].decode("ascii")


def collect(root):
    zones = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            if fn in SKIP_NAMES or fn.startswith("+") or "." in fn:
                continue
            path = os.path.join(dirpath, fn)
            name = os.path.relpath(path, root)
            rule = posix_rule(path)
            if rule:
                zones[name] = rule
    # Sorted: os.walk order is platform-dependent.
    return dict(sorted(zones.items()))


def canonical(root):
    """Canonical zones with their country codes, from zone1970.tab."""
    out = []
    for line in open(os.path.join(root, "zone1970.tab"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        fields = line.rstrip("\n").split("\t")
        out.append({"name": fields[2], "countries": fields[0].split(",")})
    return sorted(out, key=lambda z: z["name"])


def render_json(zones, root, version):
    doc = {"tzdb": version,
           "zones": sorted(zones),
           "canonical": canonical(root)}
    return json.dumps(doc, indent=1) + "\n"


def render(zones, version):
    rules = sorted(set(zones.values()))
    offset, at = {}, 0
    for rule in rules:
        offset[rule] = at
        at += len(rule) + 1
    if at > 2048:
        sys.exit("rule blob is %d B; the offset field is 11 bits" % at)

    entries = sorted((fnv_hash(n), offset[r], n) for n, r in zones.items())
    dupes = collections.Counter(h for h, _, _ in entries)
    clash = [h for h, c in dupes.items() if c > 1]
    if clash:
        for h in clash:
            print("collision %d: %s" % (h, [n for x, _, n in entries if x == h]),
                  file=sys.stderr)
        sys.exit("hash collision; widen the hash field here and in EGTimeZone.cpp")

    out = [LICENCE]
    add = out.append
    add("#ifndef EGTIMEZONE_TABLE_H")
    add("#define EGTIMEZONE_TABLE_H")
    add("")
    add("// IANA tzdb %s. Rerun tools/gen_timezones.py; never edit." % version)
    add("// Included only by EGTimeZone.cpp.")
    add("")
    add("// NUL separated; zones[].offset indexes into this.")
    add("static const char TZ_RULES[] PROGMEM =")
    for rule in rules:
        add('  "%s\\0"' % rule)
    add("  ;")
    add("")
    add("// Sorted by hash for binary search.")
    add("PROGMEM static const struct TZoneH {")
    add("  uint32_t hash   : 21;")
    add("  uint32_t offset : 11;")
    add("} zones[] = {")
    width = max(len("{%d, %d}," % (h, o)) for h, o, _ in entries)
    for h, o, name in entries:
        add("    %-*s // %s" % (width, "{%d, %d}," % (h, o), name))
    add("};")
    add("")
    add("#endif")
    return "\n".join(out) + "\n", rules, entries


def report(name, have, want):
    """Point at the offset: the data is one very long line."""
    hl, wl = have.splitlines(), want.splitlines()
    for line in difflib.unified_diff(hl, wl, "committed", "generated",
                                     lineterm="", n=0):
        print("%s: %s" % (name, line[:120]), file=sys.stderr)
    for a, b in zip(hl, wl):
        if a == b:
            continue
        i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]),
                 min(len(a), len(b)))
        print("%s: first difference at column %d of %d/%d" % (name, i, len(a), len(b)),
              file=sys.stderr)
        print("%s:   committed ...%s..." % (name, a[max(0, i - 30):i + 40]), file=sys.stderr)
        print("%s:   generated ...%s..." % (name, b[max(0, i - 30):i + 40]), file=sys.stderr)
        break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the committed header is stale")
    args = ap.parse_args()

    root, version = find_source()
    zones = collect(root)
    if not zones:
        sys.exit("no zones found under " + root)

    text, rules, entries = render(zones, version)
    data = render_json(zones, root, version)

    if args.check:
        stale = []
        for name, want, path in (("EGTimeZoneTable.h", text, HEADER),
                                 ("zones.json", data, ZONES_JSON)):
            have = open(path).read()
            if want == have:
                continue
            stale.append(name)
            report(name, have, want)
        if stale:
            print("stale, rerun lib/EGTimeZone/tools/gen_timezones.py: %s"
                  % ", ".join(stale), file=sys.stderr)
            return 1
        print("up to date (tzdb %s)" % version)
        return 0

    os.makedirs(os.path.dirname(ZONES_JSON), exist_ok=True)
    open(HEADER, "w").write(text)
    open(ZONES_JSON, "w").write(data)
    print("tzdb %s" % version)
    print("device: %d zones, %d unique rules" % (len(entries), len(rules)))
    blob = sum(len(r) + 1 for r in rules)
    print("flash:  zones %d B + rule blob %d B = %d B (offset field allows 2048)"
          % (len(entries) * 4, blob, len(entries) * 4 + blob))
    return 0


if __name__ == "__main__":
    sys.exit(main())
