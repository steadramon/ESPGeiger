#!/usr/bin/env python3
# Makes src/EGTimeZoneTable.h and extras/zones.json, run on a new tzdb
# not a build step, tzdata from PyPI not the system tree

import argparse
import collections
import difflib
import json
import os
import struct
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

# must match EGTimeZone.cpp
FNV_PRIME = 16777619
OFFSET_BASIS = 2166136261
HASH_BITS = 17
RULE_BITS = 7
SEEDS = 256

SKIP_DIRS = {"right", "posix"}
SKIP_NAMES = {"posixrules", "Factory", "localtime"}

# footer = the rule after the last transition. Morocco's is 2087's, +01 till then;
# Palestine's (2086) is right outside Ramadan, kept
NOW_RULE = {"Africa/Casablanca": "<+01>-1", "Africa/El_Aaiun": "<+01>-1",
            "Asia/Gaza": "EET-2EEST,M3.4.4/50,M10.4.4/50",
            "Asia/Hebron": "EET-2EEST,M3.4.4/50,M10.4.4/50"}
Y2038 = 2145916800   # 2038-01-01 UTC

def fnv_hash(name, seed):
    h = OFFSET_BASIS ^ seed
    for b in name.encode():
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h & ((1 << HASH_BITS) - 1)


def pick_seed(zones, rules):
    """First seed where no two zones share a hash with different rules, and no
    rule typed in raw hits a zone with another rule."""
    for seed in range(SEEDS):
        by = {}
        ok = True
        for name, rule in zones.items():
            h = fnv_hash(name, seed)
            if by.setdefault(h, rule) != rule:
                ok = False
                break
        if ok and all(by.get(fnv_hash(r, seed), r) == r for r in rules):
            return seed, by
    sys.exit("no seed under %d; widen the hash here and in EGTimeZone.cpp" % SEEDS)


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


def last_transition(path):
    """The v2+ block's last transition time, or None."""
    with open(path, "rb") as f:
        data = f.read()
    isut, isstd, leap, n, types, chars = struct.unpack(">6l", data[20:44])
    v2 = 44 + n * 5 + types * 6 + chars + leap * 8 + isstd + isut
    n = struct.unpack(">l", data[v2 + 32:v2 + 36])[0]
    if not n:
        return None
    at = v2 + 44 + (n - 1) * 8
    return struct.unpack(">q", data[at:at + 8])[0]


def collect(root):
    zones = {}
    late = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            if fn in SKIP_NAMES or fn.startswith("+") or "." in fn:
                continue
            path = os.path.join(dirpath, fn)
            name = os.path.relpath(path, root)
            rule = posix_rule(path)
            if not rule:
                continue
            if name in NOW_RULE:
                rule = NOW_RULE[name]
            elif (last_transition(path) or 0) >= Y2038:
                late.append(name)
            zones[name] = rule
    if late:
        sys.exit("footer is a rule after 2037, add to NOW_RULE: %s" % ", ".join(sorted(late)))
    # os.walk order varies
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
    if len(rules) >= 1 << RULE_BITS:
        sys.exit("%d rules; the rule field is %d bits" % (len(rules), RULE_BITS))
    index = {r: i for i, r in enumerate(rules)}
    seed, by = pick_seed(zones, rules)

    names = collections.defaultdict(list)
    for n, r in zones.items():
        names[fnv_hash(n, seed)].append(n)
    entries = sorted((h, index[r], " ".join(sorted(names[h]))) for h, r in by.items())

    out = [LICENCE]
    add = out.append
    add("#ifndef EGTIMEZONE_TABLE_H")
    add("#define EGTIMEZONE_TABLE_H")
    add("")
    add("// IANA tzdb %s. Rerun tools/gen_timezones.py; never edit." % version)
    add("// Included only by EGTimeZone.cpp.")
    add("")
    add("// NUL separated; zones[] rule n is the nth")
    add("static const char TZ_RULES[] PROGMEM =")
    for rule in rules:
        add('  "%s\\0"' % rule)
    add("  ;")
    add("")
    add("static const uint32_t TZ_SEED = %d;" % seed)
    add("")
    add("// %d bit hash, %d bit rule, big endian" % (HASH_BITS, RULE_BITS))
    add("#define TZ(h, r) { (uint8_t)((h) >> 9), (uint8_t)((h) >> 1), (uint8_t)((h) << 7 | (r)) }")
    add("")
    add("// Sorted by hash for binary search.")
    add("PROGMEM static const uint8_t zones[][3] = {")
    width = max(len("TZ(%d, %d)," % (h, r)) for h, r, _ in entries)
    for h, r, name in entries:
        add("    %-*s // %s" % (width, "TZ(%d, %d)," % (h, r), name))
    add("};")
    add("")
    add("#undef TZ")
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
    print("device: %d zones, %d rows, %d unique rules" % (len(zones), len(entries), len(rules)))
    blob = sum(len(r) + 1 for r in rules)
    print("flash:  zones %d B + rule blob %d B = %d B"
          % (len(entries) * 3, blob, len(entries) * 3 + blob))
    return 0


if __name__ == "__main__":
    sys.exit(main())
