"""Fails the build when more modules register than EG_MAX_MODULES holds.

A module past the limit is dropped at boot with only a log line. The count comes
from the compiler's own preprocessor, with the flags the sources build with, so
every #if is judged the way the build judges it. Warns at the limit.
"""

import os
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor

Import("env", "projenv")   # noqa: F821 - SCons injects these

REG_RE = re.compile(r"\bEGModuleRegistry::add\s*\(\s*&\s*(\w+)")
MAX_RE = re.compile(r"#\s*define\s+EG_MAX_MODULES\s+(\d+)")
MARKS = ("EG_REGISTER_MODULE", "EGModuleRegistry::add")


def _files(src, ext):
    for root, _, files in os.walk(src):
        for f in files:
            if f.endswith(ext):
                yield os.path.join(root, f)


def _mentions(path):
    with open(path, errors="replace") as fh:
        text = fh.read()
    return any(m in text for m in MARKS)


# a header that registers reaches any .cpp, then scan them all
def _reg_h(src):
    return os.path.join(src, "src", "Corium", "Module", "EGModuleRegistry.h")


def _sources(src):
    reg_h = _reg_h(src)
    any_h = any(_mentions(h) for h in _files(src, ".h") if h != reg_h)
    return [p for p in _files(src, ".cpp") if any_h or _mentions(p)]


def _max(src):
    for d in projenv.get("CPPDEFINES", []):   # noqa: F821
        if isinstance(d, (tuple, list)) and d[0] == "EG_MAX_MODULES":
            return int(d[1])
    with open(_reg_h(src)) as fh:
        m = MAX_RE.search(fh.read())
    return int(m.group(1))


def _registered(cmd, path):
    r = subprocess.run(cmd + [path], capture_output=True, text=True)
    if r.returncode:
        return path, None, r.stderr.strip().splitlines()[-1:]
    return path, REG_RE.findall(r.stdout), None


def _check(source, target, env):
    src = projenv.subst("$PROJECT_SRC_DIR")   # noqa: F821
    # subst_list keeps a define with spaces in one argument
    cmd = [str(a) for a in projenv.subst_list("$CXX -E -P $CXXFLAGS $CCFLAGS $_CCCOMCOM")[0]]   # noqa: F821
    cmd = [a for a in cmd if a not in ("-MMD", "-MD")]   # else a .d file lands in the project root
    cap = _max(src)
    names = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        for path, found, err in pool.map(lambda p: _registered(cmd, p), _sources(src)):
            if found is None:
                print("Modules: can't preprocess %s %s" % (path, err))
                env.Exit(1)
            names += found
    print("Modules: %d of %d (%s)" % (len(names), cap, " ".join(sorted(names))))
    if len(names) > cap:
        print("Modules: over EG_MAX_MODULES, %d would be dropped at boot" % (len(names) - cap))
        env.Exit(1)
    if len(names) == cap:
        msg = "Modules: at EG_MAX_MODULES, no room for another"
        if os.environ.get("GITHUB_ACTIONS"):
            msg = "::warning::" + msg
        print(msg)


env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", _check)   # noqa: F821
