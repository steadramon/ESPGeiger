/*
  Layout32.cpp - the 4 MB esp32 partition table, and the move to bigger app slots

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
#ifdef ESP32
#include "Layout.h"
#include "DeviceInfo.h"
#include "Fs.h"
#include "Lifecycle.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Util/Bytes.h"
#include <string.h>
#include <sdkconfig.h>
#include <nvs.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <esp_rom_md5.h>
#include <esp_spi_flash.h>

namespace corium {
namespace Layout {

struct Row {
  uint8_t type;
  uint8_t sub;
  uint32_t off;
  uint32_t size;
  const char* label;
};

// min_spiffs.csv
static const Row ROWS[] = {
  {1, 0x02, 0x9000,   0x5000,   "nvs"},
  {1, 0x00, 0xe000,   0x2000,   "otadata"},
  {0, 0x10, 0x10000,  0x1E0000, "app0"},
  {0, 0x11, 0x1F0000, 0x1E0000, "app1"},
  {1, 0x82, 0x3D0000, 0x20000,  "spiffs"},
  {1, 0x03, 0x3F0000, 0x10000,  "coredump"},
};
static const uint8_t NROWS = sizeof(ROWS) / sizeof(ROWS[0]);

static const uint8_t APP1 = 3;
static const uint32_t OLD_APP1_AT = 0x150000;   // default.csv's
static const uint32_t OLD_SLOT = 0x140000;

// md5 of the 0xC00 bytes gen_esp32part.py makes from default.csv
static const uint8_t OLD_MD5[16] = {0xa0, 0x39, 0xc6, 0x6c, 0xd3, 0x48, 0x81, 0x76,
                                    0x03, 0x7b, 0x61, 0x6b, 0x75, 0x95, 0xfe, 0x72};

static const char NS[] = "layout";
static const char K_STEP[] = "st";
// record: magic, version, count, length, then per file path length, path, data length, data, then crc32
static const uint32_t MAGIC = 0x3159414C;   // "LAY1"
static const uint16_t VERSION = 1;
static const uint32_t HEAD = 12;
static const size_t PATH_CAP = 64;
static const uint8_t DEPTH = 4;
static const size_t CHUNK = 256;

static const char R_BUSY[] PROGMEM = "Layout: busy";
static const char R_TABLE[] PROGMEM = "Layout: not default.csv";
static const char R_APP0[] PROGMEM = "Layout: app0 not this build";
static const char R_BIG[] PROGMEM = "Layout: files too big";
static const char R_FLASH[] PROGMEM = "Layout: flash write failed";
static const char R_FS[] PROGMEM = "Layout: fs failed";
static const char R_BACKUP[] PROGMEM = "Layout: backup bad";
static const char R_STUCK[] PROGMEM = "Layout: TABLE BAD, USB install";

void build(uint8_t* sec) {
  memset(sec, 0xFF, SECTOR);
  for (uint8_t i = 0; i < NROWS; i++) {
    uint8_t* p = sec + i * 32;
    wrLe16(p, 0x50AA);
    p[2] = ROWS[i].type;
    p[3] = ROWS[i].sub;
    wrLe32(p + 4, ROWS[i].off);
    wrLe32(p + 8, ROWS[i].size);
    memset(p + 12, 0, 20);
    memcpy(p + 12, ROWS[i].label, strlen(ROWS[i].label));
  }
  uint8_t* m = sec + NROWS * 32;
  md5_context_t c;
  esp_rom_md5_init(&c);
  esp_rom_md5_update(&c, sec, NROWS * 32);
  wrLe16(m, 0xEBEB);
  esp_rom_md5_final(m + 16, &c);
}

uint8_t slot() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p && p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? 1 : 0;
}

static const esp_partition_t* app(esp_partition_subtype_t s) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, s, NULL);
}

// by app1 this boot, the table write checks the whole old table first
Kind kind() {
  const esp_partition_t* p = app(ESP_PARTITION_SUBTYPE_APP_OTA_1);
  if (p && p->address == OLD_APP1_AT && p->size == OLD_SLOT) return OLD;
  if (p && p->address == ROWS[APP1].off && p->size == ROWS[APP1].size) return NEW;
  return UNKNOWN;
}

// app1 hands over to app0 only for the same build, the image is checked when it's set
static bool app0Same() {
  esp_app_desc_t d;
  const esp_partition_t* a = app(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  return a && esp_ota_get_partition_description(a, &d) == ESP_OK &&
         !memcmp(d.app_elf_sha256, esp_ota_get_app_description()->app_elf_sha256, sizeof(d.app_elf_sha256));
}

struct Nvs {
  nvs_handle_t h = 0;
  bool ok;
  explicit Nvs(nvs_open_mode_t m) : ok(nvs_open(NS, m, &h) == ESP_OK) {}
  ~Nvs() { if (ok) nvs_close(h); }
};

// an unknown number reads as nothing to do
Step step() {
  Nvs n(NVS_READONLY);
  uint8_t s = IDLE;
  if (!n.ok || nvs_get_u8(n.h, K_STEP, &s) != ESP_OK || s > CLEARING) return IDLE;
  return (Step)s;
}

CORIUM_NODISCARD static bool setStep(Step s) {
  Nvs n(NVS_READWRITE);
  return n.ok && nvs_set_u8(n.h, K_STEP, s) == ESP_OK && nvs_commit(n.h) == ESP_OK;
}

static Step quit(const char* why) {
  Log::console(why);
  (void)setStep(IDLE);   // a failed clear tries again next boot
  return IDLE;
}

static void restart() {
  DeviceInfo::safeRestart(500);
}

typedef const char* (*Visit)(File& f, void* ctx);

// every file, depth first, nullptr = all seen
static const char* walk(Visit fn, void* ctx) {
  File dirs[DEPTH];
  uint8_t d = 0;
  dirs[0] = Fs::openR("/");
  if (!dirs[0] || !dirs[0].isDirectory()) return R_FS;
  const char* why = nullptr;
  while (!why) {
    File f = dirs[d].openNextFile();
    if (!f) {
      dirs[d].close();
      if (!d) break;
      d--;
    } else if (!f.isDirectory()) {
      why = fn(f, ctx);
    } else if (d + 1 < DEPTH) {
      dirs[++d] = f;
    } else {
      why = R_BIG;
    }
  }
  return why;
}

// the record as it goes out; no partition = a size pass
struct Rec {
  const esp_partition_t* p;
  uint32_t off;
  uint32_t at;
  uint32_t crc;
  uint16_t count;
  bool ok;
};

static void add(Rec& r, const void* b, size_t n) {
  if (r.p) r.ok = r.ok && esp_partition_write(r.p, r.off + r.at, b, n) == ESP_OK;
  r.crc = esp_rom_crc32_le(r.crc, (const uint8_t*)b, n);
  r.at += n;
}

// file to flash, flash to file, flash against file, flash to crc only
enum Pump : uint8_t { OUT, IN, SAME, SUM };

static bool pump(Pump m, File* f, const esp_partition_t* p, uint32_t at, uint32_t n, uint32_t* crc) {
  uint8_t a[CHUNK], b[CHUNK];
  bool ok = true;
  for (uint32_t done = 0; ok && done < n; ) {
    size_t k = n - done < CHUNK ? n - done : CHUNK;
    ok = m == OUT ? f->read(a, k) == k && esp_partition_write(p, at + done, a, k) == ESP_OK
                  : esp_partition_read(p, at + done, a, k) == ESP_OK;
    if (m == IN) ok = ok && f->write(a, k) == k;
    if (m == SAME) ok = ok && f->read(b, k) == k && !memcmp(a, b, k);
    *crc = esp_rom_crc32_le(*crc, a, k);
    done += k;
  }
  return ok;
}

static const char* rec(File& f, void* ctx) {
  Rec& r = *(Rec*)ctx;
  const char* path = f.path();
  uint32_t plen = strlen(path);
  uint32_t size = f.size();
  if (plen >= PATH_CAP || r.at + 1 + plen + 4 + size + 4 > BACKUP_MAX) return R_BIG;
  uint8_t h[4] = {(uint8_t)plen};
  add(r, h, 1);
  add(r, path, plen);
  wrLe32(h, size);
  add(r, h, 4);
  r.count++;
  if (r.p && !pump(OUT, &f, r.p, r.off + r.at, size, &r.crc)) r.ok = false;
  r.at += size;
  return nullptr;
}

struct Back {
  const esp_partition_t* p;
  uint32_t off;
  uint32_t end;
  uint16_t count;
};

static bool readAt(const Back& k, uint32_t at, void* b, size_t n) {
  return at + n <= k.end && esp_partition_read(k.p, k.off + at, b, n) == ESP_OK;
}

// header and crc, as the restore will see them
static bool readRec(Back& k) {
  uint8_t h[HEAD];
  uint32_t crc = 0;
  k.end = HEAD;
  bool ok = readAt(k, 0, h, HEAD) && rdLe32(h) == MAGIC && rdLe16(h + 4) == VERSION &&
            rdLe32(h + 8) <= BACKUP_MAX - HEAD - 4;
  k.count = rdLe16(h + 6);
  k.end = HEAD + rdLe32(h + 8);
  return ok && esp_partition_read(k.p, k.off + k.end, h, 4) == ESP_OK &&
         pump(SUM, nullptr, k.p, k.off, k.end, &crc) && crc == rdLe32(h);
}

static Back app1() {
  Back k = {app(ESP_PARTITION_SUBTYPE_APP_OTA_1), 0, 0, 0};
  if (k.p) k.off = BACKUP_AT - k.p->address;
  return k;
}

// one record into the old app1, read back as the restore reads it
static const char* backup() {
  Back k = app1();
  if (!k.p) return R_FLASH;
  Rec r = {nullptr, 0, HEAD, 0, 0, true};
  const char* why = walk(rec, &r);
  if (why) return why;
  uint32_t end = r.at;
  uint16_t count = r.count;
  if (esp_partition_erase_range(k.p, k.off, (end + 4 + SECTOR - 1) & ~(SECTOR - 1)) != ESP_OK) return R_FLASH;
  uint8_t h[HEAD];
  wrLe32(h, MAGIC);
  wrLe16(h + 4, VERSION);
  wrLe16(h + 6, count);
  wrLe32(h + 8, end - HEAD);
  r = {k.p, k.off, 0, 0, 0, true};
  add(r, h, HEAD);
  why = walk(rec, &r);
  if (why) return why;
  wrLe32(h, r.crc);
  add(r, h, 4);
  return r.ok && r.at == end + 4 && r.count == count && readRec(k) ? nullptr : R_FLASH;
}

static void mkdirs(const char* path) {
  char p[PATH_CAP];
  strlcpy(p, path, sizeof(p));
  for (char* s = strchr(p + 1, '/'); s; s = strchr(s + 1, '/')) {
    *s = '\0';
    (void)Fs::mkdir(p);   // false when it's there
    *s = '/';
  }
}

// put back then read back, close() can drop the data
static bool file(const char* path, const esp_partition_t* p, uint32_t at, uint32_t n) {
  uint32_t crc = 0;
  mkdirs(path);
  File f = Fs::openW(path);
  bool ok = f && pump(IN, &f, p, at, n, &crc);
  f.close();
  f = Fs::openR(path);
  return ok && f && f.size() == n && pump(SAME, &f, p, at, n, &crc);
}

// every entry in bounds and the last one ending the record, before anything is put back
static const char* scan(const Back& k, bool put) {
  uint32_t at = HEAD;
  for (uint16_t i = 0; i < k.count; i++) {
    uint8_t b[4];
    char path[PATH_CAP];
    bool ok = readAt(k, at, b, 1);
    uint32_t plen = b[0];
    ok = ok && plen && plen < PATH_CAP && readAt(k, at + 1, path, plen) && readAt(k, at + 1 + plen, b, 4);
    uint32_t n = rdLe32(b);
    at += 1 + plen + 4;
    ok = ok && path[0] == '/' && !memchr(path, 0, plen) && n <= k.end - at;
    if (!ok) return R_BACKUP;
    path[plen] = '\0';
    if (put && !file(path, k.p, k.off + at, n)) return R_FS;
    at += n;
  }
  return at == k.end ? nullptr : R_BACKUP;
}

static const char* putBack() {
  Back k = app1();
  const char* why = k.p && readRec(k) ? scan(k, false) : R_BACKUP;
  if (why) return why;
  if (!Fs::format()) return R_FS;
  why = scan(k, true);
  Fs::unmount();   // Boot::prefs mounts it again, wear count and all
  return why;
}

// The default chip's os_func with no region check, for the table write only. Static, a task that read the
// pointer late still finds it. The check runs before the op lock, so the swap sits inside that lock
static esp_flash_os_functions_t s_open;

CORIUM_NODISCARD static bool putCheck(uint32_t at, const uint8_t* buf, uint8_t* chk) {
  esp_flash_t* c = esp_flash_default_chip;
  const spi_flash_guard_funcs_t* g = spi_flash_guard_get();
  const esp_flash_os_functions_t* os = c->os_func;
  s_open = *os;
  s_open.region_protected = NULL;
  g->op_lock();
  c->os_func = &s_open;
  bool ok = esp_flash_erase_region(c, at, SECTOR) == ESP_OK && esp_flash_write(c, buf, at, SECTOR) == ESP_OK;
  c->os_func = os;
  g->op_unlock();
  return ok && esp_flash_read(c, chk, at, SECTOR) == ESP_OK && !memcmp(buf, chk, SECTOR);
}

// bufs: the old sector, the new, a read back. A practice write on the coredump first
static const char* swap(uint8_t* old, uint8_t* neu, uint8_t* chk) {
  uint8_t d[16];
  md5_context_t c;
  bool ok = esp_flash_read(NULL, old, TABLE_AT, SECTOR) == ESP_OK;
  esp_rom_md5_init(&c);
  esp_rom_md5_update(&c, old, TABLE_LEN);
  esp_rom_md5_final(d, &c);
  if (!ok || memcmp(d, OLD_MD5, 16)) return R_TABLE;
  build(neu);
  const esp_partition_t* cd = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
  ok = cd && putCheck(cd->address, neu, chk);
  if (cd && esp_partition_erase_range(cd, 0, SECTOR) != ESP_OK) ok = false;
  if (!ok) return R_FLASH;
  if (putCheck(TABLE_AT, neu, chk)) return nullptr;
  return putCheck(TABLE_AT, old, chk) ? R_FLASH : R_STUCK;
}

// internal ram, the writes run with the caches off
static const char* writeTable() {
  uint8_t* m = (uint8_t*)heap_caps_malloc(3 * SECTOR, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!m) return R_FLASH;
  const char* why = swap(m, m + SECTOR, m + 2 * SECTOR);
  heap_caps_free(m);
  return why;
}

static Step save(Kind k) {
  if (k != OLD) return quit(R_TABLE);
  if (slot()) {
    if (!app0Same() || esp_ota_set_boot_partition(app(ESP_PARTITION_SUBTYPE_APP_OTA_0)) != ESP_OK) return quit(R_APP0);
    restart();
    return IDLE;
  }
  const char* why = backup();
  if (why) return quit(why);
  return setStep(SAVED) ? SAVED : quit(R_FLASH);
}

// a stuck table keeps the step, there's nothing to go back to
static Step flip(Kind k) {
  if (k == NEW) return setStep(RESTORING) ? RESTORING : IDLE;
  if (k != OLD) return quit(R_TABLE);
  const char* why = writeTable();
  if (why == R_STUCK) Log::console(why);
  if (why) return why == R_STUCK ? IDLE : quit(why);
  restart();
  return IDLE;
}

// a bad record or put back keeps the step, the next boot tries again
static Step restore(Kind k) {
  const char* why = k == NEW ? putBack() : R_TABLE;
  if (why) {
    Log::console(why);
    return IDLE;
  }
  return setStep(CLEARING) ? CLEARING : IDLE;
}

static void clearOut() {
  Back k = app1();
  if (k.p && esp_partition_erase_range(k.p, k.off, SECTOR) == ESP_OK && setStep(IDLE)) {
    Log::console(PSTR("Layout: done"));
  }
}

void resume() {
  Step s = step();
  if (s == IDLE) return;
  Kind k = kind();
  if (s == SAVING) s = save(k);
  if (s == SAVED) s = flip(k);
  if (s == RESTORING) s = restore(k);
  if (s == CLEARING) clearOut();
  otaHeld = step() != IDLE;   // app1 holds the files
}

const char* check() {
  if (step() != IDLE) return R_BUSY;
  if (kind() != OLD) return R_TABLE;
  if (slot() && !app0Same()) return R_APP0;
  Rec r = {nullptr, 0, HEAD, 0, 0, true};
  return walk(rec, &r);
}

const char* start() {
  const char* why = check();
  if (why) return why;
  if (!setStep(SAVING)) return R_FLASH;
  Log::console(PSTR("Layout: moving"));
  restart();
  return nullptr;
}

}  // namespace Layout
}  // namespace corium

// prototype: layout shows it, layout migrate runs it
static void cmd_layout() {
  using namespace corium;
  char* arg = Console::next();
  if (!arg) {
    Log::console(PSTR("Layout: table %u app%u step %u"), Layout::kind(), Layout::slot(), Layout::step());
    return;
  }
  const char* why = strcmp_P(arg, PSTR("migrate")) ? PSTR("layout [migrate]") : Layout::start();
  if (why) Log::console(why);
}
CORIUM_REGISTER_CMD(layout, cmd_layout)
#endif
