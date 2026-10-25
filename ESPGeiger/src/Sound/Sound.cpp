/*
  Sound.cpp - events to tunes, prefs override the built-ins.

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
#ifdef GEIGER_BUZZER

#include <string.h>
#include "Sound.h"
#include "App/Board.h"
#include "Counter/Counter.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Output/Buzzer.h"
#include "TunePlayer.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Lifecycle.h"
#ifdef ESPG_HV
#include "HV/HV.h"
#endif
#ifdef GEIGER_SDCARD
#include "SDCard/SDCard.h"
#endif
#ifdef GEIGER_PUSHBUTTON
#include "PushButton/PushButton.h"
#endif

Sound sound;
EG_REGISTER_MODULE(sound)

static constexpr uint16_t BURST_HZ = 2700;   // the coil's resonance
static constexpr uint8_t GAP_PCT = 12;       // rtttl, as the hw demo page

enum : uint8_t { P_LOW = 1, P_MID, P_URGENT };

EG_PSTR(K_REPEAT,    "repeat");
EG_PSTR(K_STARTUP,   "startup");
EG_PSTR(K_WIFI,      "wifi");
EG_PSTR(K_ALERT,     "alert");
EG_PSTR(K_ALERT_END, "alert_end");
EG_PSTR(K_HVFAULT,   "hvfault");
EG_PSTR(K_SD_ERROR,  "sd_error");
EG_PSTR(K_OTA,       "ota");
EG_PSTR(K_OTA_FAIL,  "ota_fail");
EG_PSTR(K_BUTTON,    "button");
EG_PSTR(K_CLICK,     "click");

// from the hw32 session's demo page
EG_PSTR(T_COPIER, "c,p79,c,p189,c,p119,c,p129,1500>3000:500@15,p90,c,p15,c,p15,c,p15,c,p15,"
                  "c,p15,c,p15,c,p15,c,p15,c,p181,2700:80,p80,2700:80");
EG_PSTR(T_DING,    "3136:40@25,3136:40@15,3136:50@8");
EG_PSTR(T_ALARM,   "2700:190,p20,2093:190,p20,2700:190,p20,2093:190,p20,2700:190,p20,2093:190");
EG_PSTR(T_CLEAR,   "2700:120,p20,2093:200");
EG_PSTR(T_PROBLEM, "1500:160,p90,1500:160");
EG_PSTR(T_ARP,     "2093:90,p20,2637:90,p20,3136:90,p20,4186:220");
EG_PSTR(T_BUTTON,  "t3");
EG_PSTR(T_CLICK,   "c");

struct SoundDef {
  const char* key;
  const char* tune;
  uint8_t prio;
};

static const SoundDef DEFS[Sound::COUNT] = {
  {K_STARTUP,   T_COPIER,  P_LOW},
  {K_WIFI,      T_DING,    P_LOW},
  {K_ALERT,     T_ALARM,   P_URGENT},
  {K_ALERT_END, T_CLEAR,   P_MID},
  {K_HVFAULT,   T_PROBLEM, P_URGENT},
  {K_SD_ERROR,  T_PROBLEM, P_MID},
  {K_OTA,       T_ARP,     P_MID},
  {K_OTA_FAIL,  T_PROBLEM, P_MID},
  {K_BUTTON,    T_BUTTON,  P_LOW},
  {K_CLICK,     T_CLICK,   P_LOW},
};

EG_PSTR(L_REPEAT, "Alert repeat (s)");
EG_PSTR(H_REPEAT, "Play the alert again while it lasts, 0 = once");

#define SOUND_ROW(k) {k, nullptr, nullptr, "", nullptr, 0, 0, TunePlayer::MAX_TEXT, EGP_STRING, EGP_HIDDEN}

// "" = built-in, off = silent, else steps or rtttl
static const EGPref SOUND_PREF_ITEMS[] PROGMEM = {
  {K_REPEAT, L_REPEAT, H_REPEAT, "60", nullptr, 0, 3600, 0, EGP_UINT, 0},
  SOUND_ROW(K_STARTUP),
  SOUND_ROW(K_WIFI),
  SOUND_ROW(K_ALERT),
  SOUND_ROW(K_ALERT_END),
  SOUND_ROW(K_HVFAULT),
  SOUND_ROW(K_SD_ERROR),
  SOUND_ROW(K_OTA),
  SOUND_ROW(K_OTA_FAIL),
  SOUND_ROW(K_BUTTON),
  SOUND_ROW(K_CLICK),
};

static const EGPrefGroup SOUND_PREF_GROUP PROGMEM = {
  "sound", "Sound", 1,
  SOUND_PREF_ITEMS,
  sizeof(SOUND_PREF_ITEMS) / sizeof(SOUND_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT,
};

const EGPrefGroup* Sound::prefs_group() { return &SOUND_PREF_GROUP; }

static volatile uint8_t s_ota = 0;   // 1 started, 2 failed

static void sound_lifecycle(Lifecycle::Event e) {
  if (e == Lifecycle::SHUTDOWN) return;
  s_ota = e == Lifecycle::OTA_START ? 1 : 2;
  EGModuleRegistry::post(&sound);
}

static const char* err_word(uint8_t e) {
  switch (e) {
    case EGTune::RANGE:    return PSTR("out of range");
    case EGTune::TOO_MANY: return PSTR("too many steps");
    case EGTune::TOO_LONG: return PSTR("too long");
    case EGTune::GROUP:    return PSTR("x( ) group");
    default:               return PSTR("not a step");
  }
}

static bool is_off(const char* v) { return !strcmp_P(v, PSTR("off")); }

void Sound::on_prefs_loaded() {
  _reload = true;
  EGModuleRegistry::post(this);
}

// a typo can't silence an alarm: the built-in plays
void Sound::load() {
  _reload = false;
  _repeat_ms = EGPrefs::getUInt("sound", K_REPEAT) * 1000UL;
  _bad = 0;
  for (uint8_t i = 0; i < COUNT; i++) {
    const char* v = EGPrefs::getString("sound", DEFS[i].key);
    if (!v || !*v || is_off(v)) continue;
    uint16_t at = 0;
    uint8_t e = EGTune::check(v, strlen(v), &at);
    if (!e) continue;
    _bad |= 1u << i;
    Log::console(PSTR("Sound: %s %s at %u, built-in plays"), DEFS[i].key, err_word(e), at);
  }
  uint16_t len = 0;
  const char* t = text(CLICK, len);
  _click_on = false;
  if (!t) return;
  EGTune::Reader r;
  r.begin(t, len);
  _click_on = r.next(_click);
}

// pref, else built-in. nullptr = off
const char* Sound::text(uint8_t id, uint16_t& len) {
  const char* v = EGPrefs::getString("sound", DEFS[id].key);
  if (v && *v) {
    if (is_off(v)) return nullptr;
    if (!(_bad & (1u << id))) {
      len = strlen(v);
      return v;
    }
  }
  len = strlen_P(DEFS[id].tune);
  return DEFS[id].tune;
}

void Sound::pre_wifi() {
  EGModuleRegistry::on(Wifi::TOPIC_CHANGED, this);
  EGModuleRegistry::on(Counter::TOPIC_ALARM, this);
#ifdef ESPG_HV
  EGModuleRegistry::on(HV::TOPIC_FAULT, this);
#endif
#ifdef GEIGER_SDCARD
  EGModuleRegistry::on(SDCard::TOPIC_ERROR, this);
#endif
#ifdef GEIGER_PUSHBUTTON
  EGModuleRegistry::on(PushButton::TOPIC_PRESS, this);
#endif
  Lifecycle::on(sound_lifecycle);
}

static void sound_click(uint32_t) { sound.click((uint32_t)micros()); }

void Sound::begin() {
  gcounter.on_click(sound_click);
  Buzzer::Limits lim = Board::buzzer();
  if (!Buzzer::begin(GEIGER_BUZZER, lim)) Log::console(PSTR("Buzzer: pin %d unusable"), GEIGER_BUZZER);
  TunePlayer::begin({lim.duty_pct, BURST_HZ, GAP_PCT});
  _duty = lim.duty_pct;
  _wifi = Wifi::connected;
  if (_reload) load();
  play(STARTUP);
  schedule(fast_millis(), TunePlayer::poll((uint32_t)micros()));
}

void Sound::play(uint8_t id) {
  uint8_t prio = DEFS[id].prio;
  if (prio < P_URGENT && gcounter.is_quiet_now()) return;
  uint16_t len = 0;
  const char* t = text(id, len);
  if (t) TunePlayer::play((uint32_t)micros(), t, len, prio);
}

void Sound::click(uint32_t now) {
  if (!_click_on || TunePlayer::busy() || gcounter.is_quiet_now()) return;
  const EGTune::Step& c = _click;
  if (c.op == EGTune::PULSE) Buzzer::pulse(now, c.ms * 1000);
  else if (c.op == EGTune::BURST) Buzzer::burst(now, c.hz ? c.hz : BURST_HZ, c.ms);
  else if (c.op == EGTune::TONE) Buzzer::tone(now, c.hz, (uint32_t)c.ms * c.hz / 1000, c.duty ? c.duty : _duty);
}

// up, not down
void Sound::wifi() {
  if (Wifi::connected && !_wifi) play(WIFI);
  _wifi = Wifi::connected;
}

void Sound::events(uint16_t ev, unsigned long now) {
  if (ev & (1u << Wifi::TOPIC_CHANGED)) wifi();
  if (ev & (1u << Counter::TOPIC_ALARM)) alarm(now);
#ifdef ESPG_HV
  if (ev & (1u << HV::TOPIC_FAULT)) play(HVFAULT);
#endif
#ifdef GEIGER_SDCARD
  if (ev & (1u << SDCard::TOPIC_ERROR)) play(SD_ERROR);
#endif
#ifdef GEIGER_PUSHBUTTON
  if (ev & (1u << PushButton::TOPIC_PRESS)) play(BUTTON);
#endif
}

// rising: alert, again every repeat. falling: the all clear
void Sound::alarm(unsigned long now) {
  bool a = gcounter.is_alert();
  if (a == _alert) return;
  _alert = a;
  if (a) {
    play(ALERT);
    _next_alert = now + _repeat_ms;
    return;
  }
  TunePlayer::stop();
  play(ALERT_END);
}

void Sound::loop(unsigned long now) {
  if (_reload) load();
  uint16_t ev = EGModuleRegistry::events(this);
  if (ev) events(ev, now);
  uint8_t ota = s_ota;
  if (ota) {
    s_ota = 0;
    play(ota == 1 ? OTA : OTA_FAIL);
  }
  if (_alert && _repeat_ms && reached(now, _next_alert)) {
    play(ALERT);
    _next_alert = now + _repeat_ms;
  }
  if (!_fault_said && Buzzer::faulted()) {
    _fault_said = true;
    Log::console(PSTR("Sound: buzzer pin held high - off until restart"));
  }
  schedule(now, TunePlayer::poll((uint32_t)micros()));
}

// the tune's next step or the alert repeat, whichever is first
void Sound::schedule(unsigned long now, uint32_t us) {
  bool any = us != 0;
  unsigned long next = now + (us + 999) / 1000;
  if (_alert && _repeat_ms && (!any || (long)(_next_alert - next) < 0)) {
    next = _next_alert;
    any = true;
  }
  if (any) EGModuleRegistry::due_at(this, next);
}

#endif
