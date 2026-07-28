"""Fails the build if one of our IRAM functions can reach flash.

IRAM_ATTR keeps a function callable while the flash cache is unmapped, and a
flash-resident callee defeats that. A near call is `call0 <addr>`; IRAM to flash
is past call0 range, so the compiler emits `l32r` + `callx0`, or `l32r` + `jx`
for a tail call, and those forms have to be resolved. A plain `j` cannot reach
flash from IRAM. Scope is symbols from our own objects (src/ and lib*/, never
FrameworkArduino), which keeps the ~20 SDK and core cases out.

Handlers reached through a pointer are checked where they are registered: a
function address passed to attachInterrupt, EGTimer1::timer_start or the NMI
attach must itself be in IRAM. A pointer that is stored and passed on later is
not traced, and flash data reads (PROGMEM, rodata) are out of scope.

Run standalone: python3 scripts/check_iram_isr.py <build_dir> [tool-prefix]
"""

import bisect
import collections
import os
import re
import subprocess
import sys
import glob

IRAM_LO, IRAM_HI, FLASH_LO = 0x40100000, 0x40108000, 0x40200000

# Compiler-planted on every std::function::operator() error path; unreachable.
SKIP_TARGETS = {"std::__throw_bad_function_call()"}

# Calls that take a handler, which then runs in interrupt context.
REGISTER_RE = re.compile(r"attachInterrupt|EGTimer1::timer_start|NmiTimSetFunc|ets_isr_attach")


def _syms(nm, elf):
    out = subprocess.run([nm, "-C", "--defined-only", elf],
                         capture_output=True, text=True).stdout
    pairs = []
    for line in out.splitlines():
        p = line.split(None, 2)
        if len(p) == 3 and re.fullmatch(r"[0-9a-f]{8}", p[0]):
            pairs.append((int(p[0], 16), p[2]))
    pairs.sort()
    return pairs


def _walk_objs(d):
    objs = []
    for root, _, files in os.walk(d):
        objs += [os.path.join(root, f) for f in files if f.endswith(".o")]
    return objs


# Project code is src/ plus the build's copy of the repo's lib/. Anything else
# under lib*/ is framework or lib_deps code, fixable only by vendoring it.
def _obj_groups(build_dir):
    proj = os.path.abspath(os.path.join(build_dir, "..", "..", ".."))
    repo = os.path.join(proj, "lib")
    mine = set(os.listdir(repo)) if os.path.isdir(repo) else set()

    ours = _walk_objs(os.path.join(build_dir, "src"))
    ext = []
    for d in glob.glob(os.path.join(build_dir, "lib*")):
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            sub = os.path.join(d, name)
            if os.path.isdir(sub):
                (ours if name in mine else ext).extend(_walk_objs(sub))
    return ours, ext


def _defined(nm, objs):
    found = set()
    for i in range(0, len(objs), 200):
        out = subprocess.run([nm, "-C", "--defined-only"] + objs[i:i + 200],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            p = line.split(None, 2)
            if len(p) == 3 and p[1] in "TtWw":
                found.add(p[2])
    return found


def _literals(objdump, elf):
    words = {}
    row = re.compile(r"^ ([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ?){1,4})")
    out = subprocess.run([objdump, "-s", elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = row.match(line)
        if not m:
            continue
        addr, raw = int(m.group(1), 16), m.group(2).replace(" ", "")
        for i in range(0, len(raw) - 7, 8):
            b = raw[i:i + 8]
            words[addr + i // 2] = int(b[6:8] + b[4:6] + b[2:4] + b[0:2], 16)
    return words


def check(build_dir, prefix="xtensa-lx106-elf-"):
    elf = os.path.join(build_dir, "firmware.elf")
    if not os.path.isfile(elf):
        return 0, "no firmware.elf"
    objdump, nm = prefix + "objdump", prefix + "nm"

    our_objs, ext_objs = _obj_groups(build_dir)
    if not our_objs:
        return 0, "no object files, nothing to scope to"
    ours = _defined(nm, our_objs)
    ext = _defined(nm, ext_objs) - ours

    pairs = _syms(nm, elf)
    addrs = [a for a, _ in pairs]

    def sym_at(a):
        i = bisect.bisect_right(addrs, a) - 1
        return pairs[i][1] if i >= 0 else "0x%08x" % a

    words = _literals(objdump, elf)

    starts = set(addrs)

    fn_re = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
    l32r_re = re.compile(r"\tl32r\s+(a\d+), ([0-9a-f]+)")
    callx_re = re.compile(r"\t(?:callx0|jx)\s+(a\d+)")
    call0_re = re.compile(r"\tcall0\s+([0-9a-f]+)")
    # Any other instruction whose first operand is a register writes it, so a
    # literal loaded there earlier no longer holds (e.g. a function pointer
    # read with l32i before callx0). Stores, branches and jumps only read it.
    insn_re = re.compile(r"\t([a-z][a-z0-9.]*)\s+(a\d+)\b")
    reads_only = re.compile(r"^(s8i|s16i|s32i|s32e|b|j|call|ret|wsr|wur)")
    # call0 ABI: the callee may change a0 and a2-a11.
    clobbered = ["a0"] + ["a%d" % i for i in range(2, 12)]

    caller, group, mine, regs = None, None, False, {}
    bad = {"ours": collections.defaultdict(set), "ext": collections.defaultdict(set)}
    handlers = collections.defaultdict(set)
    scanned = 0

    out = subprocess.run([objdump, "-d", elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = fn_re.match(line)
        if m:
            a = int(m.group(1), 16)
            caller, regs = sym_at(a), {}
            mine = caller in ours
            group = None
            if IRAM_LO <= a < IRAM_HI:
                group = "ours" if mine else "ext" if caller in ext else None
            scanned += group == "ours"
            continue
        if group is None and not mine:
            continue
        m = l32r_re.search(line)
        if m:
            regs[m.group(1)] = words.get(int(m.group(2), 16))
            continue
        m = call0_re.search(line)
        tgt = int(m.group(1), 16) if m else None
        is_call = m is not None
        if tgt is None:
            m = callx_re.search(line)
            if m:
                tgt = regs.get(m.group(1))
                is_call = True
        if is_call and mine and tgt is not None and REGISTER_RE.search(sym_at(tgt)):
            for r in ("a2", "a3", "a4", "a5", "a6", "a7"):
                v = regs.get(r)
                if v is not None and v >= FLASH_LO and v in starts:
                    handlers[caller].add(sym_at(v))
        if is_call:
            for r in clobbered:
                regs.pop(r, None)
        else:
            m = insn_re.search(line)
            if m and not reads_only.match(m.group(1)):
                regs.pop(m.group(2), None)
        if group is None or tgt is None or tgt < FLASH_LO:
            continue
        target = sym_at(tgt)
        if target not in SKIP_TARGETS:
            bad[group][caller].add(target)

    def report(found, head):
        lines = [head % len(found)]
        for fn in sorted(found):
            lines.append("  %s" % fn)
            for t in sorted(found[fn]):
                lines.append("      -> %s" % t)
        return lines

    lines = []
    if bad["ours"]:
        lines += report(bad["ours"], "%d of our IRAM functions reach flash:")
    else:
        lines.append("%d of our IRAM functions, none reach flash" % scanned)
    if handlers:
        lines += report(handlers, "%d of our functions register a handler in flash:")
    if bad["ext"]:
        lines += report(bad["ext"], "%d outside repo lib/, upstream, not blocking:")
    return (1 if bad["ours"] or handlers else 0), "\n".join(lines)


try:
    Import("env")          # noqa: F821 - SCons injects this
    _IN_SCONS = True
except NameError:
    _IN_SCONS = False

if _IN_SCONS:
    if env.get("PIOPLATFORM") == "espressif8266":   # noqa: F821
        def _post_link(source, target, env):
            cc = env.subst("$CC")
            prefix = cc[:-3] if cc.endswith("gcc") else "xtensa-lx106-elf-"
            rc, msg = check(env.subst("$BUILD_DIR"), prefix)
            print("IRAM ISR check: " + msg)
            if rc:
                env.Exit(1)
        env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _post_link)   # noqa: F821
elif __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rc, msg = check(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "xtensa-lx106-elf-")
    print(msg)
    sys.exit(rc)
