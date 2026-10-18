/*
  test_outputvars - user templates and /vars.json over the measurement rows.

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
#include <math.h>
#include <string.h>
#include <string>

#include "Corium/Measure/OutputVars.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"

// Stand-ins

static float   g_temp_c = 21.5f;
static bool    g_env    = true;

static bool r_cpm(EGMValue* v)   { v->i = 25; return true; }
static bool r_usv(EGMValue* v)   { v->f = 0.0812f; return true; }
static bool r_tc(EGMValue* v)    { v->u = 4000000000u; return true; }
static bool r_name(EGMValue* v)  { v->s = "Garage \"A\""; return true; }
static bool r_tube(EGMValue* v)  { v->b = true; return true; }
static bool r_t(EGMValue* v) {
  if (!g_env) return false;
  v->f = g_temp_c;
  return true;
}
static bool r_json_only(EGMValue* v) { v->u = 7; return true; }

#define ROW(id, read, kind, dec, unit, out) \
  {id, "", nullptr, nullptr, nullptr, read, kind, dec, unit, out, 0}

static const EGMeasure CORE[] PROGMEM = {
  ROW("cpm",   r_cpm,       EGM_I32,  0, EGMU_CPM,     EGM_OUT_VARS),
  ROW("usv",   r_usv,       EGM_F32,  4, EGMU_USVH,    EGM_OUT_VARS),
  ROW("tc",    r_tc,        EGM_U32,  0, EGMU_COUNT,   EGM_OUT_VARS),
  ROW("name",  r_name,      EGM_STR,  0, EGMU_NONE,    EGM_OUT_VARS),
  ROW("tube",  r_tube,      EGM_BOOL, 0, EGMU_NONE,    EGM_OUT_VARS),
  ROW("t",     r_t,         EGM_F32,  2, EGMU_CELSIUS, EGM_OUT_VARS),
  ROW("jonly", r_json_only, EGM_U32,  0, EGMU_NONE,    EGM_OUT_JSON),
};

EGMeasureRows egm_core_rows() { return EGMeasureRows(CORE); }
EGMeasureRows egm_input_rows() { return EGMeasureRows(); }
uint8_t EGModuleRegistry::count() { return 0; }
EGModule* EGModuleRegistry::get(uint8_t) { return nullptr; }
bool EGModuleRegistry::quarantined(EGModule*) { return false; }

static std::string tpl(const char* t, size_t cap = 128) {
  char buf[128];
  size_t n = OutputVars::renderTemplate(t, buf, cap);
  return std::string(buf, n);
}

void setUp(void) {
  g_temp_c = 21.5f;
  egm_set_temp_unit(EGMT_C);
  g_env = true;
}
void tearDown(void) {}

// Plain variables

static void serial_cpm(void) {
  TEST_ASSERT_EQUAL_STRING("25\r\n", tpl("{cpm}\r\n").c_str());
  TEST_ASSERT_EQUAL_STRING("CPM: 25\n", tpl("CPM: {cpm}\n").c_str());
}

static void each_kind_prints(void) {
  TEST_ASSERT_EQUAL_STRING("0.0812 4000000000 1",
                           tpl("{usv} {tc} {tube}").c_str());
  TEST_ASSERT_EQUAL_STRING("Garage \"A\"", tpl("{name}").c_str());
}

static void unknown_literal(void) {
  TEST_ASSERT_EQUAL_STRING("{nope} {jonly}", tpl("{nope} {jonly}").c_str());
  g_env = false;
  TEST_ASSERT_EQUAL_STRING("{t}", tpl("{t}").c_str());
}

static void long_name(void) {
  TEST_ASSERT_EQUAL_STRING("{cpmcpmcpmcpm}", tpl("{cpmcpmcpmcpm}").c_str());
}

static void escapes_still_work(void) {
  TEST_ASSERT_EQUAL_STRING("{cpm}=25\t\\", tpl("\\{cpm\\}={cpm}\\t\\\\").c_str());
}

// specs

static void spec_sets_decimals(void) {
  TEST_ASSERT_EQUAL_STRING("0.08", tpl("{usv:2}").c_str());
  TEST_ASSERT_EQUAL_STRING("0.081200", tpl("{usv:6}").c_str());
}

static void temp_unit(void) {
  TEST_ASSERT_EQUAL_STRING("21.50", tpl("{t}").c_str());
  egm_set_temp_unit(EGMT_F);
  TEST_ASSERT_EQUAL_STRING("70.70", tpl("{t}").c_str());
  egm_set_temp_unit(EGMT_K);
  TEST_ASSERT_EQUAL_STRING("294.65", tpl("{t}").c_str());
}

static void spec_unit(void) {
  egm_set_temp_unit(EGMT_F);
  TEST_ASSERT_EQUAL_STRING("21.50", tpl("{t:C}").c_str());
  TEST_ASSERT_EQUAL_STRING("294.6", tpl("{t:K1}").c_str());
  egm_set_temp_unit(EGMT_C);
  TEST_ASSERT_EQUAL_STRING("70.7", tpl("{t:F1}").c_str());
}

static void bad_spec(void) {
  TEST_ASSERT_EQUAL_STRING("{usv:}", tpl("{usv:}").c_str());
  TEST_ASSERT_EQUAL_STRING("{usv:0}", tpl("{usv:0}").c_str());
  TEST_ASSERT_EQUAL_STRING("{usv:22}", tpl("{usv:22}").c_str());
  TEST_ASSERT_EQUAL_STRING("{usv:x}", tpl("{usv:x}").c_str());
  TEST_ASSERT_EQUAL_STRING("{usv:2F}", tpl("{usv:2F}").c_str());
  TEST_ASSERT_EQUAL_STRING("{:2}", tpl("{:2}").c_str());
}

static void spec_row(void) {
  TEST_ASSERT_EQUAL_STRING("{cpm:2}", tpl("{cpm:2}").c_str());
  TEST_ASSERT_EQUAL_STRING("{usv:F}", tpl("{usv:F}").c_str());
  TEST_ASSERT_EQUAL_STRING("{name:C}", tpl("{name:C}").c_str());
}

// Render and format

static void unterminated_key(void) {
  char buf[16];
  const char* src = "usv:2}trailing";
  size_t n = OutputVars::render(src, 5, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0.08", std::string(buf, n).c_str());
}

static void render_too_small(void) {
  char buf[4];
  TEST_ASSERT_EQUAL_size_t(0, OutputVars::render("usv", 3, buf, sizeof(buf)));
}

static void format_unit(void) {
  egm_set_temp_unit(EGMT_F);
  EGMeasureRows rows(CORE);
  EGMeasure t = rows.row(5);
  EGMValue v;
  v.f = 21.5f;
  char buf[16];
  size_t n = egm_format_display(t, v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("70.70", std::string(buf, n).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(serial_cpm);
  RUN_TEST(each_kind_prints);
  RUN_TEST(unknown_literal);
  RUN_TEST(long_name);
  RUN_TEST(escapes_still_work);
  RUN_TEST(spec_sets_decimals);
  RUN_TEST(temp_unit);
  RUN_TEST(spec_unit);
  RUN_TEST(bad_spec);
  RUN_TEST(spec_row);
  RUN_TEST(unterminated_key);
  RUN_TEST(render_too_small);
  RUN_TEST(format_unit);
  return UNITY_END();
}
