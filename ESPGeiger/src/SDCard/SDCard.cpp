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
#ifdef GEIGER_SDCARD_SCK
#include <SPI.h>
#endif
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
#define SD_FULL_MB 8

// rows from before the clock was set
static const char UNDATED[] = "/undated.csv";
static const uint32_t UNDATED_DAY = 1;   // openFileDay while it's open

SdFat32* sd = nullptr;

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

const corium::PrefGroup* SDCard::prefs_group() { return &SDCARD_PREF_GROUP; }

void SDCard::on_prefs_loaded() {
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

static void dateTimeCB(uint16_t *dosYear, uint16_t *dosTime) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  uint32_t s = fat_stamp(t);
  *dosYear = (uint16_t)(s >> 16);
  *dosTime = (uint16_t)s;
}

SDCard::SDCard() {
}

static void sd_lifecycle(Lifecycle::Event e) {
  if (e != Lifecycle::OTA_FAILED) sdcard.pauseWriter();
}

void SDCard::begin()
{
  Log::console(PSTR("SDCard: Init ..."));
  sd = new SdFat32();
#ifdef GEIGER_SDCARD_SCK
  // pins first, the c3 defaults are hw32's HV and pulse pins; sd's begin() then keeps them
  SPI.begin(GEIGER_SDCARD_SCK, GEIGER_SDCARD_MISO, GEIGER_SDCARD_MOSI, GEIGER_SDCARD_CS);
#endif
  DeviceInfo::wdtPause();
  bool ok = sd->begin(GEIGER_SDCARD_CS);
  DeviceInfo::wdtResume();
  if (!ok) {
    Log::console(PSTR("SDCard: Init failed ... Please check wiring or insert a card."));
    if (sd->sdErrorCode() != SD_CARD_ERROR_CMD0) fault();   // CMD0 = no card
    delete sd;
    sd = nullptr;
    return;
  }
  FsDateTime::setCallback(dateTimeCB);

  if (!myDataFile.open("TEST.TMP", O_WRITE | O_CREAT | O_TRUNC)) {
    Log::console(PSTR("SDCard: write probe failed - read-only or full?"));
    fault();
    delete sd;
    sd = nullptr;
    return;
  }
  myDataFile.close();
  sd->remove("TEST.TMP");

  sdenabled = true;
  _fault = false;
  Log::console(PSTR("SDCard: Mounted."));
  Log::console(PSTR("SDCard: Calculating free space"));
  startCount(CLEAN_FREECHECK);
  corium::Registry::due_in(this, 0);
  // remount runs begin() again
  static bool listening = false;
  if (!listening) listening = Lifecycle::on(sd_lifecycle);
}

int32_t SDCard::freeMB() const {
  if (!sd || _counting || _free < 0) return -1;
  return (int32_t)(((uint64_t)_free * sd->sectorsPerCluster()) >> 11);
}

static uint32_t clusters_of(uint32_t bytes) {
  uint32_t per = (uint32_t)sd->sectorsPerCluster() * SD_SECTOR;
  return (bytes + per - 1) / per;
}

static uint32_t packed_cutoff_year_ago() {
  time_t cutoff_t = time(nullptr) - (365L * 86400L);
  struct tm tmv;
  localtime_r(&cutoff_t, &tmv);
  return fat_stamp(tmv);
}

// strict, no ../
static bool valid_csv_name(const char* name) {
  if (!name) return false;
  size_t i = 0;
  while (i < 8) {
    if (name[i] < '0' || name[i] > '9') return false;
    i++;
  }
  return name[8] == '.' && name[9] == 'c' && name[10] == 's' && name[11] == 'v' && name[12] == '\0';
}

static bool valid_yyyymm(const char* s) {
  if (!s) return false;
  for (int i = 0; i < SD_DIR_LEN; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return s[SD_DIR_LEN] == '\0';
}

static bool valid_year(const char* s) {
  if (!s) return false;
  for (int i = 0; i < 4; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return s[4] == '\0';
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
  time_t thisMinute = time(NULL) / 60;
  if (thisMinute == lastWrittenMinute) return;
  if (lastWrittenMinute) writeMinute();
  lastWrittenMinute = thisMinute;
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
  f.print(DeviceInfo::freeHeap());
#endif
  f.println();
}

// false = no root, failed = no file
bool SDCard::openDayFile(const char* dirStr, const char* timeStr, uint32_t day, bool& failed)
{
  if (myDataFile.isOpen()) {
    myDataFile.close();
  }
  openFileDay = 0;
  _unsynced_writes = 0;

  if (!sd->chdir()) {
    fail_step(PSTR("chdir /"), dirStr, timeStr);
    return false;
  }
  const char* step = nullptr;
  if (!sd->exists(dirStr)) {
    if (sd->mkdir(dirStr)) used_clusters(1);
    else step = PSTR("mkdir");
  }
  if (!step && !sd->chdir(dirStr)) step = PSTR("chdir");

  bool fileExists = sd->exists(timeStr);
  if (!step && !myDataFile.open(timeStr, FILE_WRITE)) step = PSTR("open");
  if (step) {
    fail_step(step, dirStr, timeStr);
    failed = true;
  } else {
    _file_cl = clusters_of(myDataFile.fileSize());
    if (!fileExists) write_header(myDataFile, PSTR("DateTime"));
    openFileDay = day;
  }
  return true;
}

// clusters used, a sync every _sync_min rows; false = the file's gone
bool SDCard::wrote(const char* dir, const char* name) {
  uint32_t cl = clusters_of(myDataFile.fileSize());
  if (cl > _file_cl) {
    used_clusters(cl - _file_cl);
    _file_cl = cl;
  }
  // power loss loses unsynced rows
  if (++_unsynced_writes < _sync_min) return true;
  _unsynced_writes = 0;
  if (myDataFile.sync()) return true;
  fail_step(PSTR("sync"), dir, name);
  myDataFile.close();
  openFileDay = 0;
  return false;
}

// no clock yet: seconds since boot, dated later by the synced line
void SDCard::writeUndated()
{
  if (openFileDay != UNDATED_DAY || !myDataFile.isOpen()) {
    if (myDataFile.isOpen()) myDataFile.close();
    openFileDay = 0;
    _unsynced_writes = 0;
    if (!sd->chdir() || !myDataFile.open(UNDATED, FILE_WRITE)) {
      fail_step(PSTR("open"), "", UNDATED + 1);
      return;
    }
    _file_cl = clusters_of(myDataFile.fileSize());
    openFileDay = UNDATED_DAY;
  }
  if (!_undated) write_header(myDataFile, PSTR("Uptime"));   // once a boot
  _undated = true;
  char up[12];
  snprintf_P(up, sizeof(up), PSTR("%lu"), (unsigned long)Uptime::seconds());
  write_row(myDataFile, up);
  if (!wrote("", UNDATED + 1)) scheduleCleanup(CLEAN_FULL);
}

void SDCard::writeMinute()
{
  struct tm t;
  if (!Clock::utcTm(&t)) {
    writeUndated();
    return;
  }
  int year = 1900 + t.tm_year;
  int mon = t.tm_mon + 1;

  bool forceCleanup = false;
  char timeStr[SD_NAME_LEN + 1];
  char dirStr[SD_DIR_LEN + 1];
  char dateTime[20];

  snprintf_P(timeStr, sizeof(timeStr), PSTR("%04d%02d%02d.csv"), year, mon, t.tm_mday);
  snprintf_P(dirStr, sizeof(dirStr), PSTR("%04d%02d"), year, mon);
#ifdef SDCARD_LOG_UNIXTIME
  fmt_u64(dateTime, sizeof(dateTime), (uint64_t)time(nullptr));
#else
  snprintf_P(dateTime, sizeof(dateTime), PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
    year, mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
#endif

  uint32_t currentDay = year * 10000u + mon * 100u + t.tm_mday;

  if (openFileDay == UNDATED_DAY && myDataFile.isOpen()) {
    // dates the rows above it
    myDataFile.print(F("synced,"));
    myDataFile.print(dateTime);
    myDataFile.print(',');
    myDataFile.println((unsigned long)Uptime::seconds());
  }

  if (currentDay != openFileDay || !myDataFile.isOpen()) {
    if (!openDayFile(dirStr, timeStr, currentDay, forceCleanup)) return;
  }

  if (myDataFile.isOpen()) {
    write_row(myDataFile, dateTime);
    if (!wrote(dirStr, timeStr)) forceCleanup = true;
  }

  // looks full, count it properly before deleting
  int32_t mb = freeMB();
  if (forceCleanup || (mb >= 0 && mb <= SD_FULL_MB)) scheduleCleanup(CLEAN_FULL);
}

// our own writes, so the FAT is only counted at mount and when full
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
  if (!sdenabled) return;
  if (!_counting) {
    corium::Registry::due_in(this, ms_to_minute());
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
      corium::Registry::due_in(this, ms_to_minute());
      return;
    }
    uint32_t left = total - _fat_sec * per;
    _free += free_entries(buf, left < per ? left : per, width);
    _fat_sec++;
  }
  if (_fat_sec * per < total) {
    corium::Registry::due_in(this, SD_COUNT_MS);
    return;
  }
  const uint8_t what = _counting;
  _counting = CLEAN_NONE;
  corium::Registry::due_in(this, ms_to_minute());
  cleanup(what);
}

void SDCard::cleanup(uint8_t what) {
  Hold hold;
  if (what == CLEAN_FREECHECK) {
    uint32_t freespace = freeMB();
    Log::console(PSTR("SDCard: %lu MB free."), (unsigned long)freespace);
    if (freespace <= SD_FULL_MB && !deleteOldest(packed_cutoff_year_ago())) disableFull();
  } else {
    uint8_t maxDeletes = 10;
    uint32_t freespace = freeMB();
    uint32_t cutoff = packed_cutoff_year_ago();
    while (freespace <= SD_FULL_MB && maxDeletes-- > 0) {
      if (!deleteOldest(cutoff)) break;
      freespace = freeMB();
    }
    if (freespace <= SD_FULL_MB) disableFull();
  }
}

void SDCard::disableFull() {
  Log::console(PSTR("SDCard: disabled - card full"));
  sdenabled = false;
  fault();
}

// once till it mounts: the step and SdFat's code
void SDCard::fail_step(const char* step, const char* dir, const char* file) {
  if (!_fault) {
    Log::console(PSTR("SDCard: %s/%s: %s failed (err 0x%02x)"), dir, file, step, sd->sdErrorCode());
  }
  fault();
}

void SDCard::fault() {
  if (_fault) return;
  _fault = true;
  corium::Registry::emit(TOPIC_ERROR);
}

// leaves cwd at root
static bool oldest_month_after(const char* after, char* out, size_t cap) {
  File32 rootDir;
  File32 subDir;
  out[0] = '\0';

  if (!sd->chdir()) return false;
  if (!rootDir.open("/")) return false;

  while (subDir.openNext(&rootDir, O_RDONLY)) {
    if (subDir.isSubDir() && !subDir.isHidden()) {
      char dirName[SD_NAME_LEN + 1];
      subDir.getName(dirName, sizeof(dirName));
      if (valid_yyyymm(dirName) && strcmp(dirName, after) > 0 &&
          (out[0] == '\0' || strcmp(dirName, out) < 0)) {
        strlcpy(out, dirName, cap);
      }
    }
    subDir.close();
    yield();
  }
  rootDir.close();
  return out[0] != '\0';
}

static bool oldest_csv_in(const char* dir, uint32_t cutoffPacked,
                          char* out, size_t cap, bool& any_csv) {
  File32 subDir;
  File32 file;
  uint32_t oldestModified = 0xFFFFFFFFul;
  out[0] = '\0';
  any_csv = false;

  if (!subDir.open(dir, O_RDONLY)) return false;
  while (file.openNext(&subDir, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[SD_NAME_LEN + 1];
      file.getName(f_name, sizeof(f_name));
      size_t nameLen = strlen(f_name);
      if (nameLen >= 4 && strcmp_P(f_name + nameLen - 4, PSTR(".csv")) == 0) {
        any_csv = true;
        uint16_t date_AGPS, time_AGPS;
        file.getModifyDateTime(&date_AGPS, &time_AGPS);
        uint32_t lastModified = (uint32_t(date_AGPS) << 16) | time_AGPS;

        if (lastModified < cutoffPacked && lastModified < oldestModified) {
          oldestModified = lastModified;
          strlcpy(out, f_name, cap);
        }
      }
    }
    file.close();
    yield();
  }
  subDir.close();
  return true;
}

bool SDCard::deleteOldest(uint32_t cutoffPacked) {
  // skip stuck months, max 12
  char after[SD_DIR_LEN + 1] = {};
  for (uint8_t skips = 0; skips <= 12; skips++) {
    char oldestDir[SD_DIR_LEN + 1];
    char oldestFile[SD_NAME_LEN + 1];
    bool any_csv = false;

    if (!oldest_month_after(after, oldestDir, sizeof(oldestDir))) return false;
    if (!oldest_csv_in(oldestDir, cutoffPacked, oldestFile, sizeof(oldestFile), any_csv)) return false;

    if (!any_csv) {
      if (sd->rmdir(oldestDir)) return true;
      memcpy(after, oldestDir, sizeof(after));
      continue;
    }
    if (oldestFile[0] == '\0') return false;

    bool deleted = false;
    File32 f;
    if (sd->chdir(oldestDir) && f.open(oldestFile, O_RDONLY)) {
      uint32_t size = f.fileSize();
      f.close();
      Log::console(PSTR("SDCard: Deleting old file %s/%s"), oldestDir, oldestFile);
      deleted = sd->remove(oldestFile);
      if (deleted) _free += clusters_of(size);
    }
    sd->chdir();
    return deleted;
  }
  return false;
}

void SDCard::reinit() {
  // first, the loop checks it
  sdenabled = false;
  if (myDataFile.isOpen()) {
    myDataFile.sync();
    myDataFile.close();
  }
  openFileDay = 0;
  _unsynced_writes = 0;
  if (sd) {
    delete sd;
    sd = nullptr;
  }
  begin();
}

SDCard::Hold::Hold()  { sdcard.pauseWriter(); }

void SDCard::pauseWriter() {
  if (myDataFile.isOpen()) {
    myDataFile.sync();
    myDataFile.close();
  }
  openFileDay = 0;
  _unsynced_writes = 0;
}

struct MonthEntry {
  uint32_t yyyymm;
  uint16_t files;
  uint32_t bytes;
};

struct DayEntry {
  uint8_t  day;
  uint32_t bytes;
};

static void end_page(EGHttpResponse& res) {
  WebPortal::endPage(res);
}

static void end_page(EGHttpResponse& res, const __FlashStringHelper* msg) {
  res.sendChunk(msg);
  end_page(res);
}

static void tally_month(File32& subDir, MonthEntry& m, uint64_t& totalBytes) {
  File32 file;
  while (file.openNext(&subDir, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[SD_NAME_LEN + 1];
      file.getName(f_name, sizeof(f_name));
      if (valid_csv_name(f_name)) {
        m.files++;
        m.bytes += file.fileSize();
        totalBytes += file.fileSize();
      }
    }
    file.close();
    yield();
  }
}

static uint16_t scan_months(MonthEntry* months, uint16_t maxMonths, uint64_t& totalBytes) {
  uint16_t count = 0;
  File32 rootDir, subDir;
  if (!sd->chdir() || !rootDir.open("/")) return 0;
  while (count < maxMonths && subDir.openNext(&rootDir, O_RDONLY)) {
    if (subDir.isSubDir() && !subDir.isHidden()) {
      char dirName[SD_NAME_LEN + 1];
      subDir.getName(dirName, sizeof(dirName));
      if (valid_yyyymm(dirName)) {
        MonthEntry& m = months[count];
        int32_t ym = 0;
        parse_i32(dirName, &ym);
        m.yyyymm = (uint32_t)ym;
        m.files  = 0;
        m.bytes  = 0;
        tally_month(subDir, m, totalBytes);
        if (m.files > 0) count++;
      }
    }
    subDir.close();
    yield();
  }
  rootDir.close();
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

static void send_list_summary(EGHttpResponse& res, uint16_t count, uint64_t totalBytes) {
  int32_t freespace = sdcard.freeMB();
  char fs[24];
  if (freespace >= 0) snprintf_P(fs, sizeof(fs), PSTR("%lu MB free"), (unsigned long)freespace);
  else if (sdcard.counting()) snprintf_P(fs, sizeof(fs), PSTR("counting free space"));
  else snprintf_P(fs, sizeof(fs), PSTR("free space unknown"));

  char head[160];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p>%u month%s, %lu KB total, %s.</p>"),
    (unsigned)count, count == 1 ? PSTR("") : PSTR("s"),
    (unsigned long)((totalBytes + 1023) / 1024),
    fs);
  if (n > 0) res.sendChunk(head, (size_t)n);
}

// months newest first, count > 0
static uint16_t send_year_picker(EGHttpResponse& res, const MonthEntry* months,
                                 uint16_t count, uint16_t selectedYear) {
  constexpr uint8_t MAX_YEARS = 30;
  uint16_t years[MAX_YEARS];
  uint8_t yearCount = 0;
  for (uint16_t i = 0; i < count && yearCount < MAX_YEARS; i++) {
    uint16_t y = (uint16_t)(months[i].yyyymm / 100);
    if (yearCount == 0 || years[yearCount - 1] != y) {
      years[yearCount++] = y;
    }
  }

  uint16_t showYear = (selectedYear > 0) ? selectedYear : years[0];

  if (yearCount > 1) {
    res.sendChunk(F("<p>Year: <select onchange=\"location.href='/sd?y='+this.value\">"));
    for (uint8_t i = 0; i < yearCount; i++) {
      char opt[64];
      int on = snprintf_P(opt, sizeof(opt),
        PSTR("<option value='%u'%s>%u</option>"),
        (unsigned)years[i],
        years[i] == showYear ? PSTR(" selected") : PSTR(""),
        (unsigned)years[i]);
      if (on > 0) res.sendChunk(opt, (size_t)on);
    }
    res.sendChunk(F("</select></p>"));
  }
  return showYear;
}

static void send_month_rows(EGHttpResponse& res, const MonthEntry* months,
                            uint16_t count, uint16_t showYear) {
  bool any = false;
  res.sendChunk(F("<div class=menu>"));
  for (uint16_t i = 0; i < count; i++) {
    uint32_t ym = months[i].yyyymm;
    if ((uint16_t)(ym / 100) != showYear) continue;
    any = true;
    char row[160];
    int rn = snprintf_P(row, sizeof(row),
      PSTR("<a href='/sd?m=%06lu'>%04u-%02u <small>(%u file%s, %lu KB)</small></a>"),
      (unsigned long)ym,
      (unsigned)(ym / 100), (unsigned)(ym % 100),
      (unsigned)months[i].files, months[i].files == 1 ? PSTR("") : PSTR("s"),
      (unsigned long)((months[i].bytes + 1023) / 1024));
    if (rn > 0) res.sendChunk(row, (size_t)rn);
  }
  res.sendChunk(F("</div>"));
  if (!any) res.sendChunk(F("<p>No log files in this year.</p>"));
}

static void hSdList(EGHttpResponse& res, uint16_t selectedYear = 0) {
  WebPortal::beginPage(res, F("SD Logs"));

  if (!sdcard.ready() || !sd) {
    end_page(res, F("<p>SD card not available.</p>"));
    return;
  }

  SDCard::Hold hold;

  constexpr uint16_t MAX_MONTHS = 120;   // 10 years
  MonthEntry* months = (MonthEntry*)malloc(MAX_MONTHS * sizeof(MonthEntry));
  if (!months) {
    end_page(res, F("<p>Out of memory.</p>"));
    return;
  }

  uint64_t totalBytes = 0;
  uint16_t count = scan_months(months, MAX_MONTHS, totalBytes);
  sort_months_desc(months, count);
  send_list_summary(res, count, totalBytes);
  if (sd->exists(UNDATED)) {
    res.sendChunk(F("<p><a href='/sd?u=1'>Undated rows</a>, from before the clock was set.</p>"));
  }

  if (count == 0) {
    res.sendChunk(F("<p>No log files yet.</p>"));
    free(months);
    end_page(res);
    return;
  }

  uint16_t showYear = send_year_picker(res, months, count, selectedYear);
  send_month_rows(res, months, count, showYear);

  free(months);
  end_page(res);
}

static bool scan_days(const char* dir, DayEntry* days, uint8_t maxDays,
                      uint8_t& count, uint32_t& totalBytes) {
  File32 subDir, file;
  bool opened = sd->chdir() && subDir.open(dir, O_RDONLY);
  if (!opened) return false;
  while (count < maxDays && file.openNext(&subDir, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[SD_NAME_LEN + 1];
      file.getName(f_name, sizeof(f_name));
      if (valid_csv_name(f_name) && strncmp(f_name, dir, SD_DIR_LEN) == 0) {
        days[count].day   = (uint8_t)((f_name[6] - '0') * 10 + (f_name[7] - '0'));
        days[count].bytes = file.fileSize();
        totalBytes += days[count].bytes;
        count++;
      }
    }
    file.close();
    yield();
  }
  subDir.close();
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
      dir, dir + 4, (unsigned)days[i].day,
      (unsigned long)((days[i].bytes + 1023) / 1024));
    if (rn > 0) res.sendChunk(row, (size_t)rn);
  }
  res.sendChunk(F("</div>"));
}

static void hSdMonth(EGHttpResponse& res, const char* yyyymm) {
  char dir[SD_DIR_LEN + 1];
  for (int i = 0; i < SD_DIR_LEN; i++) dir[i] = yyyymm[i];
  dir[SD_DIR_LEN] = '\0';

  WebPortal::beginPage(res, F("SD Logs"));

  if (!sdcard.ready() || !sd) {
    end_page(res, F("<p>SD card not available.</p>"));
    return;
  }

  SDCard::Hold hold;
  res.sendChunk(F("<p><a href='/sd'>&larr; All months</a></p>"));

  constexpr uint8_t MAX_DAYS = 31;
  DayEntry days[MAX_DAYS];
  uint8_t count = 0;
  uint32_t totalBytes = 0;
  if (!scan_days(dir, days, MAX_DAYS, count, totalBytes)) {
    end_page(res, F("<p>Month not found.</p>"));
    return;
  }
  sort_days_desc(days, count);

  char head[80];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p>%u file%s, %lu KB.</p>"),
    (unsigned)count, count == 1 ? PSTR("") : PSTR("s"),
    (unsigned long)((totalBytes + 1023) / 1024));
  if (n > 0) res.sendChunk(head, (size_t)n);

  if (count == 0) {
    res.sendChunk(F("<p>No log files in this month.</p>"));
  } else {
    send_day_rows(res, dir, days, count);
  }

  end_page(res);
}

// f checked, nullptr = the undated rows
static void hSdDownload(EGHttpResponse& res, const char* f) {
  // arg() buffer is shared
  char filename[SD_NAME_LEN + 1];
  strlcpy(filename, f ? f : UNDATED + 1, sizeof(filename));

  if (!sdcard.ready() || !sd) {
    res.send(503, FPSTR(HTTP_CT_PLAIN), F("SD not ready"));
    return;
  }

  char dir[SD_DIR_LEN + 1];
  strlcpy(dir, filename, sizeof(dir));

  SDCard::Hold hold;

  if (!sd->chdir() || (f && !sd->chdir(dir))) {
    sd->chdir();
    res.send(404, FPSTR(HTTP_CT_PLAIN), F("Not found"));
    return;
  }

  File32 fp;
  if (!fp.open(filename, O_RDONLY)) {
    sd->chdir();
    res.send(404, FPSTR(HTTP_CT_PLAIN), F("Not found"));
    return;
  }

  char disp[64];
  snprintf_P(disp, sizeof(disp),
             PSTR("attachment; filename=\"%s\""), filename);
  res.addHeader(F("Content-Disposition"), disp);
  res.beginChunked(200, F("text/csv"));

  uint8_t buf[512];
  while (true) {
    int got = fp.read(buf, sizeof(buf));
    if (got <= 0) break;
    if (!res.sendChunk((const char*)buf, (size_t)got)) break;
  }
  fp.close();
  sd->chdir();
  res.endChunked();
}

static const corium::MenuEntry SDCARD_MENU[] PROGMEM = {
  {"/sd", "SD Logs"},
  {"", ""}
};

const corium::MenuEntry* SDCard::menuEntries() {
  return sdenabled ? SDCARD_MENU : nullptr;
}

// check each arg before it goes in a path
void SDCard::registerRoutes(EGHttpServer& http) {
  http.on(F("/sd/reinit"), EGHttpRequest::POST,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      sdcard.reinit();
      res.send(200, FPSTR(HTTP_CT_PLAIN), sdcard.ready() ? F("Reinit OK") : F("Reinit failed"));
    });
  http.on(F("/sd"), EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
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
      const char* y = req.arg(F("y"));
      uint16_t selYear = 0;
      if (y && *y) {
        if (!valid_year(y)) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad year")); return; }
        int32_t yr = 0;
        parse_i32(y, &yr);
        selYear = (uint16_t)yr;
      }
      hSdList(res, selYear);
    });
}
#endif
