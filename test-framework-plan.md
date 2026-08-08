# ESPGeiger test framework - plan and status

Branch: `feat/test-framework` (from main @ 03e0ca10). Host-native unit tests for
pure-logic fundamentals, runnable locally and in GitHub Actions. Nothing here
targets hardware; on-device truths (icache/LPS, PROGMEM strictness, AsyncTCP
lifecycle, timing) stay with the bench device and the chaos/endpoint scripts.

## STATUS

**10 suites, 134 cases, green plain and under ASan/UBSan in ~7s. Uncommitted.**

```
pio test -c test.ini -e native                    all suites
pio test -c test.ini -e native -f test_msgpack    one suite
pio test -c test.ini -e native_asan               ASan + UBSan
```

Suites: `test_hostmodel` (ABI canaries + fake clock), `test_base64`,
`test_msgpack`, `test_ringavg`, `test_circularbuffer`, `test_stringutil`,
`test_uptime`, `test_sha256`, `test_grng`, `test_mqtt_packet`.

Uncommitted, in three intended commits:

1. **Framework** - `test/`, `test.ini`, `.github/workflows/test.yml`. No
   firmware touched.
2. **Testability seams** - `Util/UptimeCounter.h` (new), `NTP/NTP.h`,
   `GRNG/GRNG.cpp` + `GRNG/GRNGWeb.cpp` (new), `Util/AsyncHTTPAnchor.cpp`
   (new), `ESPGeiger.cpp`. Behaviour-neutral; flash -388 B (8266), -324 B
   (esp32).
3. **Fixes** - `EGBase64.cpp`, `WebAPI/MsgPack.h`, `Util/StringUtil.h`,
   `GRNG/sha256.cpp`, `async-mqtt-client/.../PublishPacket.{cpp,hpp}`.

Not mine, leave alone: `OLEDDisplay/fonts.h`, `src/LoRa/`.

`.github/workflows/test.yml` is written but **has never run** - push and watch.

## Philosophy

- Test the logic that has actually bitten us. Every host-reproducible decoded
  crash or fixed bug gets a regression test.
- Table-driven boundary cases over volume. Rollover tests straddle the exact
  wrap value, encoder tests compare golden bytes.
- No coverage targets. A module with no logic gets no host test.
- Keep the shim thin. If faking a dependency needs more code than the unit
  under test, extract a seam instead.
- **Tests assert correct behaviour, not current behaviour.** See
  `test/README.md`; only `CONTRACT` and `CHARACTERISATION` may pin what is.

## Framework decisions taken

- `test.ini` is standalone, deliberately **not** in `platformio.ini`'s
  `extra_configs`, so `pio run` never sees `[env:native]`.
- The shim is **header-only**: PlatformIO does not compile a non-suite
  directory under `test_dir` into each test binary, so a shim `.cpp` never
  links.
- `lib_ldf_mode = off` also hides Unity's own headers. Chain mode, plus an
  explicit `-I` for header-only libs whose headers sit at the directory root.
- A suite needing a production `.cpp` pulls it in via a one-line `prod_*.cpp`.
- `[env:native_asan]` works out of the box; no extra link flags.
- Unity's `TEST_ASSERT_EQUAL_*_ARRAY` fails on length 0.

## Host/target ABI divergences - both make tests lie

1. **`unsigned long`**: 64-bit host, 32-bit target. Rollover bugs cannot
   reproduce -> **false negatives**. Seams under host test use explicit
   `uint32_t`, pinned with `EG_ASSERT_32BIT`. The shim's `millis()` returns
   `uint32_t` for the same reason.
2. **`char` signedness**: signed on host, **unsigned on every ESP toolchain**
   (verified `gcc -dM -E` on xtensa-lx106, xtensa-esp32, riscv32-esp) -> a test
   **fails on correct code**. `test.ini` passes `-fno-signed-char`.

Canaries for both in `test_hostmodel`. **`size_t` (8 vs 4 bytes) is the next
candidate and has not been audited.** Also unaudited: struct padding, bitfield
layout, double promotion.

A `-m32` CI job would make case 1 belt-and-braces. Needs `gcc-multilib`, Linux
only, worth adding after the next round of seam work.

## MODULE ARCHITECTURE - where splitting is worth it

**Split for testability. Do NOT split for icache/LPS - that was measured and
did not pay.**

Evidence: moving 10.2 KB of AsyncHTTPRequest out of the TU holding `loop()`
(12,085 B -> 1,868 B of `.text`) produced no measurable LPS change on device
and an inconclusive tick_max reading. That was a *bigger* move than the
b026bb7d CounterWeb split credited with LPS 106.5 -> 108, which is 1.4% and
inside the noise band. Treat that historical figure as unproven.

Corollary: `.o` file size is NOT code size. `Counter.cpp.o` is 47.6 KB on disk
but 6,219 B of `.text` - the rest is DWARF. Use `xtensa-lx106-elf-size -A`
summing `.text*`, never `ls -l`.

### The four shapes that block testing

1. **Pure logic sharing a TU with web handlers or I/O.** Fix: split, following
   the existing `CounterWeb.cpp` / `GRNGWeb.cpp` convention. Cheapest, no
   behaviour change, and both splits so far came out flash-negative.
2. **Hardware reads interleaved with maths.** BoschTHP reads I2C then computes
   compensation in the same function. Fix: separate collect from compute.
3. **Logic reading global singletons instead of parameters.** `ntpclient.synced`,
   `EGPrefs::getString`, `DeviceInfo::uptime()` inside formulas. This is what
   blocks Counter. Fix: pass inputs explicitly.
4. **Wrong integer types.** `unsigned long` where `uint32_t` is meant. Free on
   target, and it is what made the uptime test possible at all.

### The pattern that works

`UptimeCounter` came out **byte-identical on ESP8266**. What made it free:

- A plain class/struct with **explicit inputs**, out-of-line, own small header.
- **No virtuals, no dependency injection, no interfaces.** Those cost flash and
  indirection here.
- Prove it: build before/after across SoC families and compare `Flash:`.

Do not reshape speculatively. Every seam must be justified by a specific test
you want to write. There is no coverage to validate a big-bang refactor
against, which is why incremental is the only safe order.

### Candidate seams, ranked by testability value

| Seam | Work | Unlocks |
|---|---|---|
| Counter `counts_missing` / `get_tube_alive` / `is_quiet_now` | moderate: take inputs explicitly instead of reading members | M3, the crown jewels. `counts_missing` clamp table, tube_alive never-counted regression, midnight wrap |
| `due_now(now, due)` header | small: 1 header + 12 call sites | the `(long)(now - due)` idiom, currently written 12x across registry/OLED/WiFi/HV/PulseOut/Voice/ESPGeiger.cpp and correct only because `long` is 32-bit |
| `sleep_until` interval ladder | small: lift 6 lines to a free function | the ladder, exhaustively |
| BoschTHP / AsairAHT compensation | moderate: split from the `Wire` read | BME280 int64 pressure path, `t_fine` carry, signed/unsigned calibration words |
| WebAPI payload build + sign | moderate | M4 golden bytes, signed region |
| EGPrefs parse/serialise | probably small - `EGPrefs.h` already compiles against the shim | M5 round trip, bounds clamping, migration |

**Not worth it:** `EGModuleRegistry` internals (park/cap/ordering). The TU pulls
Logger, Wifi, DeviceInfo, NTP, ArduinoOTA, Counter, EGPrefs - shimming that
costs more than the unit under test, which the philosophy section forbids.

**Already done, nothing left:** Counter web split (b026bb7d, Counter.cpp is
already the slim 6.2 KB TU; only ~2.2 KB of cold code remains).

## Shim status

Present: fake clock (`millis`/`micros`/`delay`, test-controlled), PROGMEM +
`pgm_read_*` + `*_P` string fns, `Print` (enough for `Sha256Class`), `ESP`
(cycle count from the fake clock, heap, restart counter, cpu freq, wdt),
`IRAM_ATTR` family, interrupt registration stubs, `portMUX` no-ops, GPIO
no-ops, `esp_random` with a test hook, `randomSeed`/`random`, `map`, min/max
templates, bit macros, maths constants.

Verified compiling against the shim: `Logger.h`, `MathUtil.h`, `FastMillis.h`,
`EGModule.h`, `EGPrefs.h`, `DeviceInfo.h`, `CrashDump.h`, `GeigerInput.h`.

**Missing, in likely-need order:**

- `IPAddress` - blocks `Wifi.h` and transitively `SerialCommand.h`. Small class.
- `Stream` / `Serial` - `Logger.h` compiles without them, `Logger.cpp` will not.
- `String` - **the big deliberate omission.** Adding it invites every
  String-using unit into scope. Add only when a specific unit worth testing
  demands it.
- `Wire` - env sensors. Prefer extracting the compensation maths instead.
- FreeRTOS (`xTaskCreate`, queues, `esp_timer`) - M6 territory.

## Test inventory

### M1 - leaf utilities - DONE

`test_hostmodel`, `test_base64`, `test_msgpack`, `test_ringavg`,
`test_circularbuffer`, `test_stringutil`. See the suites for detail.

### M2 - time and scheduling - PARTIAL

- **DONE** `test_uptime`: `UptimeCounter` extracted from `NTP.h`. Carries a
  copy of the pre-8c57420c formula and asserts it is broken. Wrap counting,
  repeated calls in the sub-second window, monotonicity and bounded drift over
  2 simulated years.
- **TODO** `due_now` wrap-safe compare (needs the seam above).
- **TODO** `sleep_until` ladder.
- **TODO** hash-and-walk offsets - the per-module chipid hash at
  `EGModuleRegistry.cpp:305-313`, inside the heavy TU. Needs locating properly
  before costing; the `:00`-alignment-for-SD scheduling was not found.
- **SKIP** registry internals (see above).

### M3 - Counter - NOT STARTED, needs the seam

`counts_missing()` table across ratio x total_clicks x uptime pinning
`clamp(max(6000/ratio, 10*ut/tc), 60, _tube_timeout_s)`; SI-3BG self-calibration
to ~600 s; dead-tube cap path; the 60 s early exit. `tube_alive` never-counted
regression. `is_quiet_now` midnight wrap. CPM getters against synthetic click
trains. Lifetime totals and `total_usv` accumulation, ratio changes mid-life.

Note: `counts_missing` is 1 Hz and already fast-exits under 60 s silence;
`is_quiet_now` is minute-cached. Neither is hot. Sell the extraction on
testability, **not** performance.

### M4 - crypto and signing - PARTIAL

- **DONE** `test_sha256`: FIPS 180-2 and RFC 4231 known-answer vectors, every
  padding boundary 0..128, split invariance, HMAC key-length branch. The
  implementation is **verified correct** - it never had been.
- **DONE** `test_grng`: xorshift zero-guard under a stuck-at-zero source,
  stuck-source survival (zero and ones), avalanche, `fast_uint32` refill on
  exactly the 9th call, exact-length fills, the 52-byte single-block constraint.
- **TODO, highest value left: uECC.** Zero hardware dependencies, no production
  change needed, and it **ships its own vectors** (`test/ecdsa_test_vectors.c`,
  `public_key_test_vectors.c`, `test_ecdsa.c`, `test_ecdh.c`, `test_compute.c`).
  Must compile with the repo's exact flags (`secp160r1/224r1/256r1/256k1=0`,
  `COMPRESSED_POINT=0`, `OPTIMIZATION_LEVEL=1`) or it certifies a config we do
  not ship.
- **TODO ECDSA nonce reuse**: sign several distinct messages, assert `r` values
  are all distinct. Nonce reuse leaks the private key outright. This is the one
  genuine security property host code can check and it was missing from the
  original plan.
- **TODO** native-LE wire-order regression - signature/pubkey byte order must
  match StationsAPI. Needs **golden bytes from a captured known-good exchange**;
  testing our encoder against our decoder proves nothing about the server.
- **TODO** WebAPI payload end-to-end with fixed inputs, golden bytes, timestamp
  inside the signed region.

Scope discipline: a host test cannot establish that the RNG is secure.
Statistical batteries only rule out gross structure - a counter through AES
passes them all. Hardware entropy and the boot-SRAM seed are device properties;
dump a few hundred MB off a real board and run PractRand offline, once.

### M5 - parsing and prefs - NOT STARTED

- EGPrefs parse/serialise round trip, defaults, bounds clamping, unknown keys,
  the display-prefs 3-way migration when that branch lands.
- EGHttp request parsing, route matching, arg extraction (shared-buffer
  semantics), response bounds (>256 without `F()` must 500), route-table
  overflow past 32. Needs the AsyncTCP shim, so effectively M6.
- SerialCommand parser: command/arg splitting, malformed input. Needs
  `IPAddress`.
- **OLED graph auto-range including the `map()` span=0 div0 regression** - pure
  function, cheap, and a real decoded crash. Good next-after-uECC pick.
- Config export/import round trip.

### M6 - async TCP state machine - NOT STARTED, the big one

The four bugs that actually bit us are callback-ordering bugs in our own state
machine, not lwIP subtleties: recv-on-closed-pcb UAF (crash #85), the DNS
found-callback UAF, the EGAsyncTCP32 dtor UAF, the ESP8266 teardown UAF.

- **Fake lwIP**: a pcb registry plus a callback scheduler, not a TCP stack.
  Roughly `tcp_new/bind/listen/accept/recv/sent/err/write/output/close/abort/
  recved` and the `pbuf_*` family. The test drives ordering directly: deliver a
  recv callback for a pcb the handler just closed; fire a DNS found callback
  after the client is deleted; delete inside `onDisconnect`.
- **ASan is the point.** On target these surface as a `memp_free` null-store in
  an unrelated stack tens of minutes later. `[env:native_asan]` is wired.
- Bigger than M1-M5 combined. Own session, clear head.

Still the chaos rig's job: real lwIP internals, WiFi-task interleaving, ESP32
cross-core races, pool exhaustion under load.

## Beyond tests - implementations we rely on but have not proved

Testing checks behaviour against expectation. These are cases where the
*expectation itself* deserves a review, not just a test:

- **`counts_missing` statistics.** The comment claims Poisson with P ~ 5e-5 for
  10 expected counts of silence. Worth checking the maths is what the code
  implements, separately from testing that the code implements the formula.
- **`apply_dead_time`** - the correction formula against detector theory, not
  just against itself.
- **uSv conversion ratios** per tube type - a data-correctness question, not a
  code one.
- **MsgPack wire format vs what StationsAPI actually accepts.** Our golden
  bytes pin our encoder; they do not prove the server agrees. Capture a real
  known-good exchange.
- **HV control loop / trim** - `TRIM_HYST_V=8` dead band; behaviour understood
  but never modelled.
- **CrashDump reset-reason gate** - `rr==4` is by design; a test would pin that
  the gate cannot silently invert.
- **Improv protocol checksum** - skipped deliberately (Paul: "nah"), but it is
  a byte-level format with 11 documented landmines.

## Open findings, not fixed

- `format_f(v, 0)` emits `"42.0"`. Now documented as out-of-contract
  (`decimals >= 1`); no caller passes 0. Fix needs a branch inside a function
  kept out-of-line so GCC does not clone it.
- MQTT `setMaxTopicLength()` above 253 used to wedge the parser; fixed by
  widening `_bytePosition` to `uint16_t`. The library's `char`-typed raw byte
  fields are now cast through `uint8_t`, verified free (`PublishPacket.cpp.o`
  is 879 B of `.text` either way).
