# Host-native unit tests

Pure-logic tests for the fundamentals, run locally and in GitHub Actions. No
hardware, no firmware link, no simulator.

```
pio test -c test.ini -e native                    all suites
pio test -c test.ini -e native -f test_msgpack    one suite
pio test -c test.ini -e native_asan               all suites, ASan + UBSan
```

The `-c test.ini` is required. `test.ini` is not in `platformio.ini`'s
`extra_configs`, so `[env:native]` never joins a `pio run`.

## What belongs here

Logic that has bitten us and is host-reproducible. Prefer table-driven boundary
cases over volume: rollover tests straddle the exact wrap value, encoder tests
compare golden bytes. Modules with no logic (pin wiggling, registers) get no
host test. Coverage numbers are not a goal.

Out of scope, and passing here says nothing about them: AsyncTCP/MQTT
lifecycle, WiFi and portal flows, OTA, icache and LPS, PROGMEM placement,
anything needing real interrupts.

## Layout

```
test/
  shim/          fake Arduino.h, pgmspace.h, Print.h
  test_<name>/
    test_<name>.cpp    the Unity suite, owns main()
    prod_<unit>.cpp    optional: #includes the production .cpp under test
```

Each suite is its own binary. A suite needing a production `.cpp` pulls it in
through a one-line `prod_*.cpp`, so the test sees only the header a caller sees.

`build_src_filter = -<*>` keeps `src_dir` out of the build. The library
dependency finder stays in chain mode and follows what a test includes; a
header-only library with headers at its directory root rather than under `src/`
needs an explicit `-I` in `test.ini`.

## The shim

`test/shim/Arduino.h` replaces the core's. Header-only: PlatformIO does not
compile a non-suite directory under `test_dir` into each test binary.

Keep it thin. If faking a dependency needs more code than the unit under test,
extract a seam instead. Absent by design: `String`, `Stream`, `Serial`, WiFi,
EEPROM.

`millis()`/`micros()` come from a fake clock that moves only when a test moves
it (`eg_clock_advance_ms`, `eg_clock_set_us`). Call `eg_clock_reset()` from
`setUp()`.

## Host/target ABI

Two divergences, and they fail in opposite directions.

**`unsigned long` is 64-bit on the host, 32-bit on target.** Rollover bugs
cannot reproduce, so a test against an `unsigned long` seam passes for the
wrong reason. Seams under host test use explicit `uint32_t`, pinned with
`EG_ASSERT_32BIT`. The shim's `millis()` returns `uint32_t` for the same reason.

**`char` is signed on the host, unsigned on every ESP toolchain.** A byte held
in a plain `char` widens differently, so a test fails on correct code.
`test.ini` passes `-fno-signed-char`.

Canaries for both live in `test_hostmodel`. `size_t` (8 vs 4 bytes) is a third
candidate, not yet audited.

A `-m32` CI job would make the width case belt-and-braces. Needs `gcc-multilib`,
Linux only, worth adding once M2 lands.

## Tests assert correct behaviour

Default: a test says what the code *should* do. If the code is wrong, fix the
code. Pinning a defect turns a bug into a specification.

Two exceptions, each named in a comment:

- **`CONTRACT`** — deliberate API behaviour that looks surprising but is right
  (`Sha256::result()` finalising once; the shared `Sha256` needing `init()`).
  Assert these: a silent change breaks callers.
- **`CHARACTERISATION`** — vendored code we do not own, described before
  deciding whether to replace it (CircularBuffer against EGRingAvg).

Anything else broken gets fixed, or gets a test asserting correct behaviour
wrapped in `TEST_IGNORE_MESSAGE` so the run reports IGNORED rather than passing.
A green suite must never mean "we decided to live with it".
