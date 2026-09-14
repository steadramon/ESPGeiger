#!/usr/bin/env python3
# Regenerates the timezone dropdown in ESPGeiger/src/NTP/NTP.cpp from
# lib/EGTimeZone/extras/zones.json. NOT a build step: rerun after
# lib/EGTimeZone/tools/gen_timezones.py, review the diff, commit.

import argparse
import collections
import difflib
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZONES_JSON = os.path.join(REPO, "lib", "EGTimeZone", "extras", "zones.json")
NTP_CPP = os.path.join(REPO, "ESPGeiger", "src", "NTP", "NTP.cpp")
ANCHOR = "var L={"

# Country to dropdown region. Derived once from UN M49 via pycountry-convert.
# A code in neither this nor OVERRIDE is a hard error, so a new country cannot
# land in the wrong group.
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


def etc_order(zone):
    """Offset order, then aliases. GMT, GMT+0 and GMT-0 are all zero, so the
    name breaks the tie rather than leaving it to input order."""
    tail = zone[4:]
    m = re.match(r"GMT([+-])(\d+)$", tail)
    if m:
        n = int(m.group(2))
        return (0, -n if m.group(1) == "-" else n, tail)
    return (0, 0, tail) if tail == "GMT" else (1, 0, tail)


def ui_groups(doc):
    """Dropdown contents: canonical zones by region, then the fixed offsets."""
    zones = set(doc["zones"])
    by_region = collections.defaultdict(list)
    for z in doc["canonical"]:
        code, name = z["countries"][0], z["name"]
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


def report(have, want):
    """Point at the offset: the data is one very long line."""
    hl, wl = have.splitlines(), want.splitlines()
    for line in difflib.unified_diff(hl, wl, "committed", "generated",
                                     lineterm="", n=0):
        print("NTP.cpp: %s" % line[:120], file=sys.stderr)
    for a, b in zip(hl, wl):
        if a == b:
            continue
        i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]),
                 min(len(a), len(b)))
        print("NTP.cpp: first difference at column %d of %d/%d" % (i, len(a), len(b)),
              file=sys.stderr)
        print("NTP.cpp:   committed ...%s..." % a[max(0, i - 30):i + 40], file=sys.stderr)
        print("NTP.cpp:   generated ...%s..." % b[max(0, i - 30):i + 40], file=sys.stderr)
        break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the committed dropdown is stale")
    args = ap.parse_args()

    doc = json.load(open(ZONES_JSON))
    groups, listed = ui_groups(doc)
    js = write_js(groups)

    if args.check:
        have = open(NTP_CPP).read()
        if js != have:
            report(have, js)
            print("stale, rerun scripts/gen_tz_dropdown.py", file=sys.stderr)
            return 1
        print("up to date (tzdb %s)" % doc["tzdb"])
        return 0

    open(NTP_CPP, "w").write(js)
    print("tzdb %s" % doc["tzdb"])
    print("dropdown: %d zones in %d groups" % (len(listed), len(groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
