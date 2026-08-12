/*
  test_tinyserial - EGTinySerial decoder and SPSC ring.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <unity.h>
#include <Arduino.h>

#include "../../lib/EGTinySerial/src/EGTinySerialDecoder.h"
#include "../../lib/EGTinySerial/src/EGSpscRing.h"

using EGTinySerial::Decoder;
using EGTinySerial::Emit;
using EGTinySerial::SpscRing;

static const uint32_t CPU_HZ  = 80000000UL;
static const uint32_t CPB_9K6 = 8333;   // 80 MHz / 9600
static const uint32_t CPB_115 = 694;    // 80 MHz / 115200

// ---------------------------------------------------------------------------
// Harness: bytes to line transitions and back
// ---------------------------------------------------------------------------

struct EdgeList {
  static const size_t MAX = 4096;
  uint32_t tick[MAX];
  bool     level[MAX];
  size_t   n = 0;
  void add(uint32_t t, bool l) {
    TEST_ASSERT_LESS_THAN_size_t(MAX, n);
    tick[n] = t; level[n] = l; n++;
  }
};

// One 8N1 frame per byte, back to back. `line` carries the current level in
// and out so callers can splice runs together. Returns the tick one frame
// past the last stop bit.
static uint32_t encode(EdgeList& out, const uint8_t* bytes, size_t n,
                       uint32_t t0, uint32_t cpb, bool& line) {
  uint32_t t = t0;
  for (size_t i = 0; i < n; i++) {
    bool bit[10];
    bit[0] = false;                                       // start
    for (int b = 0; b < 8; b++) bit[1 + b] = (bytes[i] >> b) & 1;
    bit[9] = true;                                        // stop
    for (uint32_t b = 0; b < 10; b++) {
      if (bit[b] != line) { out.add(t + b * cpb, bit[b]); line = bit[b]; }
    }
    t += 10u * cpb;
  }
  return t;
}

struct Run {
  uint8_t  bytes[1024];
  size_t   n         = 0;
  uint32_t framing   = 0;
  uint32_t coalesced = 0;
  uint32_t breaks    = 0;

  void take(const Emit& e) {
    if (e.valid()) { TEST_ASSERT_LESS_THAN_size_t(1024, n); bytes[n++] = e.byte; }
    if (e.framing())   framing++;
    if (e.coalesced()) coalesced++;
    if (e.brk())       breaks++;
  }
};

static void feed(Decoder& d, Run& r, const EdgeList& e) {
  for (size_t i = 0; i < e.n; i++) r.take(d.edge(e.tick[i], e.level[i]));
}

// The line has been idle high for well over a frame.
static void feedIdle(Decoder& d, Run& r, uint32_t at) {
  r.take(d.idle(at, true));
}

// Encode, feed, then flush. This is the whole real path: a byte is only
// emitted once its stop bit run ends, so every byte needs either a following
// start bit or this flush.
static void roundTrip(Decoder& d, Run& r, const uint8_t* bytes, size_t n,
                      uint32_t cpb, uint32_t t0 = 0) {
  EdgeList e;
  bool line = true;
  d.configure(cpb);
  d.reset(t0, true);
  const uint32_t end = encode(e, bytes, n, t0, cpb, line);
  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
}

void setUp(void)    { eg_clock_reset(); }
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

static void test_single_byte_at_9600(void) {
  const uint8_t in[] = { 0x41 };
  Decoder d; Run r;
  roundTrip(d, r, in, 1, CPB_9K6);
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8(0x41, r.bytes[0]);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
  TEST_ASSERT_FALSE(d.midFrame());
}

static void test_single_byte_at_115200(void) {
  const uint8_t in[] = { 0x41 };
  Decoder d; Run r;
  roundTrip(d, r, in, 1, CPB_115);
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8(0x41, r.bytes[0]);
}

// Bit 7 clear means the stop bit is not preceded by a transition, so nothing
// is emitted until the run ends. Every ASCII byte is this case.
static void test_pending_byte_needs_the_idle_flush(void) {
  const uint8_t in[] = { '4' };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 1, 0, CPB_115, line);
  feed(d, r, e);

  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_TRUE(d.midFrame());

  feedIdle(d, r, end + d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8('4', r.bytes[0]);
  TEST_ASSERT_FALSE(d.midFrame());
}

// A one bit start pulse and then nine high bits with no transition anywhere.
// Only the flush can count them out.
static void test_all_ones_byte_ends_with_no_edge(void) {
  const uint8_t in[] = { 0xFF };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 1, 0, CPB_115, line);

  TEST_ASSERT_EQUAL_size_t(2, e.n);
  TEST_ASSERT_FALSE(e.level[0]);
  TEST_ASSERT_TRUE(e.level[1]);
  TEST_ASSERT_EQUAL_UINT32(CPB_115, e.tick[1] - e.tick[0]);

  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8(0xFF, r.bytes[0]);
}

// Mirror case: start plus eight zero data bits are one nine-bit low run, so a
// single rising edge carries the whole byte.
static void test_zero_byte_is_one_low_run(void) {
  const uint8_t in[] = { 0x00 };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 1, 0, CPB_115, line);

  TEST_ASSERT_EQUAL_size_t(2, e.n);

  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8(0x00, r.bytes[0]);
}

// A transition every bit, the densest the line gets.
static void test_alternating_bit_patterns(void) {
  const uint8_t in[] = { 0x55, 0xAA };
  Decoder d; Run r;
  roundTrip(d, r, in, 2, CPB_115);
  TEST_ASSERT_EQUAL_size_t(2, r.n);
  TEST_ASSERT_EQUAL_HEX8(0x55, r.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAA, r.bytes[1]);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
}

static void test_framing_error_discards_the_byte(void) {
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  // Start bit, eight high data bits, then the stop bit sampled low.
  r.take(d.edge(0, false));                    // start
  r.take(d.edge(1 * CPB_115, true));           // eight ones
  r.take(d.edge(9 * CPB_115, false));          // stop low
  r.take(d.edge(11 * CPB_115, true));

  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_EQUAL_UINT32(1, r.framing);
  TEST_ASSERT_FALSE(d.midFrame());
}

// Manufacturing a stop bit purely on elapsed time invents a byte out of a
// held-low line. The level argument is what stops that.
static void test_break_emits_nothing(void) {
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  r.take(d.edge(0, false));                    // line goes down and stays
  r.take(d.idle(50 * CPB_115, false));         // still low

  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_EQUAL_UINT32(1, r.breaks);
  TEST_ASSERT_FALSE(d.midFrame());
}

// After a break the line comes back up. The tail of the break is not a start
// bit, and treating it as one would fabricate a byte on every recovery.
static void test_break_resyncs_without_a_phantom_byte(void) {
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  r.take(d.edge(0, false));
  r.take(d.idle(50 * CPB_115, false));
  r.take(d.edge(80 * CPB_115, true));          // break ends
  r.take(d.idle(200 * CPB_115, true));

  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);

  // And the next real byte decodes normally.
  EdgeList e;
  bool line = true;
  const uint32_t t0  = 300 * CPB_115;
  const uint8_t  in[] = { 0x5A };
  const uint32_t end = encode(e, in, 1, t0, CPB_115, line);
  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());

  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8(0x5A, r.bytes[0]);
}

// Byte N's pending stop bit is resolved by byte N+1's start edge instead of
// by the flush. Both routes must produce the same value.
static void test_back_to_back_frames_match_the_flush_path(void) {
  const uint8_t in[] = { '1', '2', '3', '4', '\r', '\n' };
  Decoder d; Run r;
  roundTrip(d, r, in, 6, CPB_115);
  TEST_ASSERT_EQUAL_size_t(6, r.n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in, r.bytes, 6);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
}

static void test_tick_counter_wrap(void) {
  const uint8_t in[] = { 'A', 'B' };
  Decoder d; Run r;
  // Start far enough back that the frames straddle zero.
  roundTrip(d, r, in, 2, CPB_115, 0xFFFFFF00u);
  TEST_ASSERT_EQUAL_size_t(2, r.n);
  TEST_ASSERT_EQUAL_HEX8('A', r.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8('B', r.bytes[1]);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
}

// Any gap past one frame is idle whatever the exact figure, so a delta the
// clock cannot have measured honestly still leaves the decoder consistent.
static void test_absurd_deltas_are_treated_as_idle(void) {
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  r.take(d.edge(0x80000000u, false));          // 2^31 of idle high
  r.take(d.edge(0xFFFFFFFFu, true));           // most of the range low
  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_FALSE(d.midFrame());
}

// The point of measuring deltas rather than absolute times: interrupt entry
// latency is common to both endpoints and cancels.
static void test_constant_latency_offset_cancels(void) {
  const uint8_t in[] = { 'H', 'i', '!' };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  uint32_t end = encode(e, in, 3, 0, CPB_115, line);
  for (size_t i = 0; i < e.n; i++) e.tick[i] += (CPB_115 * 2) / 5;   // +0.4 bit

  feed(d, r, e);
  feedIdle(d, r, end + 4 * d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(3, r.n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in, r.bytes, 3);
}

// Round to nearest gives just under half a bit of headroom on each delta.
static void test_single_edge_jitter_within_tolerance(void) {
  const uint8_t in[] = { 0x0F };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 1, 0, CPB_115, line);
  e.tick[1] += (CPB_115 * 2) / 5;              // one edge 0.4 bit late

  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8(0x0F, r.bytes[0]);
}

// CHARACTERISATION. Run length decoding carries no redundancy, so an edge
// displaced past half a bit moves a bit between runs and yields a wrong byte
// rather than an error. There is no framing check that can catch it. This is
// why the port counts coalesced edges and why the sampling arm is decided by
// measurement rather than argument.
static void test_edge_jitter_past_tolerance_corrupts_silently(void) {
  const uint8_t in[] = { 0x0F };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 1, 0, CPB_115, line);
  e.tick[2] += (CPB_115 * 3) / 5;              // 0.6 bit late

  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
  TEST_ASSERT_NOT_EQUAL_HEX8(0x0F, r.bytes[0]);
}

// A 2% clock mismatch accumulates 0.2 of a bit over a whole frame, inside the
// rounding margin. The old library's 8.5 against 8.68 microsecond bit period
// was this case.
static void test_two_percent_clock_mismatch_still_decodes(void) {
  const uint8_t in[] = { 0x55, 0x33, 0x0F };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  const uint32_t sender = CPB_115;
  d.configure((sender * 98) / 100);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 3, 0, sender, line);
  feed(d, r, e);
  feedIdle(d, r, end + 4 * d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(3, r.n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in, r.bytes, 3);
}

// Two transitions arriving before the handler reads the pin are delivered as
// one, with the level already back where it started. That is impossible for a
// genuine transition, so it is caught here instead of becoming a wrong byte.
static void test_coalesced_edge_is_detected(void) {
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  r.take(d.edge(0, false));
  r.take(d.edge(2 * CPB_115, false));          // low reported twice

  TEST_ASSERT_EQUAL_UINT32(1, r.coalesced);
  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_FALSE(d.midFrame());
}

// Shorter than half a bit, so it rounds away to nothing.
static void test_sub_bit_glitch_leaves_no_state(void) {
  Decoder d; Run r;
  d.configure(CPB_9K6);
  d.reset(0, true);
  r.take(d.edge(1000 * CPB_9K6, false));
  r.take(d.edge(1000 * CPB_9K6 + CPB_9K6 / 8, true));

  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
  TEST_ASSERT_FALSE(d.midFrame());
}

// CHARACTERISATION. A glitch longer than half a bit is indistinguishable from
// a start bit, so it yields a spurious byte. A hardware UART does the same. The
// defence is at the line level, where GeigerSerial drops unprintable bytes and
// drains the port after a streak of unparseable lines. What matters here is
// that the decoder returns to idle rather than staying wedged mid frame.
static void test_noise_pulse_yields_a_spurious_byte_then_recovers(void) {
  Decoder d; Run r;
  d.configure(CPB_9K6);
  d.reset(0, true);
  r.take(d.edge(0, false));
  r.take(d.edge(3 * CPB_9K6 / 2, true));       // 1.5 bits low
  TEST_ASSERT_TRUE(d.midFrame());

  r.take(d.idle(40 * CPB_9K6, true));
  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_UINT32(0, r.framing);
  TEST_ASSERT_FALSE(d.midFrame());

  // And a real byte straight after is unaffected.
  EdgeList e;
  bool line = true;
  const uint8_t  in[] = { 'K' };
  const uint32_t end  = encode(e, in, 1, 100 * CPB_9K6, CPB_9K6, line);
  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
  TEST_ASSERT_EQUAL_size_t(2, r.n);
  TEST_ASSERT_EQUAL_HEX8('K', r.bytes[1]);
}

static void test_idle_does_not_fire_early(void) {
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  r.take(d.edge(0, false));
  r.take(d.edge(1 * CPB_115, true));           // start consumed, three bits in
  r.take(d.idle(3 * CPB_115, true));

  TEST_ASSERT_EQUAL_size_t(0, r.n);
  TEST_ASSERT_TRUE(d.midFrame());
}

static void test_idle_does_not_fire_twice(void) {
  const uint8_t in[] = { '7' };
  EdgeList e;
  bool line = true;
  Decoder d; Run r;
  d.configure(CPB_115);
  d.reset(0, true);
  const uint32_t end = encode(e, in, 1, 0, CPB_115, line);
  feed(d, r, e);
  feedIdle(d, r, end + d.idleCycles());
  feedIdle(d, r, end + 4 * d.idleCycles());

  TEST_ASSERT_EQUAL_size_t(1, r.n);
  TEST_ASSERT_EQUAL_HEX8('7', r.bytes[0]);
}

static void test_cycles_per_bit(void) {
  TEST_ASSERT_EQUAL_UINT32(8333, EGTinySerial::cyclesPerBit(9600,   CPU_HZ));
  TEST_ASSERT_EQUAL_UINT32(694,  EGTinySerial::cyclesPerBit(115200, CPU_HZ));
  TEST_ASSERT_EQUAL_UINT32(1389, EGTinySerial::cyclesPerBit(115200, 160000000UL));
  TEST_ASSERT_EQUAL_UINT32(2083, EGTinySerial::cyclesPerBit(115200, 240000000UL));
  // Refusals, so a port declines to open rather than running slow and wrong.
  TEST_ASSERT_EQUAL_UINT32(0, EGTinySerial::cyclesPerBit(0,        CPU_HZ));
  TEST_ASSERT_EQUAL_UINT32(0, EGTinySerial::cyclesPerBit(115200,   0));
  TEST_ASSERT_EQUAL_UINT32(0, EGTinySerial::cyclesPerBit(20000000, CPU_HZ));
}

// ---------------------------------------------------------------------------
// Ring
// ---------------------------------------------------------------------------

static void test_ring_fills_and_drains_in_order(void) {
  uint8_t storage[8];
  SpscRing q;
  q.init(storage, 8);
  TEST_ASSERT_EQUAL_UINT32(0, q.available());
  TEST_ASSERT_EQUAL_INT(-1, q.pop());
  TEST_ASSERT_EQUAL_INT(-1, q.peek());

  for (uint8_t i = 0; i < 8; i++) TEST_ASSERT_TRUE(q.push((uint8_t)(i + 1)));
  TEST_ASSERT_EQUAL_UINT32(8, q.available());
  TEST_ASSERT_EQUAL_INT(1, q.peek());
  for (uint8_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL_INT(i + 1, q.pop());
  TEST_ASSERT_EQUAL_UINT32(0, q.available());
}

static void test_ring_wraps_at_capacity(void) {
  uint8_t storage[8];
  SpscRing q;
  q.init(storage, 8);
  for (int cycle = 0; cycle < 40; cycle++) {
    TEST_ASSERT_TRUE(q.push((uint8_t)cycle));
    TEST_ASSERT_TRUE(q.push((uint8_t)(cycle + 100)));
    TEST_ASSERT_EQUAL_INT((uint8_t)cycle, q.pop());
    TEST_ASSERT_EQUAL_INT((uint8_t)(cycle + 100), q.pop());
  }
  TEST_ASSERT_EQUAL_UINT32(0, q.available());
}

// CONTRACT. Overwriting the oldest byte would corrupt the front of a line the
// consumer is part way through parsing. Losing the tail costs one line.
static void test_ring_full_drops_the_newest(void) {
  uint8_t storage[8];
  SpscRing q;
  q.init(storage, 8);
  for (uint8_t i = 0; i < 8; i++) TEST_ASSERT_TRUE(q.push(i));
  TEST_ASSERT_FALSE(q.push(99));
  TEST_ASSERT_FALSE(q.push(98));
  TEST_ASSERT_EQUAL_UINT32(8, q.available());
  for (uint8_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL_INT(i, q.pop());
}

static void test_ring_interleaved_push_and_pop(void) {
  uint8_t storage[16];
  SpscRing q;
  q.init(storage, 16);
  uint32_t in = 0, out = 0;
  for (int step = 0; step < 500; step++) {
    if ((step % 3) != 2) { if (q.push((uint8_t)in)) in++; }
    else {
      const int got = q.pop();
      if (got >= 0) { TEST_ASSERT_EQUAL_INT((uint8_t)out, got); out++; }
    }
    TEST_ASSERT_EQUAL_UINT32(in - out, q.available());
  }
}

static void test_ring_clear_from_the_consumer(void) {
  uint8_t storage[8];
  SpscRing q;
  q.init(storage, 8);
  for (uint8_t i = 0; i < 5; i++) q.push(i);
  q.clear();
  TEST_ASSERT_EQUAL_UINT32(0, q.available());
  TEST_ASSERT_EQUAL_INT(-1, q.pop());
  TEST_ASSERT_TRUE(q.push(42));
  TEST_ASSERT_EQUAL_INT(42, q.pop());
}

// The indices are free running counters, so their own wrap has to be a
// non-event. Walk one right through it.
static void test_ring_index_counter_wrap(void) {
  uint8_t storage[8];
  SpscRing q;
  q.init(storage, 8);
  for (uint32_t i = 0; i < 0x100000u; i++) {
    TEST_ASSERT_TRUE(q.push((uint8_t)i));
    TEST_ASSERT_EQUAL_INT((uint8_t)i, q.pop());
  }
  TEST_ASSERT_EQUAL_UINT32(0, q.available());
}

// ---------------------------------------------------------------------------
// Decoder into ring
// ---------------------------------------------------------------------------

static void test_line_lands_in_the_ring(void) {
  const uint8_t in[] = { '1', '2', '3', '4', '\r', '\n' };
  uint8_t storage[16];
  SpscRing q;
  EdgeList e;
  bool line = true;
  Decoder d;
  q.init(storage, 16);
  d.configure(CPB_115);
  d.reset(0, true);

  const uint32_t end = encode(e, in, 6, 0, CPB_115, line);
  for (size_t i = 0; i < e.n; i++) {
    const Emit em = d.edge(e.tick[i], e.level[i]);
    if (em.valid()) TEST_ASSERT_TRUE(q.push(em.byte));
  }
  const Emit last = d.idle(end + d.idleCycles(), true);
  if (last.valid()) TEST_ASSERT_TRUE(q.push(last.byte));

  TEST_ASSERT_EQUAL_UINT32(6, q.available());
  for (size_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL_INT(in[i], q.pop());
}

// The terminator is the byte the flush has to deliver, and a line whose last
// data byte has bit 7 set exercises the other flush route.
static void test_line_ending_in_a_high_bit_byte(void) {
  const uint8_t in[] = { 0xC3, 0xFF, '\n' };
  Decoder d; Run r;
  roundTrip(d, r, in, 3, CPB_9K6);
  TEST_ASSERT_EQUAL_size_t(3, r.n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in, r.bytes, 3);
}

static void test_random_bytes_round_trip(void) {
  uint32_t seed = 0x13579BDFu;
  Decoder d;
  for (int batch = 0; batch < 40; batch++) {
    uint8_t in[256];
    for (size_t i = 0; i < 256; i++) {
      seed = seed * 1664525u + 1013904223u;
      in[i] = (uint8_t)(seed >> 16);
    }
    Run r;
    roundTrip(d, r, in, 256, CPB_115);
    TEST_ASSERT_EQUAL_size_t(256, r.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, r.bytes, 256);
    TEST_ASSERT_EQUAL_UINT32(0, r.framing);
    TEST_ASSERT_EQUAL_UINT32(0, r.coalesced);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_byte_at_9600);
  RUN_TEST(test_single_byte_at_115200);
  RUN_TEST(test_pending_byte_needs_the_idle_flush);
  RUN_TEST(test_all_ones_byte_ends_with_no_edge);
  RUN_TEST(test_zero_byte_is_one_low_run);
  RUN_TEST(test_alternating_bit_patterns);
  RUN_TEST(test_framing_error_discards_the_byte);
  RUN_TEST(test_break_emits_nothing);
  RUN_TEST(test_break_resyncs_without_a_phantom_byte);
  RUN_TEST(test_back_to_back_frames_match_the_flush_path);
  RUN_TEST(test_tick_counter_wrap);
  RUN_TEST(test_absurd_deltas_are_treated_as_idle);
  RUN_TEST(test_constant_latency_offset_cancels);
  RUN_TEST(test_single_edge_jitter_within_tolerance);
  RUN_TEST(test_edge_jitter_past_tolerance_corrupts_silently);
  RUN_TEST(test_two_percent_clock_mismatch_still_decodes);
  RUN_TEST(test_coalesced_edge_is_detected);
  RUN_TEST(test_sub_bit_glitch_leaves_no_state);
  RUN_TEST(test_noise_pulse_yields_a_spurious_byte_then_recovers);
  RUN_TEST(test_idle_does_not_fire_early);
  RUN_TEST(test_idle_does_not_fire_twice);
  RUN_TEST(test_cycles_per_bit);
  RUN_TEST(test_ring_fills_and_drains_in_order);
  RUN_TEST(test_ring_wraps_at_capacity);
  RUN_TEST(test_ring_full_drops_the_newest);
  RUN_TEST(test_ring_interleaved_push_and_pop);
  RUN_TEST(test_ring_clear_from_the_consumer);
  RUN_TEST(test_ring_index_counter_wrap);
  RUN_TEST(test_line_lands_in_the_ring);
  RUN_TEST(test_line_ending_in_a_high_bit_byte);
  RUN_TEST(test_random_bytes_round_trip);
  return UNITY_END();
}
