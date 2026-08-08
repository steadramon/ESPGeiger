/*
  test_circularbuffer - CircularBuffer (vendored, Roberto Lo Giacco 1.4.0).

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

// Characterisation, not verification. CircularBuffer and EGRingAvg may be
// unified; this pins what the vendored one does first. Cases the library
// documents as undefined (pop/shift on empty) are not asserted.

#include <unity.h>
#include <CircularBuffer.hpp>

#include "Util/EGSmoothed.h"

void setUp(void)    {}
void tearDown(void) {}

// --- empty and capacity -----------------------------------------------------

static void test_empty_state(void) {
  CircularBuffer<int, 4> b;
  TEST_ASSERT_TRUE(b.isEmpty());
  TEST_ASSERT_FALSE(b.isFull());
  TEST_ASSERT_EQUAL_UINT(0, b.size());
  TEST_ASSERT_EQUAL_UINT(4, b.available());
  TEST_ASSERT_EQUAL_UINT(4, decltype(b)::capacity);
}

// The index type narrows with capacity. That is what keeps the buffer cheap on
// an 8266, and a widened index would be a silent size regression.
static void test_index_type_narrows_with_capacity(void) {
  TEST_ASSERT_EQUAL_size_t(1, sizeof(CircularBuffer<int, 200>::index_t));
  TEST_ASSERT_EQUAL_size_t(2, sizeof(CircularBuffer<int, 300>::index_t));
  TEST_ASSERT_EQUAL_size_t(4, sizeof(CircularBuffer<int, 70000>::index_t));
}

// --- FIFO -------------------------------------------------------------------

static void test_push_then_shift_is_fifo(void) {
  CircularBuffer<int, 4> b;
  TEST_ASSERT_TRUE(b.push(1));
  TEST_ASSERT_TRUE(b.push(2));
  TEST_ASSERT_TRUE(b.push(3));
  TEST_ASSERT_EQUAL_UINT(3, b.size());
  TEST_ASSERT_EQUAL_INT(1, b.first());
  TEST_ASSERT_EQUAL_INT(3, b.last());

  TEST_ASSERT_EQUAL_INT(1, b.shift());
  TEST_ASSERT_EQUAL_INT(2, b.shift());
  TEST_ASSERT_EQUAL_INT(3, b.shift());
  TEST_ASSERT_TRUE(b.isEmpty());
}

// push() returns false exactly when it overwrote a live element. That return
// is the only overflow signal a caller gets.
static void test_push_reports_overwrite(void) {
  CircularBuffer<int, 3> b;
  TEST_ASSERT_TRUE(b.push(1));
  TEST_ASSERT_TRUE(b.push(2));
  TEST_ASSERT_TRUE(b.push(3));
  TEST_ASSERT_TRUE(b.isFull());

  TEST_ASSERT_FALSE(b.push(4));            // 1 is gone
  TEST_ASSERT_EQUAL_UINT(3, b.size());
  TEST_ASSERT_EQUAL_INT(2, b.first());
  TEST_ASSERT_EQUAL_INT(4, b.last());
  TEST_ASSERT_EQUAL_INT(2, b[0]);
  TEST_ASSERT_EQUAL_INT(3, b[1]);
  TEST_ASSERT_EQUAL_INT(4, b[2]);
}

// --- LIFO -------------------------------------------------------------------

static void test_unshift_then_pop_is_lifo(void) {
  CircularBuffer<int, 4> b;
  TEST_ASSERT_TRUE(b.unshift(1));
  TEST_ASSERT_TRUE(b.unshift(2));
  TEST_ASSERT_TRUE(b.unshift(3));
  TEST_ASSERT_EQUAL_INT(3, b.first());
  TEST_ASSERT_EQUAL_INT(1, b.last());

  TEST_ASSERT_EQUAL_INT(1, b.pop());
  TEST_ASSERT_EQUAL_INT(2, b.pop());
  TEST_ASSERT_EQUAL_INT(3, b.pop());
  TEST_ASSERT_TRUE(b.isEmpty());
}

static void test_unshift_reports_overwrite(void) {
  CircularBuffer<int, 3> b;
  b.unshift(1); b.unshift(2); b.unshift(3);
  TEST_ASSERT_FALSE(b.unshift(4));         // the 1 at the tail is gone
  TEST_ASSERT_EQUAL_INT(4, b.first());
  TEST_ASSERT_EQUAL_INT(2, b.last());
}

// --- indexing across the wrap -----------------------------------------------

static void test_index_is_chronological_after_many_wraps(void) {
  CircularBuffer<int, 5> b;
  for (int i = 1; i <= 23; i++) b.push(i);

  TEST_ASSERT_EQUAL_UINT(5, b.size());
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_INT(19 + i, b[i]);
  TEST_ASSERT_EQUAL_INT(19, b.first());
  TEST_ASSERT_EQUAL_INT(23, b.last());

  // Out of range clamps to the tail rather than reading a stale slot.
  TEST_ASSERT_EQUAL_INT(23, b[5]);
  TEST_ASSERT_EQUAL_INT(23, b[200]);
}

static void test_copy_to_array_is_chronological(void) {
  CircularBuffer<int, 5> b;
  for (int i = 1; i <= 8; i++) b.push(i);    // holds 4..8, head mid-buffer

  int out[5] = { -1, -1, -1, -1, -1 };
  b.copyToArray(out);
  const int expect[5] = { 4, 5, 6, 7, 8 };
  TEST_ASSERT_EQUAL_INT_ARRAY(expect, out, 5);
}

static void test_copy_to_array_partial_fill(void) {
  CircularBuffer<int, 5> b;
  b.push(7); b.push(8);

  int out[5] = { -1, -1, -1, -1, -1 };
  b.copyToArray(out);
  TEST_ASSERT_EQUAL_INT(7, out[0]);
  TEST_ASSERT_EQUAL_INT(8, out[1]);
  TEST_ASSERT_EQUAL_INT(-1, out[2]);         // beyond size() is untouched
}

// --- mixed and clear --------------------------------------------------------

static void test_mixed_ends(void) {
  CircularBuffer<int, 4> b;
  b.push(2); b.push(3);
  b.unshift(1);
  TEST_ASSERT_EQUAL_UINT(3, b.size());
  TEST_ASSERT_EQUAL_INT(1, b[0]);
  TEST_ASSERT_EQUAL_INT(2, b[1]);
  TEST_ASSERT_EQUAL_INT(3, b[2]);

  TEST_ASSERT_EQUAL_INT(3, b.pop());
  TEST_ASSERT_EQUAL_INT(1, b.shift());
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  TEST_ASSERT_EQUAL_INT(2, b.first());
}

static void test_clear_then_reuse(void) {
  CircularBuffer<int, 4> b;
  for (int i = 1; i <= 9; i++) b.push(i);
  b.clear();
  TEST_ASSERT_TRUE(b.isEmpty());
  TEST_ASSERT_EQUAL_UINT(0, b.size());

  b.push(42);
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  TEST_ASSERT_EQUAL_INT(42, b.first());
  TEST_ASSERT_EQUAL_INT(42, b.last());
  TEST_ASSERT_EQUAL_INT(42, b[0]);
}

// CHARACTERISATION of vendored behaviour, not a defect. At capacity 1 push()
// on a full buffer advances head and tail together, so the slot is replaced -
// which is what overwrite-on-full should do. Recorded because this library is
// a candidate for replacement by EGRingAvg and the replacement must match.
static void test_capacity_one(void) {
  CircularBuffer<int, 1> b;
  TEST_ASSERT_TRUE(b.push(1));
  TEST_ASSERT_TRUE(b.isFull());
  TEST_ASSERT_FALSE(b.push(2));
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  TEST_ASSERT_EQUAL_INT(2, b.first());
  TEST_ASSERT_EQUAL_INT(2, b.last());
}

// --- overlap with EGRingAvg -------------------------------------------------

// Where the two overlap they must agree, or unifying them silently changes
// every average on the device. Integer type and one division at the end on
// both sides, so this is an exact compare, not a tolerance.
static void test_agrees_with_egringavg_mean(void) {
  const int W = 6;
  CircularBuffer<int32_t, 6> cb;
  EGRingAvg<int32_t, 6> ra;

  for (int32_t i = 1; i <= 300; i++) {
    int32_t v = (i * 53) % 401 - 200;
    cb.push(v);
    ra.add(v);

    TEST_ASSERT_EQUAL_UINT((unsigned)ra.count(), (unsigned)cb.size());

    int32_t sum = 0;
    for (decltype(cb)::index_t k = 0; k < cb.size(); k++) sum += cb[k];
    TEST_ASSERT_EQUAL_INT32(sum, ra.sum());
    TEST_ASSERT_EQUAL_INT32(sum / (int32_t)cb.size(), ra.get());

    // Chronological order matches slot for slot.
    for (decltype(cb)::index_t k = 0; k < cb.size(); k++) {
      TEST_ASSERT_EQUAL_INT32(cb[k], ra.at((uint16_t)k));
    }
    TEST_ASSERT_EQUAL_INT32(cb.last(), ra.last());
    (void)W;
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_state);
  RUN_TEST(test_index_type_narrows_with_capacity);
  RUN_TEST(test_push_then_shift_is_fifo);
  RUN_TEST(test_push_reports_overwrite);
  RUN_TEST(test_unshift_then_pop_is_lifo);
  RUN_TEST(test_unshift_reports_overwrite);
  RUN_TEST(test_index_is_chronological_after_many_wraps);
  RUN_TEST(test_copy_to_array_is_chronological);
  RUN_TEST(test_copy_to_array_partial_fill);
  RUN_TEST(test_mixed_ends);
  RUN_TEST(test_clear_then_reuse);
  RUN_TEST(test_capacity_one);
  RUN_TEST(test_agrees_with_egringavg_mean);
  return UNITY_END();
}
