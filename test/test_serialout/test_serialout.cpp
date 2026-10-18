/*
  test_serialout - SerialOut labelled lines.

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
#include "sout_fakes.h"
#include <unity.h>
#include "SerialOut/SerialOut.h"

void setUp(void) {
  gcounter.cpm = 23;
  gcounter.cps = 0.5f;
  gcounter.usv = 0.13f;
}
void tearDown(void) {}

static uint8_t shown = 0;
static unsigned long t = 0;

static const char* line(uint8_t want) {
  if ((shown ^ want) & SerialOut::SHOW_CPM) serialout.toggle_cpm();
  if ((shown ^ want) & SerialOut::SHOW_CPS) serialout.toggle_cps();
  if ((shown ^ want) & SerialOut::SHOW_USV) serialout.toggle_usv();
  shown = want;
  serialout.setInterval(1);
  Serial.n = 0;
  Serial.out[0] = 0;
  t += 5000;
  serialout.loop(t);
  return Serial.out;
}

static void cpm() {
  TEST_ASSERT_EQUAL_STRING("CPM: 23\n", line(SerialOut::SHOW_CPM));
}

static void all() {
  TEST_ASSERT_EQUAL_STRING("CPM: 23 CPS: 0.50 uSv: 0.13\n",
    line(SerialOut::SHOW_CPM | SerialOut::SHOW_CPS | SerialOut::SHOW_USV));
}

static void usv_only() {
  TEST_ASSERT_EQUAL_STRING("uSv: 0.13\n", line(SerialOut::SHOW_USV));
}

static void none() {
  TEST_ASSERT_EQUAL_STRING("", line(0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(cpm);
  RUN_TEST(all);
  RUN_TEST(usv_only);
  RUN_TEST(none);
  return UNITY_END();
}
