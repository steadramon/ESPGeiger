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
#include "App/Topics.h"
#include "Counter/Counter.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Output/Buzzer.h"
#include "TunePlayer.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Lifecycle.h"

Sound sound;
CORIUM_REGISTER_MODULE(sound)

static constexpr uint16_t BURST_HZ = 2700;   // the coil's resonance
static constexpr uint8_t GAP_PCT = 12;       // rtttl, as the hw demo page

enum : uint8_t { P_LOW = 1, P_MID, P_URGENT };

CORIUM_PSTR(K_VOLUME,    "volume");
CORIUM_PSTR(K_CLICKS,    "clicks");
CORIUM_PSTR(K_CHIMES,    "chimes");
CORIUM_PSTR(K_REPEAT,    "repeat");
CORIUM_PSTR(K_STARTUP,   "startup");
CORIUM_PSTR(K_WIFI,      "wifi");
CORIUM_PSTR(K_ALERT,     "alert");
CORIUM_PSTR(K_ALERT_END, "alert_end");
CORIUM_PSTR(K_HVFAULT,   "hvfault");
CORIUM_PSTR(K_SD_ERROR,  "sd_error");
CORIUM_PSTR(K_OTA,       "ota");
CORIUM_PSTR(K_OTA_FAIL,  "ota_fail");
CORIUM_PSTR(K_BUTTON,    "button");
CORIUM_PSTR(K_CLICK,     "click");

// from the hw32 session's demo page
CORIUM_PSTR(T_COPIER, "c,p79,c,p189,c,p119,c,p129,1500>3000:500@15,p90,c,p15,c,p15,c,p15,c,p15,"
                  "c,p15,c,p15,c,p15,c,p15,c,p181,2700:80,p80,2700:80");
CORIUM_PSTR(T_DING,    "3136:40@25,3136:40@15,3136:50@8");
CORIUM_PSTR(T_ALARM,   "2700:190,p20,2093:190,p20,2700:190,p20,2093:190,p20,2700:190,p20,2093:190");
CORIUM_PSTR(T_CLEAR,   "2700:120,p20,2093:200");
CORIUM_PSTR(T_PROBLEM, "1500:160,p90,1500:160");
CORIUM_PSTR(T_ARP,     "2093:90,p20,2637:90,p20,3136:90,p20,4186:220");
CORIUM_PSTR(T_BUTTON,  "t3");
CORIUM_PSTR(T_CLICK,   "1500:2");

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

CORIUM_PSTR(L_VOLUME, "Volume (%)");
CORIUM_PSTR(H_VOLUME, "Tones only. Alarms and HV faults always play full, 0 = only those");
CORIUM_PSTR(L_CLICKS, "Click per count");
CORIUM_PSTR(L_CHIMES, "Chimes");
CORIUM_PSTR(H_CHIMES, "Startup, WiFi and button sounds");
CORIUM_PSTR(L_REPEAT, "Alert repeat (s)");
CORIUM_PSTR(H_REPEAT, "Play the alert again while it lasts, 0 = once");

#define SOUND_ROW(k) {k, nullptr, nullptr, "", nullptr, 0, 0, TunePlayer::MAX_TEXT, corium::P_STRING, corium::P_HIDDEN}

// "" = built-in, off = silent, else steps or rtttl
static const corium::Pref SOUND_PREF_ITEMS[] PROGMEM = {
  {K_VOLUME, L_VOLUME, H_VOLUME, "100", nullptr, 0, 100, 0, corium::P_UINT, corium::P_SLIDER},
  {K_CLICKS, L_CLICKS, nullptr,  "1",   nullptr, 0, 1,   0, corium::P_BOOL, 0},
  {K_CHIMES, L_CHIMES, H_CHIMES, "1",   nullptr, 0, 1,   0, corium::P_BOOL, 0},
  {K_REPEAT, L_REPEAT, H_REPEAT, "60", nullptr, 0, 3600, 0, corium::P_UINT, 0},
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

static const corium::PrefGroup SOUND_PREF_GROUP PROGMEM = {
  "sound", "Sound", 1,
  SOUND_PREF_ITEMS,
  sizeof(SOUND_PREF_ITEMS) / sizeof(SOUND_PREF_ITEMS[0]),
  corium::P_CAT_OUTPUT,
};

const corium::PrefGroup* Sound::prefs_group() { return &SOUND_PREF_GROUP; }

static volatile uint8_t s_ota = 0;   // 1 started, 2 failed

static void sound_lifecycle(Lifecycle::Event e) {
  if (e == Lifecycle::SHUTDOWN) return;
  s_ota = e == Lifecycle::OTA_START ? 1 : 2;
  corium::Registry::post(&sound);
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
  corium::Registry::post(this);
}

// a typo can't silence an alarm: the built-in plays
void Sound::load() {
  _reload = false;
  _repeat_ms = corium::Prefs::getUInt("sound", K_REPEAT) * 1000UL;
  _vol = corium::Prefs::getUInt("sound", K_VOLUME);
  _chimes = corium::Prefs::getBool("sound", K_CHIMES);
  _bad = 0;
  for (uint8_t i = 0; i < COUNT; i++) {
    const char* v = corium::Prefs::getString("sound", DEFS[i].key);
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
  if (!t || !corium::Prefs::getBool("sound", K_CLICKS)) return;
  EGTune::Reader r;
  r.begin(t, len);
  _click_on = r.next(_click);
}

// pref, else built-in. nullptr = off
const char* Sound::text(uint8_t id, uint16_t& len) {
  const char* v = corium::Prefs::getString("sound", DEFS[id].key);
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
  corium::Registry::on(Wifi::TOPIC_CHANGED, this);
  corium::Registry::on(Counter::TOPIC_ALARM, this);
  corium::Registry::on(Topic::HV_FAULT, this);
  corium::Registry::on(Topic::SD_ERROR, this);
  corium::Registry::on(Topic::PRESS, this);
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
  uint8_t vol = prio == P_URGENT ? 100 : _vol;
  if (prio < P_URGENT && (!vol || gcounter.is_quiet_now())) return;
  if (prio == P_LOW && !_chimes) return;
  uint16_t len = 0;
  const char* t = text(id, len);
  if (t) TunePlayer::play((uint32_t)micros(), t, len, prio, vol);
}

void Sound::click(uint32_t now) {
  if (!_click_on || !_vol || TunePlayer::busy() || gcounter.is_quiet_now()) return;
  const EGTune::Step& c = _click;
  uint8_t duty = TunePlayer::at_volume(c.duty ? c.duty : _duty, _vol);
  if (c.op == EGTune::PULSE) Buzzer::pulse(now, c.ms * 1000);
  else if (c.op == EGTune::BURST) Buzzer::burst(now, c.hz ? c.hz : BURST_HZ, c.ms);
  else if (c.op == EGTune::TONE) Buzzer::tone(now, c.hz, Buzzer::tone_cycles(c.ms, c.hz), duty);
}

// over chimes, alarms full as they'd play
void Sound::test(const char* v) {
  uint16_t len = 0;
  const char* t = v;
  uint8_t vol = _vol;
  uint8_t i = 0;
  while (i < COUNT && strcmp_P(v, DEFS[i].key)) i++;
  if (i < COUNT) {
    t = text(i, len);
    if (DEFS[i].prio == P_URGENT) vol = 100;
  } else {
    uint16_t at = 0;
    uint8_t e = EGTune::check(v, strlen(v), &at);
    if (e) {
      Log::console(PSTR("Sound: %s at %u"), err_word(e), at);
      return;
    }
    len = strlen(v);
  }
  if (!t) return;
  TunePlayer::play((uint32_t)micros(), t, len, P_MID, vol);
  corium::Registry::post(this);
}

static void cmd_play() {
  char* v = Console::rest();
  if (v && *v) sound.test(v);
  else Log::console(PSTR("play <sound name or tune>"));
}
CORIUM_REGISTER_CMD(play, cmd_play)

// up, not down
void Sound::wifi() {
  if (Wifi::connected && !_wifi) play(WIFI);
  _wifi = Wifi::connected;
}

void Sound::events(uint16_t ev, unsigned long now) {
  if (ev & (1u << Wifi::TOPIC_CHANGED)) wifi();
  if (ev & (1u << Counter::TOPIC_ALARM)) alarm(now);
  if (ev & (1u << Topic::HV_FAULT)) play(HVFAULT);
  if (ev & (1u << Topic::SD_ERROR)) play(SD_ERROR);
  if (ev & (1u << Topic::PRESS)) play(BUTTON);
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
  uint16_t ev = corium::Registry::events(this);
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
  if (any) corium::Registry::due_at(this, next);
}

#endif
