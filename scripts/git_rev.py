import subprocess

revision = ""
try:
    revision = (
        subprocess.check_output(["git", "describe", "--always"], stderr=subprocess.DEVNULL)
        .strip()
        .decode("utf-8")
    )
    # Version.h stamped by the release workflow
    changed = subprocess.call(
        ["git", "diff", "--quiet", "HEAD", "--", ".", ":!ESPGeiger/src/App/Version.h"],
        stderr=subprocess.DEVNULL) == 1
    # an untracked source still builds
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--", "ESPGeiger", "lib"],
        stderr=subprocess.DEVNULL).strip()
    if changed or untracked:
        revision += "-dirty"
except:
    pass

print("-DGIT_VERSION='\"%s\"'" % revision)
