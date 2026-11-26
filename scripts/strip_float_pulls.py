# drop -u _printf_float/_scanf_float, ~12 KB on esp8266
# %f in use still pulls the chain in

Import("env")

if env.get("PIOPLATFORM") != "espressif8266":
    Return()

_SKIP = {"_printf_float", "_scanf_float"}

flags = list(env.get("LINKFLAGS", []))
out = []
i = 0
while i < len(flags):
    if i + 1 < len(flags) and flags[i] == "-u" and flags[i + 1] in _SKIP:
        i += 2
        continue
    out.append(flags[i])
    i += 1

env.Replace(LINKFLAGS=out)
