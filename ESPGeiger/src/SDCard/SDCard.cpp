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
#include <EGHttpServer.h>
#include "SDCard.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../NTP/NTP.h"
#include "../Util/DeviceInfo.h"
#include "../Util/MathUtil.h"
#include "../WebPortal/WebPortal.h"

SdFat32* sd = nullptr;

SDCard sdcard;
EG_REGISTER_MODULE(sdcard)

EG_PSTR(SD_L_SI, "Sync Interval (min)");
EG_PSTR(SD_H_SI, "Minutes between syncs. 1=safest, higher=less SD wear");

static const EGPref SDCARD_PREF_ITEMS[] = {
  {"sync_min", SD_L_SI, SD_H_SI, "1", nullptr, 1, 5, 0, EGP_UINT, 0},
};

static const EGPrefGroup SDCARD_PREF_GROUP = {
  "sdcard", "SD Card", 1,
  SDCARD_PREF_ITEMS,
  sizeof(SDCARD_PREF_ITEMS) / sizeof(SDCARD_PREF_ITEMS[0]),
  EGP_CAT_SYSTEM,
};

const EGPrefGroup* SDCard::prefs_group() { return &SDCARD_PREF_GROUP; }

void SDCard::on_prefs_loaded() {
  _sync_min = (uint8_t)clamp((uint32_t)EGPrefs::getUInt("sdcard", "sync_min"), 1u, 5u);
}

static void dateTimeCB(uint16_t *dosYear, uint16_t *dosTime) {
    time_t now;
    now = time(nullptr);
    struct tm *tiempo = localtime(&now);
    *dosYear = ((tiempo->tm_year - 80) << 9) | ((tiempo->tm_mon + 1) << 5) | tiempo->tm_mday;
    *dosTime = (tiempo->tm_hour << 11) | (tiempo->tm_min << 5) | tiempo->tm_sec;
}

SDCard::SDCard() {
}

void SDCard::begin()
{
  EGModuleRegistry::set_tick_enabled(this, false);
  Log::console(PSTR("SDCard: Init ..."));
  sd = new SdFat32();
#ifdef ESP8266
  ESP.wdtDisable();
#endif
  bool ok = sd->begin(GEIGER_SDCARD_CS);
#ifdef ESP8266
  ESP.wdtEnable(WDTO_8S);
#endif
  if (!ok) {
    Log::console(PSTR("SDCard: Init failed ... Please check wiring or insert a card."));
    delete sd;
    sd = nullptr;
    return;
  }
  FsDateTime::setCallback(dateTimeCB);

  if (!myDataFile.open("TEST.TMP", O_WRITE | O_CREAT | O_TRUNC)) {
    Log::console(PSTR("SDCard: write probe failed - read-only or full?"));
    delete sd;
    sd = nullptr;
    return;
  }
  myDataFile.close();
  sd->remove("TEST.TMP");

  sdenabled = true;
  _freecheck_pending = true;
  Log::console(PSTR("SDCard: Mounted."));
  EGModuleRegistry::set_tick_enabled(this, true);
}

static uint32_t packed_cutoff_year_ago() {
  time_t now_t = time(nullptr);
  time_t cutoff_t = now_t - (365L * 86400L);
  struct tm tmv;
  localtime_r(&cutoff_t, &tmv);
  uint16_t date = (uint16_t)((((tmv.tm_year + 1900) - 1980) << 9) |
                             ((tmv.tm_mon + 1) << 5) |
                             tmv.tm_mday);
  uint16_t timev = (uint16_t)((tmv.tm_hour << 11) |
                              (tmv.tm_min << 5) |
                              (tmv.tm_sec >> 1));
  return ((uint32_t)date << 16) | timev;
}

void SDCard::s_tick(unsigned long stick_now)
{
  if (!sdenabled) {
    return;
  }

  if (!ntpclient.synced) return;

  if (_freecheck_pending) {
    _freecheck_pending = false;
    Log::console(PSTR("SDCard: Calculating free space"));
    uint32_t freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);
    Log::console(PSTR("SDCard: %lu MB free."), (unsigned long)freespace);
    if (freespace <= 8) {
      uint32_t cutoff = packed_cutoff_year_ago();
      if (!deleteOldest(cutoff)) disableFull();
    }
    return;
  }

  time_t currentTime = time (NULL);

  // Fire once per new minute. Check minute bucket before gmtime - gmtime
  // is ~100us on ESP8266 so skipping it on 59/60 ticks saves real time.
  time_t thisMinute = currentTime / 60;
  if (thisMinute == lastWrittenMinute) {
    return;
  }
  if (lastWrittenMinute == 0) {
    // First valid tick after boot - don't write immediately, just prime.
    lastWrittenMinute = thisMinute;
    return;
  }
  lastWrittenMinute = thisMinute;

  struct tm *timeinfo = gmtime (&currentTime);

  bool forceCleanup = false;
  char timeStr[23];
  char dirStr[20];
  char dateTime[20];

  snprintf_P (timeStr, sizeof (timeStr), PSTR("%04d%02d%02d.csv"), 1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday);
  snprintf_P (dirStr, sizeof (dirStr), PSTR("%04d%02d"), 1900+timeinfo->tm_year, timeinfo->tm_mon+1);
  snprintf_P (dateTime, sizeof (dateTime), PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
    1900+timeinfo->tm_year, timeinfo->tm_mon+1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec
  );

  uint32_t currentDay = (1900+timeinfo->tm_year) * 10000u
                      + (timeinfo->tm_mon+1) * 100u
                      + timeinfo->tm_mday;

  // Open file only on first write or day rollover - saves the open() cost
  // (~10-30ms) on every other minute. sync() below still flushes per-minute
  // so durability is unchanged.
  if (currentDay != openFileDay || !myDataFile.isOpen()) {
    if (myDataFile.isOpen()) {
      myDataFile.close();
    }
    openFileDay = 0;
    _unsynced_writes = 0;

    if (!sd->chdir()) {
      Log::console(PSTR("SDCard: Cannot chdir to root"));
      return;
    }
    if (!sd->exists(dirStr)) {
      sd->mkdir(dirStr);
    }
    sd->chdir(dirStr);

    bool fileExists = sd->exists(timeStr);
    if (!myDataFile.open(timeStr, FILE_WRITE)) {
      Log::console(PSTR("SDCard: Unable to create %s/%s"), dirStr, timeStr);
      forceCleanup = true;
    } else {
      if (!fileExists) {
        myDataFile.print(F("DateTime,CPM,uSv/h,CPM5,CPM15"));
#ifdef SDCARD_EXTENDEDLOG
        myDataFile.print(F(",FreeMem"));
#endif
        myDataFile.println();
      }
      openFileDay = currentDay;
    }
  }

  if (myDataFile.isOpen()) {
#ifdef SDCARD_LOG_UNIXTIME
    myDataFile.print(time(NULL));
#else
    myDataFile.print(dateTime);
#endif
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_cpmf(), 2);
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_usv(), 4);
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_cpm5f(), 2);
    myDataFile.print(F(","));
    myDataFile.print(gcounter.get_cpm15f(), 2);
#ifdef SDCARD_EXTENDEDLOG
    myDataFile.print(F(","));
    myDataFile.print(DeviceInfo::freeHeap());
#endif
    myDataFile.println();

    // Sync every _sync_min writes. Between syncs, data is buffered in RAM
    // only - power loss risks losing up to (_sync_min - 1) rows.
    _unsynced_writes++;
    if (_unsynced_writes >= _sync_min) {
      if (!myDataFile.sync()) {
        myDataFile.close();
        openFileDay = 0;
        forceCleanup = true;
      }
      _unsynced_writes = 0;
    }
  }

  static uint8_t clean_sec = 0;
  static uint16_t clean_min = 0;
  bool clean_due = false;
  if (++clean_sec >= 60) { clean_sec = 0; if (++clean_min >= 1440) { clean_min = 0; clean_due = true; } }
  if (clean_due || forceCleanup) {
    // Close before cleanup so deleteOldest never races our handle.
    if (myDataFile.isOpen()) {
      myDataFile.close();
      openFileDay = 0;
      _unsynced_writes = 0;
    }
    uint8_t maxDeletes = 10;
    uint32_t freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);
    uint32_t cutoff = packed_cutoff_year_ago();
    while (freespace <= 8 && maxDeletes-- > 0) {
      if (!deleteOldest(cutoff)) break;
      freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);
    }
    if (freespace <= 8) disableFull();
  }
}

void SDCard::disableFull() {
  Log::console(PSTR("SDCard: disabled - card full"));
  sdenabled = false;
  EGModuleRegistry::set_tick_enabled(this, false);
}

bool SDCard::deleteOldest(uint32_t cutoffPacked) {
  File32 rootDir;
  File32 subDir;
  File32 file;
  char oldestDir[13] = "";
  char oldestFile[13] = "";
  uint32_t oldestModified = 0xFFFFFFFFul;

  if (!sd->chdir()) return false;
  if (!rootDir.open("/")) return false;

  while (subDir.openNext(&rootDir, O_RDONLY)) {
    if (subDir.isSubDir() && !subDir.isHidden()) {
      char dirName[13];
      subDir.getName(dirName, sizeof(dirName));

      while (file.openNext(&subDir, O_RDONLY)) {
        if (!file.isSubDir() && !file.isHidden()) {
          char f_name[13];
          file.getName(f_name, sizeof(f_name));
          size_t nameLen = strlen(f_name);
          if (nameLen >= 4 && strcmp(f_name + nameLen - 4, ".csv") == 0) {
            uint16_t date_AGPS, time_AGPS;
            file.getModifyDateTime(&date_AGPS, &time_AGPS);
            uint32_t lastModified = (uint32_t(date_AGPS) << 16) | time_AGPS;

            if (lastModified < cutoffPacked && lastModified < oldestModified) {
              oldestModified = lastModified;
              strncpy(oldestDir, dirName, sizeof(oldestDir) - 1);
              oldestDir[sizeof(oldestDir) - 1] = '\0';
              strncpy(oldestFile, f_name, sizeof(oldestFile) - 1);
              oldestFile[sizeof(oldestFile) - 1] = '\0';
            }
          }
        }
        file.close();
        yield();   // each iter is SPI file open + stat; full walk can be seconds.
      }
    }
    subDir.close();
  }
  rootDir.close();

  if (oldestFile[0] == '\0') return false;

  bool deleted = false;
  if (sd->chdir(oldestDir) && sd->exists(oldestFile)) {
    Log::console(PSTR("SDCard: Deleting old file %s/%s"), oldestDir, oldestFile);
    deleted = sd->remove(oldestFile);
  }
  sd->chdir();
  return deleted;
}

void SDCard::reinit() {
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
  sdenabled = false;
  _freecheck_pending = true;
  EGModuleRegistry::set_tick_enabled(this, false);
  begin();
}

void SDCard::pauseWriter() {
  if (myDataFile.isOpen()) {
    myDataFile.sync();
    myDataFile.close();
  }
  openFileDay = 0;
  _unsynced_writes = 0;
}

// "YYYYMMDD.csv": 8 digits + ".csv". Strict check so /sd?f=... can't
// be coaxed into ../ traversal or anything but a log filename.
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
  for (int i = 0; i < 6; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return s[6] == '\0';
}

static bool valid_year(const char* s) {
  if (!s) return false;
  for (int i = 0; i < 4; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return s[4] == '\0';
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

// Top page: months for the selected year (or most recent if 0), newest first.
// Year dropdown appears when more than one year is on disk.
static void hSdList(EGHttpResponse& res, uint16_t selectedYear = 0) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("SD Logs"));

  if (!sdcard.ready() || !sd) {
    res.sendChunk(F("<p>SD card not available.</p>"));
    WebPortal::sendPageTail(res);
    res.endChunked();
    return;
  }

  sdcard.pauseWriter();

  constexpr uint16_t MAX_MONTHS = 120;   // 10 years
  MonthEntry* months = (MonthEntry*)malloc(MAX_MONTHS * sizeof(MonthEntry));
  if (!months) {
    res.sendChunk(F("<p>Out of memory.</p>"));
    WebPortal::sendPageTail(res);
    res.endChunked();
    return;
  }

  uint16_t count = 0;
  uint64_t totalBytes = 0;
  File32 rootDir, subDir, file;
  if (sd->chdir() && rootDir.open("/")) {
    while (count < MAX_MONTHS && subDir.openNext(&rootDir, O_RDONLY)) {
      if (subDir.isSubDir() && !subDir.isHidden()) {
        char dirName[13];
        subDir.getName(dirName, sizeof(dirName));
        if (valid_yyyymm(dirName)) {
          MonthEntry& m = months[count];
          m.yyyymm = (uint32_t)atol(dirName);
          m.files  = 0;
          m.bytes  = 0;
          while (file.openNext(&subDir, O_RDONLY)) {
            if (!file.isSubDir() && !file.isHidden()) {
              char f_name[13];
              file.getName(f_name, sizeof(f_name));
              if (valid_csv_name(f_name)) {
                m.files++;
                m.bytes += file.fileSize();
                totalBytes += file.fileSize();
              }
            }
            file.close();
          }
          if (m.files > 0) count++;
        }
      }
      subDir.close();
    }
    rootDir.close();
  }

  for (uint16_t i = 1; i < count; i++) {
    MonthEntry tmp = months[i];
    uint16_t j = i;
    while (j > 0 && months[j - 1].yyyymm < tmp.yyyymm) {
      months[j] = months[j - 1];
      j--;
    }
    months[j] = tmp;
  }

  uint32_t freespace = (uint32_t)(((uint64_t)sd->freeClusterCount() * sd->sectorsPerCluster()) >> 11);

  char head[160];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p>%u month%s, %lu KB total, %lu MB free.</p>"),
    (unsigned)count, count == 1 ? "" : "s",
    (unsigned long)((totalBytes + 1023) / 1024),
    (unsigned long)freespace);
  if (n > 0) res.sendChunk(head, (size_t)n);

  if (count == 0) {
    res.sendChunk(F("<p>No log files yet.</p>"));
    free(months);
    WebPortal::sendPageTail(res);
    res.endChunked();
    return;
  }

  // Unique years in descending order. months[] is already sorted desc,
  // so consecutive entries with the same year collapse cleanly.
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
        years[i] == showYear ? " selected" : "",
        (unsigned)years[i]);
      if (on > 0) res.sendChunk(opt, (size_t)on);
    }
    res.sendChunk(F("</select></p>"));
  }

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
      (unsigned)months[i].files, months[i].files == 1 ? "" : "s",
      (unsigned long)((months[i].bytes + 1023) / 1024));
    if (rn > 0) res.sendChunk(row, (size_t)rn);
  }
  res.sendChunk(F("</div>"));
  if (!any) res.sendChunk(F("<p>No log files in this year.</p>"));

  free(months);
  WebPortal::sendPageTail(res);
  res.endChunked();
}

// Day list within one YYYYMM folder.
static void hSdMonth(EGHttpResponse& res, const char* yyyymm) {
  char dir[7];
  for (int i = 0; i < 6; i++) dir[i] = yyyymm[i];
  dir[6] = '\0';

  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("SD Logs"));

  if (!sdcard.ready() || !sd) {
    res.sendChunk(F("<p>SD card not available.</p>"));
    WebPortal::sendPageTail(res);
    res.endChunked();
    return;
  }

  sdcard.pauseWriter();
  res.sendChunk(F("<p><a href='/sd'>&larr; All months</a></p>"));

  constexpr uint8_t MAX_DAYS = 31;
  DayEntry days[MAX_DAYS];
  uint8_t count = 0;
  uint32_t totalBytes = 0;
  File32 subDir, file;
  bool opened = sd->chdir() && subDir.open(dir, O_RDONLY);
  if (!opened) {
    res.sendChunk(F("<p>Month not found.</p>"));
    WebPortal::sendPageTail(res);
    res.endChunked();
    return;
  }
  while (count < MAX_DAYS && file.openNext(&subDir, O_RDONLY)) {
    if (!file.isSubDir() && !file.isHidden()) {
      char f_name[13];
      file.getName(f_name, sizeof(f_name));
      if (valid_csv_name(f_name) && strncmp(f_name, dir, 6) == 0) {
        days[count].day   = (uint8_t)((f_name[6] - '0') * 10 + (f_name[7] - '0'));
        days[count].bytes = file.fileSize();
        totalBytes += days[count].bytes;
        count++;
      }
    }
    file.close();
  }
  subDir.close();

  for (uint8_t i = 1; i < count; i++) {
    DayEntry tmp = days[i];
    uint8_t j = i;
    while (j > 0 && days[j - 1].day < tmp.day) {
      days[j] = days[j - 1];
      j--;
    }
    days[j] = tmp;
  }

  char head[80];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p>%u file%s, %lu KB.</p>"),
    (unsigned)count, count == 1 ? "" : "s",
    (unsigned long)((totalBytes + 1023) / 1024));
  if (n > 0) res.sendChunk(head, (size_t)n);

  if (count == 0) {
    res.sendChunk(F("<p>No log files in this month.</p>"));
  } else {
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

  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hSdDownload(EGHttpRequest& req, EGHttpResponse& res, const char* f) {
  // Copy out of arg()'s shared static buffer before any other arg/header call.
  char filename[13];
  for (int i = 0; i < 12; i++) filename[i] = f[i];
  filename[12] = '\0';

  if (!sdcard.ready() || !sd) {
    res.send(503, "text/plain", "SD not ready");
    return;
  }

  // Dir is the first 6 chars (YYYYMM).
  char dir[7];
  for (int i = 0; i < 6; i++) dir[i] = filename[i];
  dir[6] = '\0';

  sdcard.pauseWriter();

  if (!sd->chdir() || !sd->chdir(dir)) {
    sd->chdir();
    res.send(404, "text/plain", "Not found");
    return;
  }

  File32 fp;
  if (!fp.open(filename, O_RDONLY)) {
    sd->chdir();
    res.send(404, "text/plain", "Not found");
    return;
  }

  char disp[64];
  snprintf_P(disp, sizeof(disp),
             PSTR("attachment; filename=\"%s\""), filename);
  res.addHeader("Content-Disposition", disp);
  res.beginChunked(200, "text/csv");

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

static const EGMenuEntry SDCARD_MENU[] = {
  {"/sd", "SD Logs"},
  {nullptr, nullptr}
};

const EGMenuEntry* SDCard::menuEntries() {
  return sdenabled ? SDCARD_MENU : nullptr;
}

void SDCard::registerRoutes(EGHttpServer& http) {
  http.on("/sd/reinit", EGHttpRequest::POST,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      sdcard.reinit();
      res.send(200, "text/plain", sdcard.ready() ? "Reinit OK" : "Reinit failed");
    });
  http.on("/sd", EGHttpRequest::GET,
    [](EGHttpRequest& req, EGHttpResponse& res, void*) {
      const char* f = req.arg("f");
      if (f && *f) {
        if (!valid_csv_name(f)) { res.send(400, "text/plain", "Bad name"); return; }
        hSdDownload(req, res, f);
        return;
      }
      const char* m = req.arg("m");
      if (m && *m) {
        if (!valid_yyyymm(m)) { res.send(400, "text/plain", "Bad month"); return; }
        hSdMonth(res, m);
        return;
      }
      const char* y = req.arg("y");
      uint16_t selYear = 0;
      if (y && *y) {
        if (!valid_year(y)) { res.send(400, "text/plain", "Bad year"); return; }
        selYear = (uint16_t)atoi(y);
      }
      hSdList(res, selYear);
    });
}
#endif
