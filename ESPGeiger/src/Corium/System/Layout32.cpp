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
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Util/Bytes.h"
#include <stdlib.h>
#include <string.h>
#include <sdkconfig.h>
#include <nvs.h>
#include <esp_attr.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#if CONFIG_IDF_TARGET_ESP32C3
#include <esp32c3/rom/spi_flash.h>
#elif CONFIG_IDF_TARGET_ESP32S2
#include <esp32s2/rom/spi_flash.h>
#elif CONFIG_IDF_TARGET_ESP32S3
#include <esp32s3/rom/spi_flash.h>
#else
#include <esp32/rom/spi_flash.h>
#endif

// bootloader_support's, only writes the status when a protect bit is set
extern "C" esp_err_t bootloader_flash_unlock(void);

namespace corium {
namespace Layout {

// default.csv as 2.0.17 has it
const Row OLD_ROWS[ROWS] = {
  {1, 0x02, 0x9000,   0x5000,   "nvs"},
  {1, 0x00, 0xe000,   0x2000,   "otadata"},
  {0, 0x10, 0x10000,  0x140000, "app0"},
  {0, 0x11, 0x150000, 0x140000, "app1"},
  {1, 0x82, 0x290000, 0x160000, "spiffs"},
  {1, 0x03, 0x3F0000, 0x10000,  "coredump"},
};

// min_spiffs.csv
const Row NEW_ROWS[ROWS] = {
  {1, 0x02, 0x9000,   0x5000,   "nvs"},
  {1, 0x00, 0xe000,   0x2000,   "otadata"},
  {0, 0x10, 0x10000,  0x1E0000, "app0"},
  {0, 0x11, 0x1F0000, 0x1E0000, "app1"},
  {1, 0x82, 0x3D0000, 0x20000,  "spiffs"},
  {1, 0x03, 0x3F0000, 0x10000,  "coredump"},
};

// gen_esp32part.py's md5 rows for the two
const uint8_t OLD_MD5[16] = {0x97, 0x2d, 0xae, 0x2f, 0xf8, 0x72, 0xa0, 0x14,
                             0x2d, 0x60, 0xba, 0xd1, 0x24, 0xc0, 0x66, 0x6b};
const uint8_t NEW_MD5[16] = {0xb4, 0x35, 0x13, 0xb1, 0x22, 0xc4, 0x18, 0x73,
                             0xa9, 0x01, 0x0b, 0x82, 0xec, 0x3a, 0x25, 0xbd};

static const char NS[] = "layout";
static const char K_STEP[] = "st";
static const char K_COUNT[] = "n";
static const uint16_t MAX_FILES = 64;
static const size_t PATH_CAP = 64;
static const size_t FILE_MAX = 16000;   // nvs's biggest blob on 5 pages
static const uint32_t RESERVE = 126;    // nvs keeps a page free
static const uint8_t DEPTH = 4;
static const uint32_t RESTART_MS = 500;

static const char STUCK[] PROGMEM = "Layout: TABLE BAD, needs a USB install";

static bool same(const Row& a, const Row& b) {
  return a.type == b.type && a.sub == b.sub && a.off == b.off && a.size == b.size &&
         !strncmp(a.label, b.label, sizeof(a.label));
}

static bool all(const Row* rows, size_t n, const Row* want) {
  if (n != ROWS) return false;
  for (uint8_t i = 0; i < ROWS; i++) {
    if (!same(rows[i], want[i])) return false;
  }
  return true;
}

Kind classify(const Row* rows, size_t n) {
  if (all(rows, n, NEW_ROWS)) return NEW;
  if (all(rows, n, OLD_ROWS)) return OLD;
  return UNKNOWN;
}

void build(const Row* rows, const uint8_t* md5, uint8_t* sec) {
  memset(sec, 0xFF, SECTOR);
  for (uint8_t i = 0; i < ROWS; i++) {
    uint8_t* p = sec + i * 32;
    wrLe16(p, 0x50AA);
    p[2] = rows[i].type;
    p[3] = rows[i].sub;
    wrLe32(p + 4, rows[i].off);
    wrLe32(p + 8, rows[i].size);
    memset(p + 12, 0, 16);
    memcpy(p + 12, rows[i].label, strnlen(rows[i].label, 16));
    wrLe32(p + 28, 0);
  }
  uint8_t* m = sec + ROWS * 32;
  wrLe16(m, 0xEBEB);
  memcpy(m + 16, md5, 16);
}

Kind kind() {
  Row rows[ROWS];
  size_t n = 0;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  for (; it; it = esp_partition_next(it), n++) {
    if (n == ROWS) {
      esp_partition_iterator_release(it);
      return UNKNOWN;   // more rows than either
    }
    const esp_partition_t* p = esp_partition_get(it);
    rows[n] = {(uint8_t)p->type, (uint8_t)p->subtype, p->address, p->size, {}};
    strlcpy(rows[n].label, p->label, sizeof(rows[n].label));
  }
  return classify(rows, n);
}

uint8_t slot() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p && p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? 1 : 0;
}

static const esp_partition_t* app0() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
}

// app1 hands over to app0 only for the same build, the image is checked when it's set
static const char* app0Why() {
  esp_app_desc_t d;
  const esp_partition_t* a = app0();
  if (!a || esp_ota_get_partition_description(a, &d) != ESP_OK) {
    return PSTR("Layout: Running app1, app0 holds no app, update once more");
  }
  if (memcmp(d.app_elf_sha256, esp_ota_get_app_description()->app_elf_sha256, sizeof(d.app_elf_sha256))) {
    return PSTR("Layout: Running app1, app0 holds another build, update once more");
  }
  return nullptr;
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

static void key(char* k, size_t cap, uint16_t i) {
  snprintf_P(k, cap, PSTR("f%u"), (unsigned)i);
}

static bool gone(nvs_handle_t h, const char* k) {
  esp_err_t e = nvs_erase_key(h, k);
  return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
}

// every slot, so a backup cut short leaves nothing behind
static bool drop(nvs_handle_t h) {
  char k[8];
  bool ok = true;
  for (uint16_t i = 0; i < MAX_FILES; i++) {
    key(k, sizeof(k), i);
    ok = gone(h, k) && ok;
  }
  return ok && gone(h, K_COUNT);
}

// the step goes last, a cut before it clears again
static bool clear() {
  Nvs n(NVS_READWRITE);
  return n.ok && drop(n.h) && gone(n.h, K_STEP) && nvs_commit(n.h) == ESP_OK;
}

static Step quit(const char* why) {
  Log::console(why);
  if (!clear()) Log::console(PSTR("Layout: Backup not cleared"));
  return IDLE;
}

static void restart() {
  DeviceInfo::safeRestart(RESTART_MS);
}

typedef const char* (*Visit)(File& f, void* ctx);

// every file, depth first, nullptr = all seen
static const char* walk(Visit fn, void* ctx) {
  File dirs[DEPTH];
  uint8_t d = 0;
  dirs[0] = Fs::openR("/");
  if (!dirs[0] || !dirs[0].isDirectory()) return PSTR("Layout: No fs");
  const char* why = nullptr;
  for (uint16_t seen = 0; !why; ) {
    File f = dirs[d].openNextFile();
    if (!f) {
      dirs[d].close();
      if (!d) break;
      d--;
    } else if (++seen > MAX_FILES) {
      why = PSTR("Layout: Too many files");
    } else if (!f.isDirectory()) {
      why = fn(f, ctx);
    } else if (d + 1 < DEPTH) {
      dirs[++d] = f;
    } else {
      why = PSTR("Layout: Dirs too deep");
    }
  }
  return why;
}

static uint32_t entries(size_t len) {
  return (uint32_t)((len + 31) / 32) + 3;   // data, chunk header, blob index, a split
}

static const char* sized(File& f, void* ctx) {
  Fit* n = (Fit*)ctx;
  size_t plen = strlen(f.path());
  if (plen >= PATH_CAP) return PSTR("Layout: Path too long");
  if (f.size() > FILE_MAX) return PSTR("Layout: File too big");
  n->files++;
  n->bytes += f.size();
  n->need += entries(1 + plen + f.size());
  return nullptr;
}

const char* fit(Fit* f) {
  *f = {};
  f->need = 2;   // step and count
  const char* why = walk(sized, f);
  if (why) return why;
  nvs_stats_t s;
  if (nvs_get_stats(NULL, &s) != ESP_OK) return PSTR("Layout: No nvs stats");
  f->free = s.free_entries;
  if (f->need + RESERVE > f->free) return PSTR("Layout: Files don't fit in nvs");
  return nullptr;
}

static bool readBack(nvs_handle_t h, const char* k, const uint8_t* b, size_t len) {
  uint8_t* c = (uint8_t*)malloc(len);
  size_t n = len;
  bool ok = c && nvs_get_blob(h, k, c, &n) == ESP_OK && n == len && !memcmp(b, c, len);
  free(c);
  return ok;
}

struct Save {
  nvs_handle_t h;
  uint16_t i;
};

// path length, path, data
static const char* saved(File& f, void* ctx) {
  Save* s = (Save*)ctx;
  size_t plen = strlen(f.path());
  size_t size = f.size();
  size_t len = 1 + plen + size;
  uint8_t* b = (uint8_t*)malloc(len);
  if (!b) return PSTR("Layout: No memory");
  b[0] = (uint8_t)plen;
  memcpy(b + 1, f.path(), plen);
  char k[8];
  key(k, sizeof(k), s->i);
  bool ok = f.read(b + 1 + plen, size) == size && nvs_set_blob(s->h, k, b, len) == ESP_OK &&
            readBack(s->h, k, b, len);
  free(b);
  if (!ok) return PSTR("Layout: Backup write failed");
  s->i++;
  return nullptr;
}

static const char* backup() {
  Nvs n(NVS_READWRITE);
  if (!n.ok || !drop(n.h)) return PSTR("Layout: No nvs");
  Fit f;
  const char* why = fit(&f);
  if (why) return why;
  Save s = {n.h, 0};
  why = walk(saved, &s);
  if (why) return why;
  if (s.i != f.files) return PSTR("Layout: Files changed under the backup");
  if (nvs_set_u16(n.h, K_COUNT, s.i) != ESP_OK || nvs_commit(n.h) != ESP_OK) return PSTR("Layout: No nvs");
  Log::console(PSTR("Layout: %u files in nvs"), (unsigned)s.i);
  return nullptr;
}

typedef const char* (*Each)(const char* path, const uint8_t* data, size_t size);

// all of a blob parsed before it's used
static const char* each(nvs_handle_t h, uint16_t i, Each fn) {
  char k[8];
  key(k, sizeof(k), i);
  size_t len = 0;
  if (nvs_get_blob(h, k, NULL, &len) != ESP_OK || len < 2 || len > 1 + PATH_CAP + FILE_MAX) {
    return PSTR("Layout: Backup damaged");
  }
  uint8_t* b = (uint8_t*)malloc(len);
  if (!b) return PSTR("Layout: No memory");
  size_t plen = 0;
  bool ok = nvs_get_blob(h, k, b, &len) == ESP_OK;
  if (ok) plen = b[0];
  ok = ok && plen && plen < PATH_CAP && 1 + plen <= len && b[1] == '/' && !memchr(b + 1, 0, plen);
  char path[PATH_CAP];
  if (ok) {
    memcpy(path, b + 1, plen);
    path[plen] = '\0';
  }
  const char* why = ok ? fn(path, b + 1 + plen, len - 1 - plen) : PSTR("Layout: Backup damaged");
  free(b);
  return why;
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

static const char* put(const char* path, const uint8_t* data, size_t size) {
  mkdirs(path);
  File f = Fs::openW(path);
  if (!f) return PSTR("Layout: Can't write a file back");
  size_t n = size ? f.write(data, size) : 0;
  f.close();
  return n == size ? nullptr : PSTR("Layout: Can't write a file back");
}

// close() can drop the data, so each file is read back
static const char* match(const char* path, const uint8_t* data, size_t size) {
  File f = Fs::openR(path);
  if (!f || f.isDirectory() || f.size() != size) return PSTR("Layout: A file came back wrong");
  uint8_t b[64];
  for (size_t at = 0; at < size; ) {
    size_t want = size - at < sizeof(b) ? size - at : sizeof(b);
    if (f.read(b, want) != want || memcmp(b, data + at, want)) return PSTR("Layout: A file came back wrong");
    at += want;
  }
  return nullptr;
}

static const char* putBack() {
  Nvs n(NVS_READONLY);
  uint16_t count = 0;
  if (!n.ok || nvs_get_u16(n.h, K_COUNT, &count) != ESP_OK || count > MAX_FILES) {
    return PSTR("Layout: Backup count gone");
  }
  if (!Fs::format()) return PSTR("Layout: Format failed");
  const char* why = nullptr;
  for (uint16_t i = 0; i < count && !why; i++) why = each(n.h, i, put);
  for (uint16_t i = 0; i < count && !why; i++) why = each(n.h, i, match);
  Fs::unmount();   // Boot::prefs mounts it again, wear count and all
  if (!why) Log::console(PSTR("Layout: %u files back"), (unsigned)count);
  return why;
}

// caches off around each rom call, as the idf's own legacy erase does. Inlined it lands in flash
static IRAM_ATTR NOINLINE_ATTR bool romPut(uint32_t at, const uint32_t* buf) {
  const spi_flash_guard_funcs_t* g = spi_flash_guard_get();
  g->start();
  bool ok = bootloader_flash_unlock() == ESP_OK;
  g->end();
  g->start();
  ok = ok && esp_rom_spiflash_erase_sector(at / SECTOR) == ESP_ROM_SPIFLASH_RESULT_OK;
  g->end();
  g->start();
  ok = ok && esp_rom_spiflash_write(at, buf, SECTOR) == ESP_ROM_SPIFLASH_RESULT_OK;
  esp_rom_spiflash_write_disable();
  g->end();
  return ok;
}

CORIUM_NODISCARD static bool putCheck(uint32_t at, const uint8_t* buf, uint8_t* chk) {
  return romPut(at, (const uint32_t*)buf) && esp_flash_read(NULL, chk, at, SECTOR) == ESP_OK &&
         !memcmp(buf, chk, SECTOR);
}

// a practice write on the coredump first, a bad one there costs nothing
static const char* practice(const uint8_t* neu, uint8_t* chk) {
  const esp_partition_t* cd = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
  if (!cd) return PSTR("Layout: No coredump to practice on");
  bool ok = putCheck(cd->address, neu, chk);
  if (esp_partition_erase_range(cd, 0, SECTOR) != ESP_OK) ok = false;
  return ok ? nullptr : PSTR("Layout: Practice write failed, left alone");
}

// bufs: the old sector, the new, a read back
static const char* swap(uint8_t* old, uint8_t* neu, uint8_t* chk) {
  build(OLD_ROWS, OLD_MD5, chk);
  if (esp_flash_read(NULL, old, TABLE_AT, SECTOR) != ESP_OK || memcmp(old, chk, TABLE_LEN)) {
    return PSTR("Layout: Table bytes aren't default.csv's, left alone");
  }
  build(NEW_ROWS, NEW_MD5, neu);
  const char* why = practice(neu, chk);
  if (why) return why;
  if (putCheck(TABLE_AT, neu, chk)) return nullptr;
  if (putCheck(TABLE_AT, old, chk)) return PSTR("Layout: New table didn't read back, old one back");
  return STUCK;
}

// internal ram, the rom calls run with the caches off
static const char* writeTable() {
  uint8_t* m = (uint8_t*)heap_caps_malloc(3 * SECTOR, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!m) return PSTR("Layout: No memory");
  const char* why = swap(m, m + SECTOR, m + 2 * SECTOR);
  heap_caps_free(m);
  return why;
}

static Step toApp0() {
  const char* why = app0Why();
  if (why) return quit(why);
  if (esp_ota_set_boot_partition(app0()) != ESP_OK) return quit(PSTR("Layout: app0 didn't verify"));
  Log::console(PSTR("Layout: Restarting into app0"));
  restart();
  return IDLE;
}

static Step save(Kind k) {
  if (k != OLD) return quit(PSTR("Layout: Not the old table, left alone"));
  if (slot()) return toApp0();
  const char* why = backup();
  if (why) return quit(why);
  return setStep(SAVED) ? SAVED : quit(PSTR("Layout: No nvs"));
}

// a stuck table keeps the backup, there's nothing to go back to
static Step flip(Kind k) {
  if (k == NEW) return setStep(RESTORING) ? RESTORING : IDLE;
  if (k != OLD) return quit(PSTR("Layout: Not the old table, left alone"));
  const char* why = writeTable();
  if (why == STUCK) Log::console(why);
  if (why) return why == STUCK ? IDLE : quit(why);
  Log::console(PSTR("Layout: New table written, restarting"));
  restart();
  return IDLE;
}

// a failed put back keeps the backup, the next boot tries again
static Step restore(Kind k) {
  if (k != NEW) {
    Log::console(PSTR("Layout: Files wait for the new table"));
    return IDLE;
  }
  const char* why = putBack();
  if (why) {
    Log::console(why);
    return IDLE;
  }
  return setStep(CLEARING) ? CLEARING : IDLE;
}

void resume() {
  Step s = step();
  if (s == IDLE) return;
  Kind k = kind();
  if (s == SAVING) s = save(k);
  if (s == SAVED) s = flip(k);
  if (s == RESTORING) s = restore(k);
  if (s == CLEARING && clear()) Log::console(PSTR("Layout: Moved to the new table"));
}

const char* check() {
  if (step() != IDLE) return PSTR("Layout: Already moving");
  Kind k = kind();
  if (k == NEW) return PSTR("Layout: Already the new table");
  if (k != OLD) return PSTR("Layout: Not a table this knows, left alone");
  const char* why = slot() ? app0Why() : nullptr;
  if (why) return why;
  Fit f;
  return fit(&f);
}

const char* start() {
  const char* why = check();
  if (why) return why;
  if (!setStep(SAVING)) return PSTR("Layout: No nvs");
  Log::console(PSTR("Layout: Moving, a few restarts"));
  restart();
  return nullptr;
}

}  // namespace Layout
}  // namespace corium

// prototype: layout shows it, layout migrate runs it
static void cmd_layout() {
  using namespace corium;
  char* arg = Console::next();
  if (arg && !strcmp_P(arg, PSTR("migrate"))) {
    const char* why = Layout::start();
    if (why) Log::console(why);
    return;
  }
  if (arg) {
    Log::console(PSTR("Usage: layout [migrate]"));
    return;
  }
  static const char* const KINDS[] = {"unknown", "old", "new"};
  Layout::Fit f;
  const char* why = Layout::fit(&f);
  Log::console(PSTR("Layout: %s, app%u, step %u"), KINDS[Layout::kind()], Layout::slot(), Layout::step());
  Log::console(PSTR("Layout: %u files %u B, nvs %u of %u entries"), f.files, (unsigned)f.bytes,
               (unsigned)f.need, (unsigned)f.free);
  if (why) Log::console(why);
}
CORIUM_REGISTER_CMD(layout, cmd_layout)
#endif
