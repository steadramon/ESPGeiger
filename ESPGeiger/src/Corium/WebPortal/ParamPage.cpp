/*
  ParamPage.cpp - /param, the Config tabs, and the Backup tab's export and import.

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
#include "WebPortal.h"
#include "PARAM_PAGE_JS.gz.h"
#include "ImportText.h"
#include "PortalPaths.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/System/PinSafety.h"
#include <EGEscape.h>
#include <EGHttpForm.h>
#include "Corium/Logger/Logger.h"
#include <EGBase64.h>

// a literal from flash, dropped if it won't fit
#define APPEND_LIT(buf, pos, cap, lit) do {                              \
  size_t _n = sizeof(lit) - 1;                                           \
  if ((pos) + _n < (cap)) { memcpy_P((buf)+(pos), PSTR(lit), _n); (pos) += _n; } \
} while (0)

// POST body for /param and /import
struct BodyBuf {
  char*  buf;
  size_t len;
  size_t cap;
  bool   ok;
};

static void body_free(BodyBuf& b) {
  if (b.buf) { free(b.buf); b.buf = nullptr; }
  b.len = 0;
  b.cap = 0;
  b.ok  = false;
}

static constexpr size_t BODY_MAX = 8192;   // largest body held, it's heap

static bool body_size_ok(size_t cap) {
  return cap != 0 && cap <= BODY_MAX;
}

static bool body_alloc(BodyBuf& b, size_t cap) {
  b.buf = (char*)malloc(cap + 1);
  if (!b.buf) return false;
  b.cap = cap;
  b.ok  = true;
  return true;
}

static void body_append(BodyBuf& b, const char* chunk, size_t len) {
  if (b.ok && b.buf && b.len + len <= b.cap) {
    memcpy(b.buf + b.len, chunk, len);
    b.len += len;
  }
}

static void body_end(BodyBuf& b) {
  if (b.buf) b.buf[b.len] = '\0';
}

// path nullptr = quiet
static void body_begin(BodyBuf& b, size_t cap, PGM_P path) {
  body_free(b);
  if (!body_size_ok(cap)) {
    if (path) corium::Log::console(PSTR("%s: body size %u out of range"), path, (unsigned)cap);
    return;
  }
  if (!body_alloc(b, cap) && path) {
    corium::Log::console(PSTR("%s: malloc(%u) failed"), path, (unsigned)(cap + 1));
  }
}

// no buffer, the rest isn't read
static bool body_event(BodyBuf& b, EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                       const char* chunk, size_t len, PGM_P path) {
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: body_begin(b, req.bodyLen(), path); break;
    case EGHttpServer::BODY_DATA:  body_append(b, chunk, len);         break;
    case EGHttpServer::BODY_END:   body_end(b);                        break;
    case EGHttpServer::BODY_ABORT: body_free(b);                       break;
  }
  return b.buf != nullptr;
}

// freed in hParam
static BodyBuf s_param_body;

bool corium::WebPortal::hParamBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                            const char* chunk, size_t len, void*) {
  return body_event(s_param_body, req, ev, chunk, len, P_PARAM);
}

// first field a save refused, and first saved with a warning, shown above the form
struct Refusal {
  char name[2 * CORIUM_PREF_ID_MAX + 2];   // mid.id
  char why[corium::PinSafety::WHY_MAX];       // copied, the next put reuses "in use"
  char note_name[2 * CORIUM_PREF_ID_MAX + 2];
  const char* note;                   // PROGMEM
};

static void param_put(const char* mid, const char* id, const char* name,
                      const char* v, Refusal& r) {
  if (corium::Prefs::put(mid, id, v)) {
    if (r.note || !corium::Prefs::warned()) return;
    strlcpy(r.note_name, name, sizeof(r.note_name));
    r.note = corium::Prefs::warned();
    return;
  }
  if (r.why[0]) return;
  strncpy(r.name, name, sizeof(r.name) - 1);
  r.name[sizeof(r.name) - 1] = '\0';
  strncpy_P(r.why, corium::Prefs::refused() ? corium::Prefs::refused() : PSTR("not valid"), sizeof(r.why) - 1);
}

static void param_save_pref(const char* mid, const corium::Pref& p,
                            const char* body, size_t len, Refusal& r) {
  if (p.flags & (corium::P_HIDDEN | corium::P_READONLY)) return;
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) return;
  char name[2 * CORIUM_PREF_ID_MAX + 2];   // mid.id
  char id[CORIUM_PREF_ID_MAX + 1];
  char field[CORIUM_PREFS_MAX_VALUE + 1];
  flashToRam(p.id, id, sizeof(id));
  snprintf_P(name, sizeof(name), PSTR("%s.%s"), mid, id);

  const char* v = EGHttpRequest::decodeArg(body, len, name, field, sizeof(field));
  if (p.type == corium::P_BOOL) {
    v = v ? "1" : "0";
  } else if (!v) {
    return;
  } else if (p.flags & corium::P_SENSITIVE) {
    if (strcmp_P(v, PSTR("__CLEAR__")) == 0) v = "";
    else if (v[0] == '\0') return;
  }
  param_put(mid, id, name, v, r);
}

// false = nothing saved; out of line, its buffers aren't on the stack under commit()
static __attribute__((noinline)) bool param_save_body(uint8_t tab, Refusal& r) {
  const char* body = s_param_body.buf;
  size_t      len  = s_param_body.len;
  bool        ok   = s_param_body.ok && body;

  if (ok) {
    for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
      const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
      // this tab only, else other bools clear
      if ((g->category.get() & corium::P_CAT_TAB) != tab) continue;
      // not on the page, a missing bool would save as off
      corium::Module* m = corium::Prefs::moduleAt(gi);
      if (m->displayOrder() == 0) continue;
      for (size_t j = 0; j < g->count.get(); j++) {
        const corium::Pref p = g->prefs.row(j);
        param_save_pref(g->moduleId, p, body, len, r);
      }
    }
  }
  body_free(s_param_body);
  return ok;
}

#ifndef APP_PARAM_TABS
#define APP_PARAM_TABS "System", "Input", "Output", "Upload", "Backup"   // app sets it
#endif

static constexpr uint8_t TABS = corium::P_CAT_BACKUP + 1;

static void param_render_tabs(EGHttpResponse& res, uint8_t tab) {
  static const char TAB_NAMES[TABS][7] PROGMEM = {APP_PARAM_TABS};
  res.sendChunk(F("<nav class=tabs>"));
  for (uint8_t t = 0; t < TABS; t++) {
    corium::WebPortal::sendf(res, PSTR("<a href='/param?t=%u'%s>%s</a>"),
      (unsigned)t, t == tab ? PSTR(" class=ac aria-current=page") : PSTR(""), TAB_NAMES[t]);
  }
  res.sendChunk(F("</nav>"));
}

static void param_render_backup(EGHttpResponse& res) {
  res.sendChunk(F(
    "<form method=POST action=/import id=impF>"
    "<p><textarea name=blob id=cfgB rows=5 style=\"width:100%;font-family:monospace;font-size:.85em\" placeholder=\"Click Export to read current config, or paste a blob here to import\" required></textarea></p>"
    "<p><button type=button id=expB>Export</button>"
    " <button type=submit>Import</button></p></form>"
    "<p style=\"color:var(--muted);font-size:.85em\">WiFi and network settings are preserved. Device restarts automatically after import.</p>"
    "<p class=\"a aw\" style=\"font-size:.85em\"><b>Note:</b> The exported blob contains private information. Keep it private.</p>"));
}

static bool param_group_on(uint8_t gi) {
  return corium::Prefs::moduleAt(gi)->hwPresent() &&
         corium::Prefs::groupEnabled(corium::Prefs::groupAt(gi));
}

static uint8_t param_group_order(uint8_t gi) {
  return corium::Prefs::moduleAt(gi)->displayOrder();
}

// on groups first, then displayOrder; insertion sort, keeps ties in order
static void param_sort_groups(uint8_t* order, uint8_t gc) {
  static constexpr uint16_t OFF_LAST = 0x100;
  uint16_t key[CORIUM_MAX_MODULES];
  for (uint8_t i = 0; i < gc; i++) {
    order[i] = i;
    key[i] = (param_group_on(i) ? 0 : OFF_LAST) | param_group_order(i);
  }
  for (uint8_t i = 1; i < gc; i++) {
    uint8_t cur = order[i];
    uint8_t j = i;
    while (j > 0 && key[order[j - 1]] > key[cur]) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = cur;
  }
}

struct ParamCtx {
  EGHttpResponse* res;
  const char* mid;
  const char* cur;
  PGM_P       disabled;
  size_t      pos;
  const corium::PrefShow* show;
  char        buf[512];
  char        id[CORIUM_PREF_ID_MAX + 1], lbl[80], help[120], pat[CORIUM_PREF_PATTERN_MAX + 1];
  char        attr[2 * CORIUM_PREF_ID_MAX + 32];   // data-w='mid.key' data-m=N
};

static void param_appendf(ParamCtx& c, PGM_P fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(c.buf + c.pos, sizeof(c.buf) - c.pos, fmt, ap);
  va_end(ap);
  advancePos(c.pos, n, sizeof(c.buf));
}

// one chunk from the start of buf
static void param_sendf(ParamCtx& c, PGM_P fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(c.buf, sizeof(c.buf), fmt, ap);
  va_end(ap);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
}

// for the form script, "" = always shown
static const char* show_attr(ParamCtx& c, const char* id) {
  corium::PrefShow r;
  c.attr[0] = '\0';
  if (corium::Prefs::showRule(c.show, id, &r)) {
    snprintf_P(c.attr, sizeof(c.attr), PSTR(" data-w='%s' data-m=%u"),
               r.when, (unsigned)r.values);
  }
  return c.attr;
}

static void param_group_header(ParamCtx& c, const corium::PrefGroup* g, corium::Module* mod) {
  bool has_toggle = (g->enableKey != nullptr);
  bool grp_on = corium::Prefs::groupEnabled(g);
  bool hw_absent = (mod && !mod->hwPresent());
  bool off = hw_absent;
  PGM_P badge = PSTR("");
  if (hw_absent) {
    badge = PSTR(" <span class='gb off'>not detected</span>");
  } else if (has_toggle) {
    off = !grp_on;
    badge = grp_on ? PSTR(" <span class='gb on'>on</span>") : PSTR(" <span class='gb off'>off</span>");
  }
  param_sendf(c, PSTR("<details class='bx%s'%s><summary>%s%s</summary>"),
              off ? PSTR(" off") : PSTR(""), show_attr(c, nullptr), g->label, badge);
}

static void param_emit_bool(ParamCtx& c, const char* lbl, bool inline_bool) {
  PGM_P checked = corium::Prefs::getBool(c.mid, c.id) ? W_CHECKED : PSTR("");
  if (inline_bool) {
    param_appendf(c,
      PSTR("<label class='il' title='%s'><input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s>%s</label>"),
      c.help, c.mid, c.id, c.mid, c.id, checked, c.disabled, lbl);
  } else {
    param_appendf(c,
      PSTR("<input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s><br>"),
      c.mid, c.id, c.mid, c.id, checked, c.disabled);
  }
}

// sent per option, a long list can't overflow buf
static void param_emit_options(ParamCtx& c, const corium::Pref& p) {
  int32_t cur_idx = 0, def_idx = -1;
  parseI32(c.cur, &cur_idx);
  parseI32(p.defaultVal, &def_idx);
  const char* opt = c.pat;
  long idx = p.minI;
  while (*opt) {
    const char* sep = strchr(opt, '|');
    size_t olen = sep ? (size_t)(sep - opt) : strlen(opt);
    // blank holds a number this build hasn't got
    if (olen) {
      param_sendf(c, PSTR("<option value=%ld%s>%.*s%s</option>"),
                  idx, (idx == cur_idx) ? PSTR(" selected") : PSTR(""),
                  (int)olen, opt, (idx == def_idx) ? PSTR(" (default)") : PSTR(""));
    }
    if (!sep) break;
    opt = sep + 1;
    idx++;
  }
}

static void param_emit_enum(ParamCtx& c, const corium::Pref& p) {
  if (c.pos) { c.res->sendChunk(c.buf, c.pos); c.pos = 0; }
  param_sendf(c, PSTR("<select id='%s.%s' name='%s.%s'%s>"),
              c.mid, c.id, c.mid, c.id, c.disabled);
  param_emit_options(c, p);
  param_sendf(c, PSTR("</select><br>"));
  c.pos = 0;
}

static PGM_P param_input_type(const corium::Pref& p, bool slider) {
  if (slider) return PSTR("range");
  if (p.type == corium::P_INT || p.type == corium::P_UINT || p.type == corium::P_FLOAT) return PSTR("number");
  if (p.type == corium::P_STRING && (p.flags & corium::P_SENSITIVE)) return PSTR("password");
  if (p.type == corium::P_STRING && (p.flags & corium::P_TIME))      return PSTR("time");
  return PSTR("text");
}

// html grows 6x, a cut value would be saved cut
static constexpr size_t ESC_PIECE = sizeof(ParamCtx::buf) / 6;

static void param_send_escaped(ParamCtx& c, const char* s) {
  char piece[ESC_PIECE + 1];
  for (size_t left = strlen(s); left; ) {
    size_t k = left < ESC_PIECE ? left : ESC_PIECE;
    // whole chars, a cut one comes back as two U+FFFD
    while (k < left && k > 1 && (s[k] & 0xC0) == 0x80) k--;
    memcpy(piece, s, k);
    piece[k] = '\0';
    c.res->sendChunk(c.buf, egesc_html(c.buf, sizeof(c.buf), piece));
    s += k;
    left -= k;
  }
}

static void param_emit_value(ParamCtx& c, const corium::Pref& p, bool slider) {
  if (p.flags & corium::P_SENSITIVE) {
    PGM_P ph = c.cur[0] ? PSTR("(unchanged \xE2\x80\x94 leave blank)") : PSTR("(not set)");
    param_appendf(c, PSTR(" value='' placeholder='%s'"), ph);
    return;
  }
  if (c.pos) { c.res->sendChunk(c.buf, c.pos); c.pos = 0; }
  c.res->sendChunk(F(" value='"));
  param_send_escaped(c, c.cur);
  c.buf[c.pos++] = '\'';
  if (!slider && p.defaultVal && p.defaultVal[0]
      && !(p.flags & corium::P_TIME)) {
    param_appendf(c, PSTR(" placeholder='%s'"), p.defaultVal);
  }
}

static void param_emit_limits(ParamCtx& c, const corium::Pref& p) {
  if (p.type == corium::P_STRING) {
    if (p.maxLen > 0) param_appendf(c, PSTR(" maxlength='%u'"), (unsigned)p.maxLen);
    if (p.minI > 0) param_appendf(c, PSTR(" minlength='%ld'"), (long)p.minI);
    if (p.pattern && c.pat[0]) param_appendf(c, PSTR(" pattern='%s'"), c.pat);
    return;
  }
  if ((p.type == corium::P_INT || p.type == corium::P_UINT || p.type == corium::P_FLOAT) && p.minI != p.maxI) {
    param_appendf(c, PSTR(" min=%ld max=%ld"), (long)p.minI, (long)p.maxI);
  }
}

static void param_emit_input_end(ParamCtx& c, const corium::Pref& p, bool slider) {
  if (p.type == corium::P_FLOAT) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " step=any");
  if (!slider && (p.type == corium::P_INT || p.type == corium::P_UINT)) {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " inputmode=numeric");
  } else if (p.type == corium::P_FLOAT) {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " inputmode=decimal");
  }
  if (p.flags & corium::P_REQUIRED) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " required");
  if (p.flags & corium::P_READONLY) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " readonly");
  if (slider) {
    param_appendf(c, PSTR("><output>%s</output><br>"), c.cur);
  } else {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), "><br>");
  }
}

static void param_emit_input(ParamCtx& c, const corium::Pref& p) {
  const bool slider = (p.type == corium::P_INT || p.type == corium::P_UINT)
                   && (p.flags & corium::P_SLIDER)
                   && p.minI != p.maxI;
  param_appendf(c, PSTR("<input type=%s id='%s.%s' name='%s.%s'"),
                param_input_type(p, slider), c.mid, c.id, c.mid, c.id);
  param_emit_value(c, p, slider);
  param_emit_limits(c, p);
  param_emit_input_end(c, p, slider);
}

static void param_emit_help(ParamCtx& c, const corium::Pref& p, bool inline_bool) {
  if (!inline_bool && c.help[0]) {
    param_appendf(c, PSTR("<small>%s</small>"), c.help);
  }
  if ((p.flags & corium::P_SENSITIVE) && c.cur[0]) {
    param_appendf(c,
      PSTR("<button type=button class='danger btn-sm clr' data-k='%s.%s' "
           "style='margin-top:.2em'>Clear</button>"),
      c.mid, c.id);
  }
  c.res->sendChunk(c.buf, c.pos);
}

static void param_load_text(ParamCtx& c, const corium::Pref& p) {
  flashToRam(p.id,      c.id,   sizeof(c.id));
  flashToRam(p.label,   c.lbl,  sizeof(c.lbl));
  flashToRam(p.help,    c.help, sizeof(c.help));
  flashToRam(p.pattern, c.pat,  sizeof(c.pat));
  // from the flag, no row types it
  if (p.flags & corium::P_REBOOT) {
    size_t n = strlen(c.help);
    if (n && c.help[n - 1] != '.' && n + 1 < sizeof(c.help)) c.help[n++] = '.';   // help with no full stop
    snprintf_P(c.help + n, sizeof(c.help) - n, n ? PSTR(" %s.") : PSTR("%s."), corium::H_RBA);
  }
}

static void param_emit_heading(ParamCtx& c, const corium::Pref& p) {
  param_sendf(c, p.type == corium::P_LABEL ? PSTR("<label>%s</label>") : PSTR("<h4>%s</h4>"), c.lbl);
}

static void param_emit_label(ParamCtx& c, const char* lbl) {
  int n = snprintf_P(c.buf, sizeof(c.buf), PSTR("<label for='%s.%s'>%s</label>"), c.mid, c.id, lbl);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.pos = (size_t)n;
}

static void param_emit_row(ParamCtx& c, const corium::Pref& p);

// a row with a show rule sits in its own div
static void param_emit_field(ParamCtx& c, const corium::Pref& p) {
  param_load_text(c, p);
  bool wrap = show_attr(c, c.id)[0];
  if (wrap) {
    c.res->sendChunk(F("<div"));
    c.res->sendChunk(c.attr);
    c.res->sendChunk(F(">"));
  }
  param_emit_row(c, p);
  if (wrap) c.res->sendChunk(F("</div>"));
}

static void param_emit_row(ParamCtx& c, const corium::Pref& p) {
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) {
    param_emit_heading(c, p);
    return;
  }

  c.cur = corium::Prefs::getString(c.mid, c.id);
  const char* lbl = p.label ? c.lbl : c.id;
  c.disabled = (p.flags & corium::P_READONLY) ? PSTR(" disabled") : PSTR("");
  bool inline_bool = (p.type == corium::P_BOOL) && (p.flags & corium::P_INLINE);
  c.pos = 0;
  if (!inline_bool) param_emit_label(c, lbl);

  if (p.type == corium::P_BOOL)      param_emit_bool(c, lbl, inline_bool);
  else if (p.type == corium::P_ENUM) param_emit_enum(c, p);
  else                         param_emit_input(c, p);
  param_emit_help(c, p, inline_bool);
}

static void param_render_group(ParamCtx& c, const corium::PrefGroup* g, corium::Module* mod) {
  param_group_header(c, g, mod);
  c.mid = g->moduleId;

  for (size_t j = 0; j < g->count.get(); j++) {
    const corium::Pref p = g->prefs.row(j);
    if (p.flags & (corium::P_HIDDEN | corium::P_ADVANCED)) continue;
    param_emit_field(c, p);
  }
  bool adv_open = false;
  for (size_t j = 0; j < g->count.get(); j++) {
    const corium::Pref p = g->prefs.row(j);
    if ((p.flags & corium::P_HIDDEN) || !(p.flags & corium::P_ADVANCED)) continue;
    if (!adv_open) {
      c.res->sendChunk(F("<details class=adv><summary>Advanced</summary>"));
      adv_open = true;
    }
    param_emit_field(c, p);
  }
  if (adv_open) c.res->sendChunk(FPSTR(W_DETAILS_END));
  c.res->sendChunk(FPSTR(W_DETAILS_END));
}

static void param_render_groups(EGHttpResponse& res, uint8_t tab) {
  ParamCtx c;
  c.res = &res;
  uint8_t gc = (uint8_t)corium::Prefs::groupCount();
  uint8_t order[CORIUM_MAX_MODULES];
  if (gc > sizeof(order)) gc = sizeof(order);
  param_sort_groups(order, gc);

  for (uint8_t gi = 0; gi < gc; gi++) {
    corium::Module* mod = corium::Prefs::moduleAt(order[gi]);
    if (mod->displayOrder() == 0) continue;   // has its own page
    const corium::PrefGroup* g = corium::Prefs::groupAt(order[gi]);
    if ((g->category.get() & corium::P_CAT_TAB) != tab) continue;
    c.show = mod->prefsShow();
    param_render_group(c, g, mod);
  }
}

#if !EG_GZ_PARAM_PAGE_JS
extern const char PARAM_PAGE_JS[] PROGMEM = R"JS(
document.forms[0].addEventListener('invalid',e=>{for(let n=e.target;n;n=n.parentNode)n.tagName=='DETAILS'&&(n.open=1);fErr.style.display=''},true);
function sw(){Q('[data-w]').forEach(e=>{var c=byID(e.dataset.w);if(c)e.hidden=!(e.dataset.m>>(c.type=='checkbox'?+c.checked:+c.value)&1)})}
addEventListener('change',sw);sw();
Q('.clr').forEach(b=>b.onclick=()=>{if(confirm('Clear stored value?')){byID(b.dataset.k).value='__CLEAR__';b.form.submit()}});
Q('input[type=range]').forEach(r=>r.oninput=()=>r.nextElementSibling.value=r.value);
var x=byID('expB');if(x)x.onclick=()=>fetch('/export').then(r=>r.text()).then(t=>{var e=byID('cfgB');e.value=t;e.select()});
var im=byID('impF');if(im)im.onsubmit=()=>confirm('Import config and restart?');
var w=byID('sys.web_pass');if(w){var c=mk('input');c.type='password';c.placeholder='Confirm password';var k=()=>c.setCustomValidity(c.value!=w.value?'Passwords do not match':'');c.oninput=k;w.addEventListener('input',k);w.after(c)}
)JS";
#endif

void corium::WebPortal::hParamJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_PARAM_PAGE_JS
  sendGz(res, FPSTR(HTTP_CT_JS), PARAM_PAGE_JS_GZ, PARAM_PAGE_JS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(PARAM_PAGE_JS));
#endif
}

// out of line, the form's buffers aren't on the stack under commit()
static __attribute__((noinline)) void param_render_form(EGHttpResponse& res, uint8_t tab) {
  corium::WebPortal::sendf(res, PSTR("<form method=POST action='/param?t=%u'>"), (unsigned)tab);
  param_render_groups(res, tab);
  res.sendChunk(F(
    "<hr><div id=fErr class='a ae' style=display:none>"
    "Some fields need fixing - check expanded sections.</div>"
    "<button type=submit>Save</button></form>"));
}

static uint8_t param_tab(EGHttpRequest& req) {
  int32_t t;
  if (parseI32(req.arg(F("t")), &t) && t >= 0 && t < TABS) return (uint8_t)t;
  return (uint8_t)corium::P_CAT_SYSTEM;
}

static const char RESTART_APPLY[] PROGMEM = " <a href=/restart>Restart</a> to apply.";

static PGM_P link_once(bool& link, PGM_P text) {
  if (!link) return PSTR("");
  link = false;
  return text;
}

// what a post did, above the form; frozen = a group left out, the rest saved
static void param_says(EGHttpResponse& res, bool saved, bool wrote, bool restart,
                       const Refusal& refused, const char* frozen) {
  if (wrote) res.sendChunk(F(
    "<div class='a ak'>Saved.</div>"));
  else if (saved) corium::WebPortal::sendNotSaved(res);
  bool link = restart;
  if (refused.why[0]) {
    corium::WebPortal::sendf(res, PSTR("<div class='a ae'>Not saved: %s, %s.%s</div>"),
               refused.name, refused.why,
               link_once(link, PSTR(" <a href=/restart>Restart</a> for the rest.")));
  }
  if (refused.note) {
    corium::WebPortal::sendf(res, PSTR("<div class='a aw'>%s, %s.%s</div>"),
               refused.note_name, refused.note, link_once(link, RESTART_APPLY));
  }
  if (frozen) {
    corium::WebPortal::sendf(res, PSTR("<div class='a aw'>%s.%s</div>"), frozen,
               link_once(link, RESTART_APPLY));
  }
  if (!saved) res.sendChunk(F(
    "<div class='a ae'>Not saved: the form was too large or memory was short.</div>"));
}

void corium::WebPortal::hParam(EGHttpRequest& req, EGHttpResponse& res, void*) {
  uint8_t tab = param_tab(req);
  bool posted = (req.method() == EGHttpRequest::POST);
  Refusal refused{};
  bool saved = posted && param_save_body(tab, refused);
  bool wrote = saved && corium::Prefs::commit();
  // every tab row is put, a frozen group on it mustn't read as not saved
  char why[CORIUM_PREFS_FROZEN_MAX];
  const char* frozen = saved && !wrote && corium::Prefs::frozen(why, sizeof(why)) ? why : nullptr;
  if (frozen) wrote = true;
  // stay, so the refusal or note shows
  bool restart = wrote && corium::Prefs::restartPending();
  if (restart && !refused.why[0] && !refused.note && !frozen) {
    res.redirect(FPSTR(P_RESTART));
    return;
  }

  corium::WebPortal::beginPage(res, F("Config"));
  if (posted) param_says(res, saved, wrote, restart, refused, frozen);
  param_render_tabs(res, tab);

  // Backup is the import form only
  if (tab == (uint8_t)corium::P_CAT_BACKUP) param_render_backup(res);
  else                                param_render_form(res, tab);
  res.sendChunk(F("<script src=/paramjs" CORIUM_CACHE_BUST "></script>"));
  corium::WebPortal::endPage(res);
}

// crc32 (reflected), a bit at a time; mask is all ones when the low bit is set
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      uint32_t mask = -(int32_t)(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~crc;
}

static constexpr uint32_t EXPORT_OBF_SEED = CORIUM_EXPORT_SEED;
// magic, version, groups, crc
static constexpr size_t  BLOB_MAGIC = 3, BLOB_HEAD = BLOB_MAGIC + 1, BLOB_CRC = 4;
static constexpr uint8_t BLOB_VER = 1;

// base64 straight into the response, crc over the bytes as sent
struct B64Sink {
  EGHttpResponse* res;
  uint8_t  tri[3];
  uint8_t  tri_n;
  char     out[256];
  size_t   out_len;
  uint32_t crc;
  uint32_t obf_state;
  bool     obf_on;
};

static void b64_init(B64Sink& s, EGHttpResponse& r) {
  s.res = &r; s.tri_n = 0; s.out_len = 0; s.crc = 0;
  s.obf_state = EXPORT_OBF_SEED; s.obf_on = false;
}

// xorshift keystream byte, keeps the blob unreadable at a glance, not crypto
static inline uint8_t obf_step(uint32_t* st) {
  uint32_t x = xorshift32(*st);
  *st = x;
  return (uint8_t)(x & 0xFF);
}
static void b64_flush_out(B64Sink& s) {
  if (s.out_len) { s.res->sendChunk(s.out, s.out_len); s.out_len = 0; }
}
static void b64_emit_quartet(B64Sink& s, uint8_t in_len) {
  if (s.out_len + 4 > sizeof(s.out)) b64_flush_out(s);
  // writes a NUL too
  unsigned char quad[5];
  encode_base64(s.tri, in_len, quad);
  memcpy(s.out + s.out_len, quad, 4);
  s.out_len += 4;
  s.tri_n = 0;
}
static void b64_put(B64Sink& s, uint8_t b) {
  s.tri[s.tri_n++] = b;
  if (s.tri_n == 3) b64_emit_quartet(s, 3);
}
static void b64_write(B64Sink& s, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = p[i];
    if (s.obf_on) b ^= obf_step(&s.obf_state);
    s.crc = crc32_update(s.crc, &b, 1);
    b64_put(s, b);
  }
}
static void b64_write_u8(B64Sink& s, uint8_t v) { b64_write(s, &v, 1); }
static void b64_finish(B64Sink& s) {
  if (s.tri_n > 0) b64_emit_quartet(s, s.tri_n);
  b64_flush_out(s);
}

// id gets the key, val_out its value
static bool is_exportable_pref(const corium::PrefGroup* g, const corium::Pref& p,
                               char (&id)[CORIUM_PREF_ID_MAX + 1], const char*& val_out) {
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) return false;
  if (p.flags & corium::P_LOCAL) return false;
  flashToRam(p.id, id, sizeof(id));
  const char* cur = corium::Prefs::getString(g->moduleId, id);
  if (p.defaultVal && strcmp(cur, p.defaultVal) == 0) return false;
  val_out = cur;
  return true;
}

static uint8_t group_export_count(const corium::PrefGroup* g) {
  uint8_t n = 0;
  for (size_t j = 0; j < g->count.get(); j++) {
    char id[CORIUM_PREF_ID_MAX + 1];
    const char* cur;
    if (is_exportable_pref(g, g->prefs.row(j), id, cur)) n++;
  }
  return n;
}

static void export_write_group(B64Sink& sink, const corium::PrefGroup* g, uint8_t n) {
  size_t mid_len = strlen(g->moduleId);
  if (mid_len > 255) return;
  b64_write_u8(sink, (uint8_t)mid_len);
  b64_write(sink, g->moduleId, mid_len);
  b64_write_u8(sink, n);

  for (size_t j = 0; j < g->count.get(); j++) {
    const corium::Pref p = g->prefs.row(j);
    char id[CORIUM_PREF_ID_MAX + 1];
    const char* cur;
    if (!is_exportable_pref(g, p, id, cur)) continue;
    size_t klen = strlen(id);
    size_t vlen = strlen(cur);
    if (klen > 255 || vlen > 65535) continue;
    b64_write_u8(sink, (uint8_t)klen);
    b64_write(sink, id, klen);
    uint8_t vb[2];
    wrBe16(vb, (uint16_t)vlen);
    b64_write(sink, vb, 2);
    b64_write(sink, cur, vlen);
  }
}

// first, an import reads it before the rest
static void export_board(B64Sink& sink) {
  char mid[8];
  flashToRam(corium::ImportText::BOARD_MID, mid, sizeof(mid));
  const char* b = corium::DeviceInfo::board();
  size_t n = strlen(b);
  b64_write_u8(sink, (uint8_t)strlen(mid));
  b64_write(sink, mid, strlen(mid));
  b64_write_u8(sink, 1);
  b64_write_u8(sink, 4);
  b64_write(sink, "name", 4);
  uint8_t vb[2];
  wrBe16(vb, (uint16_t)n);
  b64_write(sink, vb, 2);
  b64_write(sink, b, n);
}

static void export_stream(B64Sink& sink) {
  uint8_t groups = 1;   // the board
  for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
    const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
    if (group_export_count(g)) groups++;
  }

  const uint8_t MAGIC[BLOB_MAGIC] = {CORIUM_EXPORT_MAGIC};
  b64_write(sink, MAGIC, BLOB_MAGIC);
  b64_write_u8(sink, BLOB_VER);
  // magic and version stay clear, the rest is xored
  sink.obf_on = true;
  b64_write_u8(sink, groups);
  export_board(sink);

  for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
    const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
    uint8_t n = group_export_count(g);
    if (n == 0) continue;
    export_write_group(sink, g, n);
  }
}

static void deobfuscate_payload(uint8_t* buf, size_t len) {
  uint32_t st = EXPORT_OBF_SEED;
  for (size_t i = 0; i < len; i++) buf[i] ^= obf_step(&st);
}

static const char* check_import(uint8_t* buf, size_t len) {
  if (len < BLOB_HEAD + 1 + BLOB_CRC) return PSTR("too short");   // no groups
  // the magic is blobStart's 4 base64 chars
  if (buf[BLOB_MAGIC] != BLOB_VER) return PSTR("bad format");
  size_t payload_len = len - BLOB_CRC;
  // crc is over the xored bytes, check before undoing them
  uint32_t expected = rdBe32(buf + payload_len);
  uint32_t actual = crc32_update(0, buf, payload_len) ^ EXPORT_OBF_SEED;
  if (expected != actual) return PSTR("checksum mismatch");

  deobfuscate_payload(buf + BLOB_HEAD, payload_len - BLOB_HEAD);
  return corium::ImportText::blobError(buf + BLOB_HEAD, payload_len - BLOB_HEAD);
}

static bool import_local(const char* mid, const char* key) {
  corium::Pref p;
  return corium::Prefs::findPref(mid, key, &p) && (p.flags & corium::P_LOCAL);
}

// put refuses unknown keys, forwards moved ones
static bool import_put(const char* mid, const char* key, const char* value) {
  return !import_local(mid, key) && corium::Prefs::put(mid, key, value);
}

static bool s_other_board = false;   // the blob came from another board

static bool board_group(const char* mid) {
  for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
    const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
    if (strcmp(g->moduleId, mid) == 0) return g->category.get() & corium::P_CAT_BOARD;
  }
  return false;
}

// a backup's values were saved before, a newer shortest rule mustn't drop them;
// another board's calibration is refused, this one's stays
static bool import_kept(const char* mid, const char* key, const char* value) {
  if (s_other_board && board_group(mid)) return false;
  return !import_local(mid, key) && corium::Prefs::putKept(mid, key, value);
}

// the wipe reset them, flash still has this board's
static void keep_board_groups() {
  for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
    const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
    if (g->category.get() & corium::P_CAT_BOARD) corium::Prefs::revert(g->moduleId);
  }
}

// wipe then apply, -1 = bad blob or every row refused
static int apply_import(uint8_t* buf, size_t len, corium::ImportText::Result* out,
                        const char** err_out) {
  const char* err = check_import(buf, len);
  if (err) { if (err_out) *err_out = err; return -1; }

  const uint8_t* recs = buf + BLOB_HEAD;
  size_t recs_len = len - BLOB_HEAD - BLOB_CRC;
  char from[32];
  corium::ImportText::blobBoard(recs, recs_len, from, sizeof(from));
  s_other_board = *from && strcmp(from, corium::DeviceInfo::board()) != 0;
  if (s_other_board) corium::Log::console(PSTR("Import: From %s, this board's calibration kept"), from);
  corium::Prefs::resetAll(/*keep_network=*/true);
  *out = corium::ImportText::applyBlob(recs, recs_len, import_kept);
  if (s_other_board) keep_board_groups();
  if (out->skipped) corium::Log::console(PSTR("Import: %d refused, first %s"), out->skipped, out->firstKey);
  // another board's export, say: the old settings stand
  if (out->applied == 0 && out->skipped) {
    corium::Prefs::revert();
    if (err_out) *err_out = PSTR("nothing imported");
    return -1;
  }
  return out->applied;
}

// safe mode gets here frozen: one the import didn't replace stops it all
static const char* import_commit(char* why, size_t n) {
  const char* err = corium::Prefs::anyFrozen(why, n) ? why : nullptr;
  if (!err && !corium::Prefs::commit(false)) err = corium::WRITE_FAILED;   // silent, the cascade would WDT
  if (err) corium::Prefs::revert();
  return err;
}

// a row that didn't load isn't in the blob, an import would lose it
void corium::WebPortal::hExport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  char why[CORIUM_PREFS_FROZEN_MAX];
  if (corium::Prefs::anyFrozen(why, sizeof(why))) {
    res.send(409, FPSTR(HTTP_CT_PLAIN), why);
    return;
  }
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));
  B64Sink sink;
  b64_init(sink, res);
  export_stream(sink);
  // crc goes out big endian, after the payload
  uint32_t crc = sink.crc ^ EXPORT_OBF_SEED;
  uint8_t crc_be[BLOB_CRC];
  wrBe32(crc_be, crc);
  for (uint8_t b : crc_be) b64_put(sink, b);
  b64_finish(sink);
  res.endChunked();
}

static BodyBuf s_import_body;

// too big or no memory, hImport says no body
bool corium::WebPortal::hImportBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* chunk, size_t len, void*) {
  return body_event(s_import_body, req, ev, chunk, len, nullptr);
}

static void import_fail(EGHttpResponse& res, const char* msg) {
  corium::WebPortal::beginPage(res, F("Import"));
  corium::WebPortal::sendf(res, PSTR("<p>Import failed: %s.</p><p><a href=/param>Back</a></p>"), msg);
  corium::WebPortal::endPage(res);
  body_free(s_import_body);
}

// decoded in the body buffer
static char* import_field(size_t* len, const char** err) {
  if (!s_import_body.ok || !s_import_body.buf || s_import_body.len == 0) {
    *err = PSTR("no body");
    return nullptr;
  }
  char* field = eghttp_form_take(s_import_body.buf, s_import_body.len, PSTR("blob"), len);
  if (!field || *len == 0) {
    *err = PSTR("missing blob");
    return nullptr;
  }
  return field;
}

// malloc'd blob, or nullptr and err
static uint8_t* import_decode(const char* b64, size_t b64_len, unsigned int* raw_len, const char** err) {
  unsigned int raw_cap = decode_base64_length((const unsigned char*)b64, b64_len);
  if (raw_cap == 0 || raw_cap > BODY_MAX) {
    *err = PSTR("bad base64");
    return nullptr;
  }
  uint8_t* raw = (uint8_t*)malloc(raw_cap);
  if (!raw) {
    *err = PSTR("alloc failed");
    return nullptr;
  }
  *raw_len = decode_base64((const unsigned char*)b64, b64_len, raw);
  return raw;
}

// wipe then apply, -1 = bad
static int import_blob(const char* b64, size_t b64_len, corium::ImportText::Result* out,
                       const char** err) {
  unsigned int raw_len = 0;
  uint8_t* raw = import_decode(b64, b64_len, &raw_len, err);
  body_free(s_import_body);
  if (!raw) return -1;
  int applied = apply_import(raw, raw_len, out, err);
  free(raw);
  return applied;
}

// only the listed prefs change, each one validated; -1 = none
static int import_lines(const char* text, size_t len, corium::ImportText::Result* out, const char** err) {
  corium::ImportText::Result r = corium::ImportText::apply(text, len, import_put);
  body_free(s_import_body);
  *out = r;
  if (r.applied == 0) {
    *err = PSTR("nothing imported");
    return -1;
  }
  return r.applied;
}

void corium::WebPortal::hImport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  const char* err = nullptr;
  size_t len = 0;
  corium::ImportText::Result r = {};
  int applied = -1;
  char why[CORIUM_PREFS_FROZEN_MAX];
  // safe mode: the import may replace it
  if (!corium::BootGuard::safeMode() && corium::Prefs::anyFrozen(why, sizeof(why))) {
    import_fail(res, why);
    return;
  }
  char* field = import_field(&len, &err);
  const char* blob = field ? corium::ImportText::blobStart(field, len) : nullptr;
  if (blob) applied = import_blob(blob, len - (size_t)(blob - field), &r, &err);
  else if (field) applied = import_lines(field, len, &r, &err);
  if (applied >= 0) err = import_commit(why, sizeof(why));
  if (applied < 0 || err) {
    import_fail(res, err ? err : PSTR("bad blob"));
    return;
  }
  corium::WebPortal::beginPage(res, F("Imported"));
  if (r.firstKey[0]) {
    // blob, a key from the upload
    sendf(res, PSTR("<p>Imported %d settings, %d refused (first "), applied, r.skipped);
    char esc[6 * (sizeof(r.firstKey) - 1) + 1];
    egesc_html(esc, sizeof(esc), r.firstKey);
    res.sendChunk(esc);
    res.sendChunk(F("). Restarting.</p>"));
  } else if (r.skipped) {
    sendf(res, PSTR("<p>Imported %d settings, skipped %d (first at line %d). Restarting.</p>"),
          applied, r.skipped, r.firstSkip);
  } else {
    sendf(res, PSTR("<p>Imported %d settings. Restarting.</p>"), applied);
  }
  corium::WebPortal::endRestartPage(res);
}
