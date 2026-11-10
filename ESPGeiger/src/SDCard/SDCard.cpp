/*
  SDCard.cpp - functions to handle SDCard

  Copyright (C) 2023 @steadramon

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
#ifdef GEIGER_SDCARD
#include <Arduino.h>
#include <sys/time.h>
#include <new>
#include <EGHttpServer.h>
#include "SDCard.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Measure/Measure.h"
#include "Corium/Module/Registry.h"
#include "Corium/System/Clock.h"
#include "Corium/System/Uptime.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/WebPortal/WebPortal.h"
#include "App/FeatureBits.h"

CORIUM_PSTR(K_SYNC_MIN, "sync_min");

#define SD_NAME_LEN 12   // 8.3, YYYYMMDD.csv
#define SD_DIR_LEN  6    // YYYYMM
#define SD_SECTOR   512
#define SD_COUNT_US 5000   // FAT a pass, ~1 ms a sector, the loop waits on it
#define SD_COUNT_MS 5
#define SD_FULL_MB 8       // at or under: delete old files, else stop
#define SD_FAIL_MAX 3      // failed minutes in a row, then off till reinit
#define SD_REINIT_S 30     // a missing card holds the loop ~2 s
#define SD_MONTHS   120    // 10 years
#define SD_TABLE_S  600    // the /sd table goes when nobody's looked for this long

// rows from before the clock was set
static const char UNDATED[] = "/undated.csv";
static const uint32_t UNDATED_DAY = 1;   // _open_day while it's open

SdFat32* sd = nullptr;
static File32* s_dl = nullptr;   // /sd?f=, one at a time, freed when its reply ends

struct MonthEntry {
  uint32_t yyyymm : 20;
  uint32_t files  : 12;
  uint32_t bytes;
};

// /sd's months, off the card once; a new or deleted file drops it
static MonthEntry* s_months = nullptr;
static uint16_t s_month_n = 0;
static bool s_undated = false;
static uint32_t s_months_s = 0;   // uptime it was last shown

static void forget_months() {
  free(s_months);
  s_months = nullptr;
  s_month_n = 0;
}

// the day file's rows, as the card would say
static void month_grew(uint32_t yyyymm, uint32_t bytes) {
  for (uint16_t i = 0; i < s_month_n; i++) {
    if (s_months[i].yyyymm == yyyymm) {
      s_months[i].bytes += bytes;
      return;
    }
  }
}

SDCard sdcard;
CORIUM_REGISTER_FEATURE(sdcard, FEAT_SDCARD)

CORIUM_PSTR(SD_L_SI, "Sync Interval (min)");
CORIUM_PSTR(SD_H_SI, "Minutes between syncs. 1=safest, higher=less SD wear");

static const corium::Pref SDCARD_PREF_ITEMS[] PROGMEM = {
  {K_SYNC_MIN, SD_L_SI, SD_H_SI, "1", nullptr, 1, 5, 0, corium::P_UINT, 0},
};

static const corium::PrefGroup SDCARD_PREF_GROUP PROGMEM = {
  "sdcard", "SD Card", 1,
  SDCARD_PREF_ITEMS,
  sizeof(SDCARD_PREF_ITEMS) / sizeof(SDCARD_PREF_ITEMS[0]),
  corium::P_CAT_SYSTEM,
};

const corium::PrefGroup* SDCard::prefsGroup() { return &SDCARD_PREF_GROUP; }

void SDCard::onPrefsLoaded() {
  _sync_min = (uint8_t)clamp<uint32_t>(corium::Prefs::getUInt("sdcard", K_SYNC_MIN), 1, 5);
}

// FAT date high, time low; FAT seconds are 2 s units
static uint32_t fat_pack(int y80, int mon, int day, int h, int m, int s) {
  uint16_t date = (uint16_t)((y80 << 9) | (mon << 5) | day);
  uint16_t tod  = (uint16_t)((h << 11) | (m << 5) | (s >> 1));
  return ((uint32_t)date << 16) | tod;
}

// FAT runs 1980-2107, outside it the nearest end; no clock yet is 1970
static uint32_t fat_stamp(const struct tm& t) {
  if (t.tm_year < 80)  return fat_pack(0, 1, 1, 0, 0, 0);
  if (t.tm_year > 207) return fat_pack(127, 12, 31, 23, 59, 58);
  return fat_pack(t.tm_year - 80, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

static void fat_now(uint16_t *date, uint16_t *tod) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  uint32_t s = fat_stamp(t);
  *date = (uint16_t)(s >> 16);
  *tod = (uint16_t)s;
}

SDCard::SDCard() {
}

static void sd_lifecycle(corium::Lifecycle::Event e) {
  if (e != corium::Lifecycle::OTA_FAILED) sdcard.pauseWriter();
}

void SDCard::begin()
{
  corium::Log::console(PSTR("SDCard: Init ..."));
  sd = new (std::nothrow) SdFat32();
  if (!sd) {
    corium::Log::console(PSTR("SDCard: Init failed, no memory"));
    return;
  }
#ifdef GEIGER_SDCARD_SCK
  // pins first, the c3 defaults are hw32's HV and pulse pins; sd's begin() then keeps them
  SPI.begin(GEIGER_SDCARD_SCK, GEIGER_SDCARD_MISO, GEIGER_SDCARD_MOSI, GEIGER_SDCARD_CS);
#endif
  corium::DeviceInfo::wdtPause();
  bool ok = sd->begin(GEIGER_SDCARD_CS);
  corium::DeviceInfo::wdtResume();
  if (!ok) {
    corium::Log::console(PSTR("SDCard: Init failed ... Please check wiring or insert a card."));
    if (sd->sdErrorCode() != SD_CARD_ERROR_CMD0) fault();   // CMD0 = no card
    delete sd;
    sd = nullptr;
    return;
  }
  FsDateTime::setCallback(fat_now);

  if (!_file.open("TEST.TMP", O_WRITE | O_CREAT | O_TRUNC)) {
    corium::Log::console(PSTR("SDCard: write probe failed - read-only or full?"));
    fault();
    delete sd;
    sd = nullptr;
    return;
  }
  _file.close();
  sd->remove("TEST.TMP");

  _enabled = true;
  _fault = false;
  _fails = 0;
  _fail_min = 0;
  corium::Log::console(PSTR("SDCard: Mounted."));
  corium::Log::console(PSTR("SDCard: Calculating free space"));
  startCount(CLEAN_FREECHECK);
  corium::Registry::dueIn(this, 0);
  // remount runs begin() again
  static bool listening = false;
  if (!listening) listening = corium::Lifecycle::on(sd_lifecycle);
}

// 512 B sectors >> 11 = MB
int32_t SDCard::freeMB() const {
  if (!sd || _counting || _free < 0) return -1;
  return (int32_t)(((uint64_t)_free * sd->sectorsPerCluster()) >> 11);
}

static uint32_t clusters_of(uint32_t bytes) {
  uint32_t per = (uint32_t)sd->sectorsPerCluster() * SD_SECTOR;
  return (bytes + per - 1) / per;
}

static uint32_t year_ago() {
  time_t cutoff_t = time(nullptr) - (365L * 86400L);
  struct tm tmv;
  localtime_r(&cutoff_t, &tmv);
  return fat_stamp(tmv);
}

// n digits, the first non-digit (or the end) stops it
static bool digits(const char* s, int n) {
  if (!s) return false;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

// strict, no ../
static bool valid_csv_name(const char* name) {
  return digits(name, 8) && name[8] == '.' && name[9] == 'c' && name[10] == 's' && name[11] == 'v' && name[12] == '\0';
}

static bool valid_yyyymm(const char* s) { return digits(s, SD_DIR_LEN) && s[SD_DIR_LEN] == '\0'; }
static bool valid_year(const char* s)   { return digits(s, 4) && s[4] == '\0'; }

// a day file of this month
static bool our_csv(const char* name, const char* dir) {
  return valid_csv_name(name) && strncmp(name, dir, SD_DIR_LEN) == 0;
}

// just past the next :00
static uint32_t ms_to_minute() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (59 - tv.tv_sec % 60) * 1000UL + (1000 - tv.tv_usec / 1000) + SD_EDGE_MS;
}

// first minute seen is only noted, a row needs a whole one
void SDCard::minuteEdge()
{
  time_t minute = time(NULL) / 60;
  if (minute == _last_minute) return;
  if (_last_minute) writeMinute();
  _last_minute = minute;
}

static void write_header(File32& f, PGM_P first) {
  f.print(FPSTR(first));
  f.print(F(",CPM,uSv/h,CPM5,CPM15"));
#ifdef SDCARD_EXTENDEDLOG
  f.print(F(",FreeMem"));
#endif
  f.println();
}

static void write_col(File32& f, const char* id) {
  char fb[16];
  corium::m_text(id, corium::M_OUT_JSON, fb, sizeof(fb));
  f.print(F(","));
  f.print(fb);
}

static void write_row(File32& f, const char* dateTime) {
  f.print(dateTime);
  write_col(f, PSTR("cpm"));
  write_col(f, PSTR("usv"));
  write_col(f, PSTR("cpm5"));
  write_col(f, PSTR("cpm15"));
#ifdef SDCARD_EXTENDEDLOG
  f.print(F(","));
  f.print(corium::DeviceInfo::freeHeap());
#endif
  f.println();
}

// false = no root, failed = no file
bool SDCard::openDayFile(const char* dir, const char* name, uint32_t day, bool& failed)
{
  closeDay(false);

  if (!sd->chdir()) {
    fail_step(PSTR("chdir /"), dir, name);
    return false;
  }
  // SdFat never yields, a dying card waits up to 600 ms a step: the 8266 wdt
  const char* step = nullptr;
  yield();
  if (!sd->exists(dir)) {
    yield();
    if (sd->mkdir(dir)) used_clusters(1);
    else step = PSTR("mkdir");
  }
  yield();
  if (!step && !sd->chdir(dir)) step = PSTR("chdir");
  yield();
  if (!step && !_file.open(name, FILE_WRITE)) step = PSTR("open");
  if (step) {
    fail_step(step, dir, name);
    failed = true;
  } else {
    _file_size = _file.fileSize();
    if (_file_size == 0) {
      write_header(_file, PSTR("DateTime"));
      forget_months();
    }
    _open_day = day;
  }
  return true;
}

// clusters used, a sync every _sync_min rows; false = the file's gone
bool SDCard::wrote(const char* dir, const char* name) {
  uint32_t size = _file.fileSize();
  uint32_t cl = clusters_of(size);
  uint32_t was = clusters_of(_file_size);
  if (cl > was) used_clusters(cl - was);
  if (_open_day != UNDATED_DAY) month_grew(_open_day / 100, size - _file_size);
  _file_size = size;
  // power loss loses unsynced rows
  if (++_unsynced_writes < _sync_min) return true;
  _unsynced_writes = 0;
  if (_file.sync()) {
    _fails = 0;
    return true;
  }
  fail_step(PSTR("sync"), dir, name);
  closeDay(false);
  return false;
}

// no clock yet: seconds since boot, dated later by the synced line
void SDCard::writeUndated()
{
  if (_open_day != UNDATED_DAY || !_file.isOpen()) {
    closeDay(false);
    if (!sd->chdir() || !_file.open(UNDATED, FILE_WRITE)) {
      fail_step(PSTR("open"), "", UNDATED + 1);
      return;
    }
    _file_size = _file.fileSize();
    _open_day = UNDATED_DAY;
    s_undated = true;
  }
  if (!_undated) write_header(_file, PSTR("Uptime"));   // once a boot
  _undated = true;
  char up[12];
  snprintf_P(up, sizeof(up), PSTR("%lu"), (unsigned long)corium::Uptime::seconds());
  write_row(_file, up);
  if (!wrote("", UNDATED + 1)) scheduleCleanup(CLEAN_FULL);
}

void SDCard::writeMinute()
{
  struct tm t;
  if (!corium::Clock::utcTm(&t)) {
    writeUndated();
    return;
  }
  int year = 1900 + t.tm_year;
  int mon = t.tm_mon + 1;

  bool clean = false;
  char name[SD_NAME_LEN + 1];
  char dir[SD_DIR_LEN + 1];
  char dateTime[20];

  snprintf_P(name, sizeof(name), PSTR("%04d%02d%02d.csv"), year, mon, t.tm_mday);
  snprintf_P(dir, sizeof(dir), PSTR("%04d%02d"), year, mon);
#ifdef SDCARD_LOG_UNIXTIME
  fmtU64(dateTime, sizeof(dateTime), (uint64_t)time(nullptr));
#else
  snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
    year, mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
#endif

  uint32_t day = year * 10000u + mon * 100u + t.tm_mday;

  if (_open_day == UNDATED_DAY && _file.isOpen()) {
    // dates the rows above it
    _file.print(F("synced,"));
    _file.print(dateTime);
    _file.print(',');
    _file.println((unsigned long)corium::Uptime::seconds());
  }

  if (day != _open_day || !_file.isOpen()) {
    if (!openDayFile(dir, name, day, clean)) return;
  }

  if (_file.isOpen()) {
    write_row(_file, dateTime);
    if (!wrote(dir, name)) clean = true;
  }

  // looks full, count it properly before deleting
  int32_t mb = freeMB();
  if (clean || (mb >= 0 && mb <= SD_FULL_MB)) scheduleCleanup(CLEAN_FULL);
}

// the device's own writes, so the FAT is only counted at mount and when full
void SDCard::used_clusters(uint32_t n) {
  if (_free < 0) return;
  _free =(uint32_t)_free > n ? _free - (int32_t)n : 0;
}

// one byte, ticker safe
void SDCard::scheduleCleanup(uint8_t what) {
  if (what > _cleanup) _cleanup = what;
}

// once a minute, not re-armed once off; a cleanup counts free space first
void SDCard::loop(unsigned long /*now*/) {
  if (s_months && (!_enabled || (uint32_t)corium::Uptime::seconds() - s_months_s >= SD_TABLE_S)) forget_months();
  if (!_enabled) return;
  if (!_counting) {
    corium::Registry::dueIn(this, ms_to_minute());
    minuteEdge();
    if (_cleanup == CLEAN_NONE) return;
    startCount(_cleanup);
    _cleanup = CLEAN_NONE;
  }
  countFat();
}

void SDCard::startCount(uint8_t what) {
  _counting = what;
  _free = 0;
  _fat_sec = 0;
}

static uint16_t free_entries(const uint8_t* buf, uint16_t n, uint8_t width) {
  uint16_t count = 0;
  for (uint16_t i = 0; i < n * width; i += width) {
    uint8_t used = 0;
    for (uint8_t k = 0; k < width; k++) used |= buf[i + k];
    if (!used) count++;
  }
  return count;
}

// raw sectors, SdFat's own count reads the whole FAT in one go
void SDCard::countFat() {
  uint8_t buf[SD_SECTOR];
  const uint8_t width = sd->fatType() == 16 ? 2 : 4;
  const uint32_t per = sizeof(buf) / width;
  const uint32_t total = sd->clusterCount() + 2;
  const uint32_t t0 = micros();
  while (_fat_sec * per < total && (uint32_t)(micros() - t0) < SD_COUNT_US) {
    if (!sd->card()->readSector(sd->fatStartSector() + _fat_sec, buf)) {
      // next pass counts again
      scheduleCleanup(_counting);
      _counting = CLEAN_NONE;
      _free = -1;
      corium::Registry::dueIn(this, ms_to_minute());
      return;
    }
    uint32_t left = total - _fat_sec * per;
    _free += free_entries(buf, left < per ? left : per, width);
    _fat_sec++;
  }
  if (_fat_sec * per < total) {
    corium::Registry::dueIn(this, SD_COUNT_MS);
    return;
  }
  const uint8_t what = _counting;
  _counting = CLEAN_NONE;
  corium::Registry::dueIn(this, ms_to_minute());
  cleanup(what);
}

void SDCard::cleanup(uint8_t what) {
  Hold hold;
  if (what == CLEAN_FREECHECK) {
    uint32_t mb = freeMB();
    corium::Log::console(PSTR("SDCard: %lu MB free."), (unsigned long)mb);
    if (mb <= SD_FULL_MB && !deleteOldest(year_ago())) disableFull();
  } else {
    uint8_t left = 10;
    uint32_t mb = freeMB();
    uint32_t cutoff = year_ago();
    while (mb <= SD_FULL_MB && left-- > 0) {
      if (!deleteOldest(cutoff)) break;
      mb = freeMB();
    }
    if (mb <= SD_FULL_MB) disableFull();
  }
}

void SDCard::disableFull() {
  corium::Log::console(PSTR("SDCard: disabled - card full"));
  _enabled = false;
  fault();
}

// once till it mounts: the step and SdFat's code
void SDCard::fail_step(const char* step, const char* dir, const char* file) {
  if (!_fault) {
    corium::Log::console(PSTR("SDCard: %s/%s: %s failed (err 0x%02x)"), dir, file, step, sd->sdErrorCode());
  }
  fault();
}

// a dying card holds the loop seconds a try, so a minute counts once
void SDCard::fault() {
  if (!_fault) corium::Registry::emit(TOPIC_ERROR);
  _fault = true;
  uint32_t m = (uint32_t)(time(nullptr) / 60);
  if (!_enabled || m == _fail_min) return;
  _fail_min = m;
  if (++_fails < SD_FAIL_MAX) return;
  corium::Log::console(PSTR("SDCard: disabled after %u failed minutes"), (unsigned)SD_FAIL_MAX);
  _enabled = false;
}

// leaves cwd at root
static bool oldest_month_after(const char* after, char* out, size_t cap) {
  File32 root;
  File32 sub;
  out[0] = '\0';

  if (!sd->chdir()) return false;
  if (!root.open("/")) return false;

  while (sub.openNext(&root, O_RDONLY)) {
    if (sub.isSubDir() && !sub.isHidden()) {
      char name[SD_NAME_LEN + 1];
      sub.getName(name, sizeof(name));
      if (valid_yyyymm(name) && strcmp(name, after) > 0 &&
          (out[0] == '\0' || strcmp(name, out) < 0)) {
        strlcpy(out, name, cap);
      }
    }
    sub.close();
    yield();
  }
  root.close();
  return out[0] != '\0';
}

static bool oldest_csv_in(const char* dir, uint32_t cutoff,
                          char* out, size_t cap, bool& any_csv) {
  File32 sub;
  File32 file;
  uint32_t oldest = 0xFFFFFFFFul;
  out[0] = '\0';
  any_csv = false;

  if (!sub.open(dir, O_RDONLY)) return false;
  while (file.openNext(&sub, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[SD_NAME_LEN + 1];
      file.getName(f_name, sizeof(f_name));
      if (our_csv(f_name, dir)) {
        any_csv = true;
        uint16_t date, tod;
        file.getModifyDateTime(&date, &tod);
        uint32_t stamp = (uint32_t(date) << 16) | tod;

        if (stamp < cutoff && stamp < oldest) {
          oldest = stamp;
          strlcpy(out, f_name, cap);
        }
      }
    }
    file.close();
    yield();
  }
  sub.close();
  return true;
}

bool SDCard::deleteOldest(uint32_t cutoff) {
  // skip stuck months, max 12
  char after[SD_DIR_LEN + 1] = {};
  for (uint8_t skips = 0; skips <= 12; skips++) {
    char dir[SD_DIR_LEN + 1];
    char name[SD_NAME_LEN + 1];
    bool any_csv = false;

    if (!oldest_month_after(after, dir, sizeof(dir))) return false;
    if (!oldest_csv_in(dir, cutoff, name, sizeof(name), any_csv)) return false;

    if (!any_csv) {
      if (sd->rmdir(dir)) return true;
      memcpy(after, dir, sizeof(after));
      continue;
    }
    if (name[0] == '\0') return false;

    bool deleted = false;
    File32 f;
    if (sd->chdir(dir) && f.open(name, O_RDONLY)) {
      uint32_t size = f.fileSize();
      f.close();
      corium::Log::console(PSTR("SDCard: Deleting old file %s/%s"), dir, name);
      deleted = sd->remove(name);
      if (deleted) {
        _free += clusters_of(size);
        forget_months();
      }
    }
    sd->chdir();
    return deleted;
  }
  return false;
}

void SDCard::reinit() {
  // first, the loop checks it
  _enabled = false;
  pauseWriter();
  // the reply reads nothing more, it frees it
  if (s_dl) (void)s_dl->close();
  forget_months();
  if (sd) {
    delete sd;
    sd = nullptr;
  }
  begin();
}

SDCard::Hold::Hold()  { sdcard.pauseWriter(); }

void SDCard::pauseWriter() {
  closeDay(true);
}

void SDCard::closeDay(bool sync) {
  if (_file.isOpen()) {
    // close flushes the rows since the last sync
    bool ok = !sync || _file.sync();
    if (!_file.close()) ok = false;
    if (!ok) fail_step(PSTR("close"), "", "day file");
  }
  _open_day = 0;
  _unsynced_writes = 0;
}

struct DayEntry {
  uint8_t  day;
  uint32_t bytes;
};

static void end_page(EGHttpResponse& res) {
  corium::WebPortal::endPage(res);
}

static void end_page(EGHttpResponse& res, const __FlashStringHelper* msg) {
  res.sendChunk(msg);
  end_page(res);
}

static void tally_month(File32& sub, const char* dir, MonthEntry& m) {
  File32 file;
  while (file.openNext(&sub, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[SD_NAME_LEN + 1];
      file.getName(f_name, sizeof(f_name));
      if (our_csv(f_name, dir)) {
        m.files++;
        m.bytes += file.fileSize();
      }
    }
    file.close();
    yield();
  }
}

static uint16_t scan_months(MonthEntry* months, uint16_t cap) {
  uint16_t count = 0;
  File32 root, sub;
  if (!sd->chdir() || !root.open("/")) return 0;
  while (count < cap && sub.openNext(&root, O_RDONLY)) {
    if (sub.isSubDir() && !sub.isHidden()) {
      char name[SD_NAME_LEN + 1];
      sub.getName(name, sizeof(name));
      if (valid_yyyymm(name)) {
        MonthEntry& m = months[count];
        int32_t ym = 0;
        parseI32(name, &ym);
        m.yyyymm = (uint32_t)ym;
        m.files  = 0;
        m.bytes  = 0;
        tally_month(sub, name, m);
        if (m.files > 0) count++;
      }
    }
    sub.close();
    yield();
  }
  root.close();
  return count;
}

static void sort_months_desc(MonthEntry* months, uint16_t count) {
  for (uint16_t i = 1; i < count; i++) {
    MonthEntry tmp = months[i];
    uint16_t j = i;
    while (j > 0 && months[j - 1].yyyymm < tmp.yyyymm) {
      months[j] = months[j - 1];
      j--;
    }
    months[j] = tmp;
  }
}

static unsigned long kb(uint64_t bytes) { return (unsigned long)((bytes + 1023) / 1024); }
static PGM_P plural(unsigned n) { return n == 1 ? PSTR("") : PSTR("s"); }

static void send_list_summary(EGHttpResponse& res, uint16_t count, uint64_t total) {
  int32_t mb = sdcard.freeMB();
  char fs[24];
  if (mb >= 0) snprintf_P(fs, sizeof(fs), PSTR("%lu MB free"), (unsigned long)mb);
  else if (sdcard.counting()) snprintf_P(fs, sizeof(fs), PSTR("counting free space"));
  else snprintf_P(fs, sizeof(fs), PSTR("free space unknown"));

  char head[160];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p>%u month%s, %lu KB total, %s.</p>"),
    (unsigned)count, plural(count), kb(total), fs);
  if (n > 0) res.sendChunk(head, (size_t)n);
}

// months newest first, count > 0
static uint16_t send_year_picker(EGHttpResponse& res, const MonthEntry* months,
                                 uint16_t count, uint16_t sel) {
  constexpr uint8_t MAX_YEARS = 30;
  uint16_t years[MAX_YEARS];
  uint8_t n = 0;
  for (uint16_t i = 0; i < count && n < MAX_YEARS; i++) {
    uint16_t y = (uint16_t)(months[i].yyyymm / 100);
    if (n == 0 || years[n - 1] != y) {
      years[n++] = y;
    }
  }

  uint16_t year = (sel > 0) ? sel : years[0];

  if (n > 1) {
    res.sendChunk(F("<p>Year: <select onchange=\"location.href='/sd?y='+this.value\">"));
    for (uint8_t i = 0; i < n; i++) {
      char opt[64];
      int len = snprintf_P(opt, sizeof(opt),
        PSTR("<option value='%04u'%s>%04u</option>"),
        (unsigned)years[i],
        years[i] == year ? PSTR(" selected") : PSTR(""),
        (unsigned)years[i]);
      if (len > 0) res.sendChunk(opt, (size_t)len);
    }
    res.sendChunk(F("</select></p>"));
  }
  return year;
}

static void send_month_rows(EGHttpResponse& res, const MonthEntry* months,
                            uint16_t count, uint16_t year) {
  bool any = false;
  res.sendChunk(F("<div class=menu>"));
  for (uint16_t i = 0; i < count; i++) {
    uint32_t ym = months[i].yyyymm;
    if ((uint16_t)(ym / 100) != year) continue;
    any = true;
    char row[160];
    int rn = snprintf_P(row, sizeof(row),
      PSTR("<a href='/sd?m=%06lu'>%04u-%02u <small>(%u file%s, %lu KB)</small></a>"),
      (unsigned long)ym,
      (unsigned)(ym / 100), (unsigned)(ym % 100),
      (unsigned)months[i].files, plural(months[i].files), kb(months[i].bytes));
    if (rn > 0) res.sendChunk(row, (size_t)rn);
  }
  res.sendChunk(F("</div>"));
  if (!any) res.sendChunk(F("<p>No log files in this year.</p>"));
}

// the whole card once, then only the months found are kept
static bool load_months() {
  MonthEntry* m = (MonthEntry*)malloc(SD_MONTHS * sizeof(MonthEntry));
  if (!m) return false;
  SDCard::Hold hold;
  uint16_t n = scan_months(m, SD_MONTHS);
  sort_months_desc(m, n);
  s_undated = sd->exists(UNDATED);
  // never 0, the pointer says it's loaded
  MonthEntry* fit = (MonthEntry*)realloc(m, (n ? n : 1) * sizeof(MonthEntry));
  s_months = fit ? fit : m;
  s_month_n = n;
  return true;
}

static void hSdList(EGHttpResponse& res, uint16_t sel = 0) {
  corium::WebPortal::beginPage(res, F("SD Logs"));

  if (!sdcard.ready() || !sd) {
    end_page(res, F("<p>SD card not available.</p>"));
    return;
  }

  if (!s_months && !load_months()) {
    end_page(res, F("<p>Out of memory.</p>"));
    return;
  }
  s_months_s = (uint32_t)corium::Uptime::seconds();

  uint64_t total = 0;
  for (uint16_t i = 0; i < s_month_n; i++) total += s_months[i].bytes;
  send_list_summary(res, s_month_n, total);
  if (s_undated) {
    res.sendChunk(F("<p><a href='/sd?u=1'>Undated rows</a>, from before the clock was set.</p>"));
  }

  if (s_month_n == 0) {
    res.sendChunk(F("<p>No log files yet.</p>"));
    end_page(res);
    return;
  }

  uint16_t year = send_year_picker(res, s_months, s_month_n, sel);
  send_month_rows(res, s_months, s_month_n, year);
  end_page(res);
}

static bool scan_days(const char* dir, DayEntry* days, uint8_t cap,
                      uint8_t& count, uint32_t& total) {
  File32 sub, file;
  bool opened = sd->chdir() && sub.open(dir, O_RDONLY);
  if (!opened) return false;
  while (count < cap && file.openNext(&sub, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[SD_NAME_LEN + 1];
      file.getName(f_name, sizeof(f_name));
      if (our_csv(f_name, dir)) {
        days[count].day   = (uint8_t)((f_name[6] - '0') * 10 + (f_name[7] - '0'));
        days[count].bytes = file.fileSize();
        total += days[count].bytes;
        count++;
      }
    }
    file.close();
    yield();
  }
  sub.close();
  return true;
}

static void sort_days_desc(DayEntry* days, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    DayEntry tmp = days[i];
    uint8_t j = i;
    while (j > 0 && days[j - 1].day < tmp.day) {
      days[j] = days[j - 1];
      j--;
    }
    days[j] = tmp;
  }
}

static void send_day_rows(EGHttpResponse& res, const char* dir,
                          const DayEntry* days, uint8_t count) {
  res.sendChunk(F("<div class=\"menu dense\">"));
  for (uint8_t i = 0; i < count; i++) {
    char row[160];
    int rn = snprintf_P(row, sizeof(row),
      PSTR("<a href='/sd?f=%s%02u.csv'>%.4s-%.2s-%02u <small>(%lu KB)</small></a>"),
      dir, (unsigned)days[i].day,
      dir, dir + 4, (unsigned)days[i].day, kb(days[i].bytes));
    if (rn > 0) res.sendChunk(row, (size_t)rn);
  }
  res.sendChunk(F("</div>"));
}

static void hSdMonth(EGHttpResponse& res, const char* yyyymm) {
  char dir[SD_DIR_LEN + 1];
  for (int i = 0; i < SD_DIR_LEN; i++) dir[i] = yyyymm[i];
  dir[SD_DIR_LEN] = '\0';

  corium::WebPortal::beginPage(res, F("SD Logs"));

  if (!sdcard.ready() || !sd) {
    end_page(res, F("<p>SD card not available.</p>"));
    return;
  }

  SDCard::Hold hold;
  res.sendChunk(F("<p><a href='/sd'>&larr; All months</a></p>"));

  constexpr uint8_t MAX_DAYS = 31;
  DayEntry days[MAX_DAYS];
  uint8_t count = 0;
  uint32_t total = 0;
  if (!scan_days(dir, days, MAX_DAYS, count, total)) {
    end_page(res, F("<p>Month not found.</p>"));
    return;
  }
  sort_days_desc(days, count);

  char head[80];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p>%u file%s, %lu KB.</p>"),
    (unsigned)count, plural(count), kb(total));
  if (n > 0) res.sendChunk(head, (size_t)n);

  if (count == 0) {
    res.sendChunk(F("<p>No log files in this month.</p>"));
  } else {
    send_day_rows(res, dir, days, count);
  }

  end_page(res);
}

// the server asks as acks come, a short write asks again at the same place
static size_t dl_fill(void* f, size_t at, char* buf, size_t room) {
  File32* fp = (File32*)f;
  if (fp->curPosition() != at && !fp->seekSet(at)) return 0;
  int got = fp->read(buf, room);
  return got > 0 ? (size_t)got : 0;
}

static void dl_done(void* f) {
  File32* fp = (File32*)f;
  (void)fp->close();   // read only
  delete fp;
  s_dl = nullptr;
}

// f checked, nullptr = the undated rows
static void hSdDownload(EGHttpResponse& res, const char* f) {
  // arg() buffer is shared
  char name[SD_NAME_LEN + 1];
  strlcpy(name, f ? f : UNDATED + 1, sizeof(name));

  if (!sdcard.ready() || !sd) {
    res.send(503, FPSTR(HTTP_CT_PLAIN), F("SD not ready"));
    return;
  }
  File32* fp = s_dl ? nullptr : new (std::nothrow) File32;
  if (!fp) {
    res.send(503, FPSTR(HTTP_CT_PLAIN), F("Busy"));
    return;
  }

  // full path, the writer runs between sends and moves the cwd
  char path[SD_DIR_LEN + SD_NAME_LEN + 3];
  if (f) snprintf_P(path, sizeof(path), PSTR("/%.6s/%s"), name, name);
  else   strlcpy(path, UNDATED, sizeof(path));

  // the writer reopens on its next row, the handle keeps the size at open
  SDCard::Hold hold;
  if (!fp->open(path, O_RDONLY)) {
    delete fp;
    res.send(404, FPSTR(HTTP_CT_PLAIN), F("Not found"));
    return;
  }
  s_dl = fp;

  char disp[64];
  snprintf_P(disp, sizeof(disp),
             PSTR("attachment; filename=\"%s\""), name);
  res.addHeader(F("Content-Disposition"), disp);
  res.sendPull(200, F("text/csv"), fp->fileSize(), dl_fill, dl_done, fp);
}

static void hSdYear(EGHttpResponse& res, const char* y) {
  uint16_t sel = 0;
  if (y && *y) {
    if (!valid_year(y)) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad year")); return; }
    int32_t yr = 0;
    parseI32(y, &yr);
    sel = (uint16_t)yr;
  }
  hSdList(res, sel);
}

// check each arg before it goes in a path
static void hSd(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* f = req.arg(F("f"));
  if (f && *f) {
    if (!valid_csv_name(f)) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad name")); return; }
    hSdDownload(res, f);
    return;
  }
  const char* u = req.arg(F("u"));
  if (u && *u) {
    hSdDownload(res, nullptr);
    return;
  }
  const char* m = req.arg(F("m"));
  if (m && *m) {
    if (!valid_yyyymm(m)) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad month")); return; }
    hSdMonth(res, m);
    return;
  }
  hSdYear(res, req.arg(F("y")));
}

static const corium::MenuEntry SDCARD_MENU[] PROGMEM = {
  {"/sd", "SD Logs"},
  {"", ""}
};

const corium::MenuEntry* SDCard::menuEntries() {
  return _enabled ? SDCARD_MENU : nullptr;
}

void SDCard::registerRoutes(EGHttpServer& http) {
  http.on(F("/sd/reinit"), EGHttpRequest::POST,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      static uint32_t next_s = 0;   // uptime
      uint32_t now = (uint32_t)corium::Uptime::seconds();
      if (now < next_s) {
        res.send(429, FPSTR(HTTP_CT_PLAIN), F("Try again later"));
        return;
      }
      next_s = now + SD_REINIT_S;
      sdcard.reinit();
      res.send(200, FPSTR(HTTP_CT_PLAIN), sdcard.ready() ? F("Reinit OK") : F("Reinit failed"));
    });
  http.on(F("/sd"), EGHttpRequest::GET, hSd);
}
#endif
