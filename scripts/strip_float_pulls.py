# Drop "-u _printf_float" and "-u _scanf_float" from LINKFLAGS so the linker
# can dead-code-strip newlib's float printf/scanf chain when nothing references
# it. Saves ~12 KB on ESP8266 builds with no %f/%g/%e usage.
#
# Safe-by-construction: if future code does use %f, the linker pulls the chain
# in normally — no silent garbage like a strong stub override would cause.
#
# To re-enable framework default: remove this from extra_scripts in platformio.ini.
# ESP8266 only; ESP32 doesn't carry these flags.

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
