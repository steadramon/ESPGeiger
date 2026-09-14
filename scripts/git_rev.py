import subprocess

revision = ""
try:
    revision = (
        # Without --dirty a modified tree reports the last commit, which is not
        # the code that ran.
        subprocess.check_output(["git", "describe", "--always", "--dirty"], stderr=subprocess.DEVNULL)
        .strip()
        .decode("utf-8")
    )
except:
    pass

print("-DGIT_VERSION='\"%s\"'" % revision)