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
#include "App/AppKeys.h"
#include "Corium/Util/Words.h"
#include "App/Board.h"
#include "App/Topics.h"
#include "Counter/Counter.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Output/Buzzer.h"
#include "Corium/System/PinSafety.h"
#include "TunePlayer.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Lifecycle.h"

Sound sound;
CORIUM_REGISTER_MODULE(sound)

static constexpr uint16_t BURST_HZ = 2700;   // the coil's resonance
static constexpr uint8_t GAP_PCT = 12;       // rtttl, quiet end of each note

// A higher priority cuts in, urgent ignores volume and quiet hours
enum : uint8_t { P_LOW = 1, P_MID, P_URGENT };

CORIUM_PSTR(K_CLICKS,    "clicks");
CORIUM_PSTR(K_CHIMES,    "chimes");
CORIUM_PSTR(K_ALARM,     "alarm");
CORIUM_PSTR(K_REPEAT,    "repeat");
CORIUM_PSTR(K_STARTUP,   "startup");
CORIUM_PSTR(K_SETUP,     "setup");
CORIUM_PSTR(K_WIFI,      "wifi");
CORIUM_PSTR(K_ALERT_END, "alert_end");
CORIUM_PSTR(K_HVFAULT,   "hvfault");
CORIUM_PSTR(K_SD_ERROR,  "sd_error");
CORIUM_PSTR(K_OTA,       "ota");
CORIUM_PSTR(K_OTA_FAIL,  "ota_fail");
CORIUM_PSTR(K_BUTTON,    "button");
CORIUM_PSTR(K_CLICK,     "click");

// built-in tunes, EGTune steps
CORIUM_PSTR(T_COPIER, "c,p79,c,p189,c,p119,c,p129,1500>3000:500@15,p90,c,p15,c,p15,c,p15,c,p15,"
                  "c,p15,c,p15,c,p15,c,p15,c,p181,2700:80,p80,2700:80");
CORIUM_PSTR(T_DING,    "3136:40@25,3136:40@15,3136:50@8");
CORIUM_PSTR(T_BEEPS,   "2700:60,p120,2700:60,p120,2700:60");
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
  {K_SETUP,     T_BEEPS,   P_MID},
  {K_WIFI,      T_DING,    P_LOW},
  {EG_K_ALERT,  T_ALARM,   P_URGENT},
  {K_ALERT_END, T_CLEAR,   P_MID},
  {K_HVFAULT,   T_PROBLEM, P_URGENT},
  {K_SD_ERROR,  T_PROBLEM, P_MID},
  {K_OTA,       T_ARP,     P_MID},
  {K_OTA_FAIL,  T_PROBLEM, P_MID},
  {K_BUTTON,    T_BUTTON,  P_LOW},
  {K_CLICK,     T_CLICK,   P_LOW},
};

CORIUM_PSTR(L_VOLUME, "Volume (%)");
CORIUM_PSTR(H_VOLUME, "Alarms and HV faults always play full. 0 = only those");
CORIUM_PSTR(L_CLICKS, "Click per count");
CORIUM_PSTR(L_CHIMES, "Chimes");
CORIUM_PSTR(H_CHIMES, "Startup, WiFi and button sounds");
CORIUM_PSTR(L_ALARM,  "Alarm sound");
CORIUM_PSTR(H_ALARM,  "Enable alarm sound");
CORIUM_PSTR(L_REPEAT, "Alert repeat (s)");
CORIUM_PSTR(H_REPEAT, "Play the alert again while it lasts, 0 = once");

// P_LOCAL, 15 whole tunes would outgrow an import
#define SOUND_ROW(k) {k, nullptr, nullptr, "", nullptr, 0, 0, TunePlayer::MAX_TEXT, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL}

// "" = built-in, off = silent, else steps or rtttl
static const corium::Pref SOUND_PREF_ITEMS[] PROGMEM = {
  {EG_K_VOLUME, L_VOLUME, H_VOLUME, "100", nullptr, 0, 100, 0, corium::P_UINT, corium::P_SLIDER},
  {K_CLICKS, L_CLICKS, nullptr,  "1",   nullptr, 0, 1,   0, corium::P_BOOL, 0},
  {K_CHIMES, L_CHIMES, H_CHIMES, "1",   nullptr, 0, 1,   0, corium::P_BOOL, 0},
  {K_ALARM,  L_ALARM,  H_ALARM,  "1",   nullptr, 0, 1,   0, corium::P_BOOL, 0},
  {K_REPEAT, L_REPEAT, H_REPEAT, "60", nullptr, 0, 3600, 0, corium::P_UINT, 0},
  SOUND_ROW(K_STARTUP),
  SOUND_ROW(K_SETUP),
  SOUND_ROW(K_WIFI),
  SOUND_ROW(EG_K_ALERT),
  SOUND_ROW(K_ALERT_END),
  SOUND_ROW(K_HVFAULT),
  SOUND_ROW(K_SD_ERROR),
  SOUND_ROW(K_OTA),
  SOUND_ROW(K_OTA_FAIL),
  SOUND_ROW(K_BUTTON),
  SOUND_ROW(K_CLICK),
};

static constexpr size_t SOUND_ROWS = sizeof(SOUND_PREF_ITEMS) / sizeof(SOUND_PREF_ITEMS[0]);
// room for a whole tune in every row
static constexpr uint16_t SOUND_BYTES = SOUND_ROWS * TunePlayer::MAX_TEXT;
static_assert(SOUND_BYTES <= CORIUM_PREFS_GROUP_SZ_MAX, "every tune fits the group");

static const corium::PrefGroup SOUND_PREF_GROUP PROGMEM = {
  "sound", "Sound", 1,
  SOUND_PREF_ITEMS,
  SOUND_ROWS,
  corium::P_CAT_OUTPUT, nullptr, SOUND_BYTES,
};

const corium::PrefGroup* Sound::prefsGroup() { return &SOUND_PREF_GROUP; }

static volatile uint8_t s_ota = 0;   // 1 started, 2 failed

static void sound_lifecycle(corium::Lifecycle::Event e) {
  if (e == corium::Lifecycle::SHUTDOWN) return;
  s_ota = e == corium::Lifecycle::OTA_START ? 1 : 2;
  corium::Registry::post(&sound);
}

static const char* err_word(uint8_t e) {
  switch (e) {
    case EGTune::RANGE:    return corium::PinSafety::OUT_OF_RANGE;
    case EGTune::TOO_MANY: return PSTR("too many steps");
    case EGTune::TOO_LONG: return W_TOO_LONG;
    case EGTune::GROUP:    return PSTR("x( ) group");
    default:               return PSTR("not a step");
  }
}

static bool is_off(const char* v) { return !strcmp_P(v, PSTR("off")); }

void Sound::onPrefsLoaded() {
  _reload = true;
  corium::Registry::post(this);
}

// A typo can't silence an alarm, the built-in plays instead
void Sound::load() {
  _reload = false;
  _repeat_ms = corium::Prefs::getUInt("sound", K_REPEAT) * 1000UL;
  _vol = corium::Prefs::getUInt("sound", EG_K_VOLUME);
  _chimes = corium::Prefs::getBool("sound", K_CHIMES);
  _alarm = corium::Prefs::getBool("sound", K_ALARM);
  if (!_alarm) _alert = false;   // stops any repeat
  _bad = 0;
  for (uint8_t i = 0; i < COUNT; i++) {
    const char* v = corium::Prefs::getString("sound", DEFS[i].key);
    if (!*v || is_off(v)) continue;
    uint16_t at = 0;
    uint8_t e = EGTune::check(v, strlen(v), &at);
    if (!e) continue;
    _bad |= 1u << i;
    corium::Log::console(PSTR("Sound: %s %s at %u, built-in plays"), DEFS[i].key, err_word(e), at);
  }
  uint16_t len = 0;
  const char* t = text(CLICK, len);
  _click_on = false;
  if (!t || !corium::Prefs::getBool("sound", K_CLICKS)) return;
  EGTune::Reader r;
  r.begin(t, len);
  _click_on = r.next(_click);
}

// The pref, else the built-in. nullptr = off
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

void Sound::preWifi() {
  corium::Registry::on(corium::Wifi::TOPIC_CHANGED, this);
  corium::Registry::on(Counter::TOPIC_ALARM, this);
  corium::Registry::on(Topic::HV_FAULT, this);
  corium::Registry::on(Topic::SD_ERROR, this);
  corium::Registry::on(Topic::PRESS, this);
  corium::Lifecycle::on(sound_lifecycle);
}

static void sound_click(uint32_t) { sound.click((uint32_t)micros()); }

// Idle low before the rmt takes the pin, safe mode too
void Sound::park() {
  pinMode(GEIGER_BUZZER, OUTPUT);
  digitalWrite(GEIGER_BUZZER, LOW);
}

// Only once, the setup portal can get here first
void Sound::start() {
  if (_started) return;
  _started = true;
  corium::Buzzer::Limits lim = Board::buzzer();
  if (!corium::Buzzer::begin(GEIGER_BUZZER, lim)) corium::Log::console(PSTR("Buzzer: Pin %d unusable"), GEIGER_BUZZER);
  TunePlayer::begin({lim.dutyPct, BURST_HZ, GAP_PCT});
  _duty = lim.dutyPct;
}

static const unsigned long SETUP_MAX_MS = 3000;   // a long custom one is cut

// The portal holds the loop, so the tune is played out here
void Sound::setup_portal() {
  start();
  if (_reload) load();
  play(SETUP);
  unsigned long t0 = corium::fastMillis();
  while (TunePlayer::poll((uint32_t)micros()) && corium::fastMillis() - t0 < SETUP_MAX_MS) delay(1);
  TunePlayer::stop();
}

void Sound::begin() {
  gcounter.on_click(sound_click);
  start();
  _wifi = corium::Wifi::connected;
  if (_reload) load();
  play(STARTUP);
  schedule(corium::fastMillis(), TunePlayer::poll((uint32_t)micros()));
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

void Sound::click(uint32_t now_us) {
  if (!_click_on || !_vol || TunePlayer::busy() || gcounter.is_quiet_now()) return;
  const EGTune::Step& c = _click;
  uint8_t duty = TunePlayer::at_volume(c.duty ? c.duty : _duty, _vol);
  if (c.op == EGTune::PULSE) corium::Buzzer::pulse(now_us, c.ms * 1000);
  else if (c.op == EGTune::BURST) corium::Buzzer::burst(now_us, c.hz ? c.hz : BURST_HZ, c.ms);
  else if (c.op == EGTune::TONE) corium::Buzzer::tone(now_us, c.hz, corium::Buzzer::toneCycles(c.ms, c.hz), duty);
}

// Plays over chimes, alarms at full volume as they would really play
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
      corium::Log::console(PSTR("Sound: %s at %u"), err_word(e), at);
      return;
    }
    len = strlen(v);
  }
  if (!t) return;
  TunePlayer::play((uint32_t)micros(), t, len, P_MID, vol);
  corium::Registry::post(this);
}

static void cmd_play() {
  char* v = corium::Console::rest();
  if (v && *v) sound.test(v);
  else corium::Log::console(PSTR("play <sound name or tune>"));
}
CORIUM_REGISTER_CMD(play, cmd_play)

// only on connect, not on a drop
void Sound::wifi() {
  if (corium::Wifi::connected && !_wifi) play(WIFI);
  _wifi = corium::Wifi::connected;
}

void Sound::events(uint16_t ev, unsigned long now) {
  if (ev & (1u << corium::Wifi::TOPIC_CHANGED)) wifi();
  if (ev & (1u << Counter::TOPIC_ALARM)) alarm(now);
  if (ev & (1u << Topic::HV_FAULT)) play(HVFAULT);
  if (ev & (1u << Topic::SD_ERROR)) play(SD_ERROR);
  if (ev & (1u << Topic::PRESS)) play(BUTTON);
}

// Rising plays the alert, again every repeat. Falling plays the all clear. A snooze goes quiet with no all clear
void Sound::alarm(unsigned long now) {
  bool a = _alarm && gcounter.is_alert();
  if (a && gcounter.is_snoozed()) {
    if (_alert) TunePlayer::stop();
    _alert = false;
    return;
  }
  if (a == _alert) return;
  _alert = a;
  if (a) {
    play(ALERT);
    _repeat_at = now + _repeat_ms;
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
  if (_alert && _repeat_ms && corium::reached(now, _repeat_at)) {
    play(ALERT);
    _repeat_at = now + _repeat_ms;
  }
  if (!_fault_said && corium::Buzzer::faulted()) {
    _fault_said = true;
    corium::Log::console(PSTR("Sound: Buzzer pin stuck high, off until restart"));
  }
  schedule(now, TunePlayer::poll((uint32_t)micros()));
}

// the tune's next step or the alert repeat, whichever is first
void Sound::schedule(unsigned long now, uint32_t us) {
  bool any = us != 0;
  unsigned long next = now + (us + 999) / 1000;
  if (_alert && _repeat_ms && (!any || (int32_t)(_repeat_at - next) < 0)) {
    next = _repeat_at;
    any = true;
  }
  if (any) corium::Registry::dueAt(this, next);
}

#endif
