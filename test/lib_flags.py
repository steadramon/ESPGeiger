# u8g2 C core for test_oled, as it ships, no -Werror
import os

Import("env")

if env.get("PIOTEST_RUNNING_NAME") == "test_oled":
    src = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "U8g2", "src")
    env.Append(CPPPATH=[src])
    env.Append(CPPDEFINES=["U8G2_USE_DYNAMIC_ALLOC"])   # as the firmware
    u8g2 = env.Clone()
    u8g2.Replace(CCFLAGS=["-w"])
    u8g2.BuildSources("$BUILD_DIR/u8g2", src, "+<clib/*.c>")
