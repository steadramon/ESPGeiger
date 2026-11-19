# Host tests

Unity tests for the plain logic (counting maths, serial parsing, formats), built
and run on your computer with PlatformIO. No board needed.

```
pio test -c test.ini -e native                        all suites
pio test -c test.ini -e native -f app/test_msgpack    one suite
```

`-c test.ini` is needed: the test env is not part of `platformio.ini`.

A suite is `test/<group>/test_<name>/test_<name>.cpp` with `setUp`, `tearDown`
and a `main()` that calls `RUN_TEST` for each case. Code that lives in a `.cpp`
comes in through a one-line `prod_<unit>.cpp` (`#include "ESPGeiger/src/..."`).
