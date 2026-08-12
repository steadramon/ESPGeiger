# EGTinySerial

Receive-first software serial for ESP8266 and ESP32. 8N1 only, no heap.

```cpp
#include <EGTinySerial.h>

static EGTinySerial::Port<64> port;          // capacity must be a power of two

port.begin(115200, rxPin, txPin);            // txPin optional, defaults to -1
while (port.available()) c = port.read();
const auto& s = port.stats();
```

## Constraints

**8N1 only.** No parity, no 5/6/7-bit words, no two stop bits, no inversion,
no one-wire, no RS-485 enable pin. Each of those widens the interrupt handler,
and nothing in this project asks for them.

**One producer and one consumer.** The handler fills the ring and the polling
loop drains it. Reading from two places races.

**`available()`, `read()`, `peek()` and `readInto()` must be called
regularly.** They run the idle flush, which is not an escape hatch: only line
transitions are recorded, so a frame whose trailing bits are high ends with no
edge to mark it. Every ASCII byte has bit 7 clear, so that is the last byte of
every burst, including the newline ending every line. Nothing arrives if
nothing polls.

**Not a `Stream`.** Virtual read/write/peek/flush would pin the transmit path
into the vtable, and `--gc-sections` could not then drop it from a
receive-only build. That is several hundred bytes of ESP8266 IRAM. Use
`EGTinySerialStream.h` where a `Stream&` is genuinely required.

**115200 is the tested ceiling**, and above `EG_TINYSERIAL_SYNC_BAUD` it is
reached by sampling the frame inline, not by timing edges. Per-edge interrupt
service is 5 to 6 us against an 8.68 us bit at 115200, which does not leave
enough margin: measured on a live device, the edge handler accumulates
`framing` and `coalesced` counts at that rate. Watch both before trusting any
new baud.

**Call `tick()` about once a second** if anything can change the CPU
frequency. The bit period is held in cycles, so a clock change invalidates it.

**`begin()` returns false rather than opening at the wrong bit period.** A
port that looks alive and delivers rubbish is worse than one that refused.

## Layout

| file | |
|---|---|
| `EGTinySerialDecoder.{h,cpp}` | the 8N1 state machine. Pure: no Arduino, no pins, no interrupts, no atomics. Host tested in `test/test_tinyserial`. |
| `EGSpscRing.h` | lock-free byte ring. Host tested. |
| `EGTinySerialPlatform.h` | the only file that knows which chip this is. |
| `EGTinySerial.{h,cpp}` | pins, handlers, admission budget. |
| `EGTinySerialTx.cpp` | transmit. Its own unit so the linker can drop it. |
| `EGTinySerialStream.h` | optional `Stream` facade. |

## How it differs from EspSoftwareSerial

**Decodes in the handler and pushes bytes.** Upstream queues 32-bit edge
timestamps and decodes them later on the main thread, which needs
`bufCapacity * (2 + dataBits)` words: 2564 bytes of heap at the settings this
project uses. Here the buffer is a `uint8_t[Cap]` in `.bss` and the heap cost
is zero.

**Cycle counter, not `micros()`.** A bit at 115200 is 8.68 us, so `micros()`
rounds away 11.5% of it. That quantisation is the entire reason upstream
switches sampling strategy at 74880 baud.

**The sampling handler masks its own pin.** Upstream's leaves it armed, so a
falling data bit sets the interrupt status mid frame and the dispatcher
re-fires the handler the moment it returns, taking a mid frame edge as a fresh
start bit. Masking for the duration removes that, and with it several times
the CPU each frame was costing.

**Masks the pin when over budget instead of declining a frame.** Declining
still pays full interrupt entry, which at a high enough edge rate starves the
loop on its own. The poller re-arms after the window. A storm is not carrying
data anyway.

**Detects what upstream turns into a wrong byte.** A transition pair the
handler missed leaves the line level unchanged, which cannot happen across a
genuine transition, so it is counted as `coalesced` rather than silently
shifting a bit. Upstream's idle flush also infers a stop bit purely from
elapsed time, so holding the line low manufactures a byte; here `idle()` takes
the level and reports a break instead.

**Separates the two overflow causes.** `dropped` means the caller is not
polling. Upstream merges that with the opposite condition under one
`overflow()`.

**Counters are monotonic.** Read-and-clear flags lose magnitude and let one
reader steal an event from another.

## Tuning

| macro | default | |
|---|---|---|
| `EG_TINYSERIAL_SYNC_BAUD` | 78432 | at or above this the handler samples the frame inline instead of timing edges. Set it past every baud in use to force the edge path. |
| `EG_TINYSERIAL_BUDGET_WINDOW_US` | 10000 | admission window. |
| `EG_TINYSERIAL_BUDGET_US` | 5000 | handler share of that window before the pin is masked. |
| `EG_TINYSERIAL_BENCH` | off | adds `isrCalls` and `isrMaxCycles` to `Stats`. |

## Placement

Anything the handler reaches is in IRAM, including `Decoder::edge`,
`runOfBits` and `bitsIn`. Flash is unmapped while it is being written, so a
byte arriving during a filesystem save or an OTA would fault on the
instruction fetch. Nothing enforces this at build time. If you add a call from
a handler, mark the callee `EGTS_ISR_ATTR` and check it lands in
`.iram.text.*`:

```
xtensa-lx106-elf-size -A <object>.o
```

For the same reason the port class is a template only around `begin()` and the
storage array. GCC does not honour `IRAM_ATTR` on template members, so a
handler defined in a header would land in flash with no diagnostic.
