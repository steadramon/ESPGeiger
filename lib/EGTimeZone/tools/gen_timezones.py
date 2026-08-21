#!/usr/bin/env python3
# Regenerates EGTimeZoneTable.h and the ESPGeiger dropdown list from the IANA tz
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
import os
import re
import sys

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(LIB))
HEADER = os.path.join(LIB, "src", "EGTimeZoneTable.h")
NTP_CPP = os.path.join(REPO, "ESPGeiger", "src", "NTP", "NTP.cpp")
ANCHOR = "var L={"

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

# Must match timezones.cpp.
FNV_PRIME = 16777619
OFFSET_BASIS = 2166136261
HASH_MASK = 0x1FFFFF

SKIP_DIRS = {"right", "posix"}
SKIP_NAMES = {"posixrules", "Factory", "localtime"}

# Country to dropdown region. Derived once from UN M49 via pycountry-convert so
# the generator keeps tzdata as its only dependency. A code in neither this nor
# OVERRIDE is a hard error, so a new country cannot land in the wrong group.
REGION_OF = {
    "AD":"eu", "AE":"as", "AF":"as", "AL":"eu", "AM":"as", "AR":"sa",
    "AS":"pa", "AT":"eu", "AZ":"as", "BB":"na", "BD":"as", "BE":"eu",
    "BG":"eu", "BM":"na", "BO":"sa", "BR":"sa", "BT":"as", "BY":"eu",
    "BZ":"na", "CA":"na", "CH":"eu", "CI":"af", "CK":"pa", "CL":"sa",
    "CN":"as", "CO":"sa", "CR":"na", "CU":"na", "CV":"af", "CY":"as",
    "CZ":"eu", "DE":"eu", "DO":"na", "DZ":"af", "EC":"sa", "EE":"eu",
    "EG":"af", "ES":"eu", "FI":"eu", "FJ":"pa", "FK":"sa", "FM":"pa",
    "FO":"eu", "FR":"eu", "GB":"eu", "GE":"as", "GF":"sa", "GI":"eu",
    "GR":"eu", "GS":"sa", "GT":"na", "GU":"pa", "GW":"af", "GY":"sa",
    "HK":"as", "HN":"na", "HT":"na", "HU":"eu", "ID":"as", "IE":"eu",
    "IL":"as", "IN":"as", "IO":"as", "IQ":"as", "IR":"as", "IT":"eu",
    "JM":"na", "JO":"as", "JP":"as", "KE":"af", "KG":"as", "KI":"pa",
    "KP":"as", "KR":"as", "KZ":"as", "LB":"as", "LK":"as", "LR":"af",
    "LT":"eu", "LV":"eu", "LY":"af", "MA":"af", "MD":"eu", "MH":"pa",
    "MM":"as", "MN":"as", "MO":"as", "MQ":"na", "MT":"eu", "MU":"af",
    "MV":"as", "MX":"na", "MY":"as", "MZ":"af", "NA":"af", "NC":"pa",
    "NF":"pa", "NG":"af", "NI":"na", "NP":"as", "NR":"pa", "NU":"pa",
    "NZ":"pa", "PA":"na", "PE":"sa", "PF":"pa", "PG":"pa", "PH":"as",
    "PK":"as", "PL":"eu", "PM":"na", "PR":"na", "PS":"as", "PT":"eu",
    "PW":"pa", "PY":"sa", "QA":"as", "RO":"eu", "RS":"eu", "RU":"eu",
    "SA":"as", "SB":"pa", "SD":"af", "SG":"as", "SR":"sa", "SS":"af",
    "ST":"af", "SV":"na", "SY":"as", "TC":"na", "TD":"af", "TH":"as",
    "TJ":"as", "TK":"pa", "TM":"as", "TN":"af", "TO":"pa", "TW":"as",
    "UA":"eu", "US":"na", "UY":"sa", "UZ":"as", "VE":"sa", "VN":"as",
    "VU":"pa", "WS":"pa", "ZA":"af",
}

# Where M49 disagrees with where a user looks for their zone.
OVERRIDE = {
    "AU": "au",   # its own group rather than lumped into Oceania
    "GL": "eu",   # Greenland is Denmark
    "TR": "eu",   # Europe/Istanbul
    "AQ": "aq", "EH": "af", "PN": "pa", "TL": "as",   # absent from M49
}

# Continent population descending (UN WPP 2024): likeliest regions first.
REGIONS = [("as", "Asia"), ("af", "Africa"), ("eu", "Europe"),
           ("na", "North America"), ("sa", "South America"), ("au", "Australia"),
           ("pa", "Pacific"), ("aq", "Antarctica")]


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


def etc_order(zone):
    """Offset order, then aliases. GMT, GMT+0 and GMT-0 are all zero, so the
    name breaks the tie rather than leaving it to input order."""
    tail = zone[4:]
    m = re.match(r"GMT([+-])(\d+)$", tail)
    if m:
        n = int(m.group(2))
        return (0, -n if m.group(1) == "-" else n, tail)
    return (0, 0, tail) if tail == "GMT" else (1, 0, tail)


def ui_groups(root, zones):
    """Dropdown contents: canonical zones by region, then the fixed offsets."""
    by_region = collections.defaultdict(list)
    for line in open(os.path.join(root, "zone1970.tab"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        fields = line.rstrip("\n").split("\t")
        code, name = fields[0].split(",")[0], fields[2]
        # A research station belongs to Antarctica whoever operates it.
        region = "aq" if name.startswith("Antarctica/") else (
            OVERRIDE.get(code) or REGION_OF.get(code))
        if not region:
            sys.exit("country %s (%s) is in no region; add it to OVERRIDE" % (code, name))
        by_region[region].append(name)

    groups = [(label, sorted(by_region[key])) for key, label in REGIONS if by_region[key]]
    groups.append(("Etc", sorted((z for z in zones if z.startswith("Etc/")), key=etc_order)))

    listed = [z for _, zs in groups for z in zs]
    missing = [z for z in listed if z not in zones]
    if missing:
        sys.exit("dropdown would offer unresolvable zones: %s" % missing)
    return groups, listed


def write_js(groups):
    # region -> prefix -> tails; the page rebuilds the name. Smaller than
    # repeating the prefix on every entry.
    body = []
    for label, zs in groups:
        if not zs:
            continue
        bucket = collections.OrderedDict()
        for z in zs:
            head, _, tail = z.partition("/")
            bucket.setdefault(head, []).append(tail or z)
        body.append('"%s":{%s}' % (label, ",".join(
            '"%s":[%s]' % (head, ",".join('"%s"' % t for t in tails))
            for head, tails in bucket.items())))
    body = ",".join(body)
    # Replaces the one-line var L={...}; declaration. No marker comments: that
    # raw string is served to the browser.
    out = []
    for line in open(NTP_CPP).read().splitlines(True):
        out.append(ANCHOR + body + "};\n" if line.startswith(ANCHOR) else line)
    return "".join(out)


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
        sys.exit("hash collision; widen the hash field here and in timezones.cpp")

    out = [LICENCE]
    add = out.append
    add("#ifndef EGTIMEZONE_TABLE_H")
    add("#define EGTIMEZONE_TABLE_H")
    add("")
    add("// IANA tzdb %s. Rerun scripts/gen_timezones.py; never edit." % version)
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

    groups, listed = ui_groups(root, zones)
    js = write_js(groups)

    if args.check:
        stale = []
        for name, want, path in (("EGTimeZoneTable.h", text, HEADER),
                                 ("NTP.cpp", js, NTP_CPP)):
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

    open(HEADER, "w").write(text)
    open(NTP_CPP, "w").write(js)
    print("tzdb %s" % version)
    print("device: %d zones, %d unique rules" % (len(entries), len(rules)))
    blob = sum(len(r) + 1 for r in rules)
    print("flash:  zones %d B + rule blob %d B = %d B (offset field allows 2048)"
          % (len(entries) * 4, blob, len(entries) * 4 + blob))
    print("dropdown: %d zones in %d groups" % (len(listed), len(groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
