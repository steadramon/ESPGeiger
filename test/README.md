# Host tests

Unit tests for the plain logic in the firmware, built and run on your computer
with PlatformIO. No board needed.

```
pio test -c test.ini -e native                    all suites
pio test -c test.ini -e native -f test_msgpack    one suite
pio test -c test.ini -e native_asan               all suites, ASan + UBSan
```

`-c test.ini` is needed: the test envs are not part of `platformio.ini`.

## Layout

```
test/
  shim/          small fake Arduino.h, pgmspace.h, Print.h
  test_<name>/
    test_<name>.cpp    the Unity suite, owns main()
    prod_<unit>.cpp    optional: #includes the firmware .cpp under test
```

## Adding a suite

1. Make `test/test_<name>/test_<name>.cpp` with `setUp`, `tearDown` and a
   `main()` that calls `RUN_TEST` for each case.
2. If the code under test lives in a `.cpp`, add a one-line
   `prod_<unit>.cpp` that `#include`s it.
3. Run it with `pio test -c test.ini -e native -f test_<name>`.

The shim's `millis()` and `micros()` only move when a test moves them
(`eg_clock_advance_ms`, `eg_clock_set_us`); call `eg_clock_reset()` from
`setUp()`.
