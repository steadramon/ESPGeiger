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
#include "BootGuard.h"
#include "CrashDump.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/PrefsStorage.h"
#include "Corium/WebPortal/WebPortal.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Util/Bytes.h"
#include <string.h>
#include <sdkconfig.h>
#include <nvs.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_littlefs.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <esp_rom_md5.h>
#include <esp_spi_flash.h>
#include <vfs_api.h>

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
static constexpr Row ROWS[] = {
  {1, 0x02, 0x9000,   0x5000,   "nvs"},
  {1, 0x00, 0xe000,   0x2000,   "otadata"},
  {0, 0x10, 0x10000,  0x1E0000, "app0"},
  {0, 0x11, 0x1F0000, 0x1E0000, "app1"},
  {1, 0x82, 0x3D0000, 0x20000,  "spiffs"},
  {1, 0x03, 0x3F0000, 0x10000,  "coredump"},
};
static const uint8_t NROWS = sizeof(ROWS) / sizeof(ROWS[0]);
static constexpr uint8_t APP0 = 2;
static constexpr uint8_t APP1 = 3;
static constexpr uint8_t SPIFFS = 4;
static_assert(ROWS[APP0].off == 0x10000, "app0 stays where the bootloader runs it");
static_assert(ROWS[APP1].off <= BACKUP_AT && BACKUP_AT + BACKUP_MAX <= ROWS[APP1].off + ROWS[APP1].size,
              "the backup sits in the new app1");

// A table this moves from: md5 of its first 0xC00 bytes as gen_esp32part.py makes them, its app slots, its fs
struct From {
  uint8_t md5[16];
  uint32_t app0;
  uint32_t app0Size;
  uint32_t app1;
  uint32_t app1Size;
  uint32_t fs;
  uint32_t fsSize;
};

static const From FROM[] = {
  // default.csv
  {{0xa0, 0x39, 0xc6, 0x6c, 0xd3, 0x48, 0x81, 0x76, 0x03, 0x7b, 0x61, 0x6b, 0x75, 0x95, 0xfe, 0x72},
   0x10000, 0x140000, 0x150000, 0x140000, 0x290000, 0x160000},
#ifdef CORIUM_LAYOUT_FROM_MORE
  CORIUM_LAYOUT_FROM_MORE
#endif
};

static const char NS[] = "layout";
static const char K_STEP[] = "st";
// record: magic, version, count, length, then per file path length, path, data length, data, then crc32
static const uint32_t MAGIC = 0x3159414C;   // "LAY1"
static const uint16_t VERSION = 1;
static const uint32_t HEAD = 12;
static const size_t PATH_CAP = 64;
static const uint8_t DEPTH = 4;
static const size_t CHUNK = 256;
// the new fs in lfs blocks: a file up to the cache size sits in its directory, a directory takes 2 blocks
static const uint32_t FS_BLOCK = 4096;
static const uint32_t FS_BLOCKS = ROWS[SPIFFS].size / FS_BLOCK;
static const uint32_t INLINE = CONFIG_LITTLEFS_CACHE_SIZE;
static const uint32_t DIR_BYTES = 2048;   // inline bytes a directory's 2 blocks hold, low
static const uint32_t SPARE = 4;          // lfs writes copies
static const uint8_t CRASHES = 2;
static const char OLD_PATH[] = "/old";

static const char R_BUSY[] PROGMEM = "Layout: busy";
static const char R_TABLE[] PROGMEM = "Layout: not a known table";
static const char R_APP0[] PROGMEM = "Layout: app0 copy failed";
static const char R_BIG[] PROGMEM = "Layout: files too big";
static const char R_FLASH[] PROGMEM = "Layout: flash write failed";
static const char R_FS[] PROGMEM = "Layout: fs failed";
static const char R_BACKUP[] PROGMEM = "Layout: backup bad";
static const char R_STUCK[] PROGMEM = "Layout: TABLE BAD, USB install";
static const char R_CRASH[] PROGMEM = "Layout: crashing, stopped";

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

// app0 where it is in the new table and no bigger, the backup inside its app1
static bool fits(const From& f) {
  return f.app0 == ROWS[APP0].off && f.app0Size <= ROWS[APP0].size && f.app1 <= BACKUP_AT &&
         BACKUP_AT + BACKUP_MAX <= f.app1 + f.app1Size;
}

static bool at(const esp_partition_t* p, uint32_t off, uint32_t size) {
  return p && p->address == off && p->size == size;
}

// the entry this boot's app slots match, nullptr = none or one that doesn't fit
static const From* from() {
  const esp_partition_t* a0 = app(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  const esp_partition_t* a1 = app(ESP_PARTITION_SUBTYPE_APP_OTA_1);
  for (const From& f : FROM) {
    if (at(a0, f.app0, f.app0Size) && at(a1, f.app1, f.app1Size)) return fits(f) ? &f : nullptr;
  }
  return nullptr;
}

static const esp_partition_t* coredump() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
}

// the 0xC00 bytes at 0x8000 as they are in flash now
static bool md5Is(const uint8_t* want) {
  uint8_t b[CHUNK], d[16];
  md5_context_t c;
  esp_rom_md5_init(&c);
  for (uint32_t at = 0; at < TABLE_LEN; at += CHUNK) {
    if (esp_flash_read(NULL, b, TABLE_AT + at, CHUNK) != ESP_OK) return false;
    esp_rom_md5_update(&c, b, CHUNK);
  }
  esp_rom_md5_final(d, &c);
  return !memcmp(d, want, 16);
}

// old = an entry's slots, its bytes and a coredump to practice on; new = by the app slots
Kind kind() {
  if (at(app(ESP_PARTITION_SUBTYPE_APP_OTA_1), ROWS[APP1].off, ROWS[APP1].size)) return NEW;
  const From* f = from();
  return f && coredump() && md5Is(f->md5) ? OLD : UNKNOWN;
}

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

// every file and directory, depth first, nullptr = all seen. No src = Fs
static const char* walk(fs::FS* src, Visit fn, void* ctx) {
  File dirs[DEPTH];
  uint8_t d = 0;
  dirs[0] = src ? src->open("/") : Fs::openR("/");
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
      why = fn(f, ctx);
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
  uint32_t blocks;   // on the new fs, root and inline files apart
  uint32_t inl;
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

static bool staged(const char* path, size_t plen) {
  const size_t n = sizeof(CORIUM_PS_STAGING_SUFFIX) - 1;
  return plen >= n && !strcmp(path + plen - n, CORIUM_PS_STAGING_SUFFIX);
}

// a staged write left by a cut is never read back, so it stays behind
static const char* rec(File& f, void* ctx) {
  Rec& r = *(Rec*)ctx;
  const char* path = f.path();
  uint32_t plen = strlen(path);
  if (f.isDirectory() || staged(path, plen)) {
    r.blocks += f.isDirectory() ? 2 : 0;
    return nullptr;
  }
  uint32_t size = f.size();
  if (size > INLINE) r.blocks += size / FS_BLOCK + 1;
  else r.inl += plen + size + 16;
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

// the files on the new fs, low on room rather than out of it
static const char* room(const Rec& r) {
  uint32_t blocks = 2 + r.blocks + 2 * ((r.inl + DIR_BYTES - 1) / DIR_BYTES);
  return blocks + SPARE <= FS_BLOCKS ? nullptr : R_BIG;
}

// a size pass over the fs, as backup() writes it
static const char* sized(Rec& r, fs::FS* src) {
  r = {nullptr, 0, HEAD, 0, 0, true, 0, 0};
  const char* why = walk(src, rec, &r);
  return why ? why : room(r);
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
static const char* backup(fs::FS* src) {
  Back k = app1();
  if (!k.p) return R_FLASH;
  Rec r;
  const char* why = sized(r, src);
  if (why) return why;
  uint32_t end = r.at;
  uint16_t count = r.count;
  if (esp_partition_erase_range(k.p, k.off, (end + 4 + SECTOR - 1) & ~(SECTOR - 1)) != ESP_OK) return R_FLASH;
  uint8_t h[HEAD];
  wrLe32(h, MAGIC);
  wrLe16(h + 4, VERSION);
  wrLe16(h + 6, count);
  wrLe32(h + 8, end - HEAD);
  r = {k.p, k.off, 0, 0, 0, true, 0, 0};
  add(r, h, HEAD);
  why = walk(src, rec, &r);
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
  const From* f = from();
  if (!f || !md5Is(f->md5) || esp_flash_read(NULL, old, TABLE_AT, SECTOR) != ESP_OK) return R_TABLE;
  build(neu);
  const esp_partition_t* cd = coredump();
  bool ok = cd && putCheck(cd->address, neu, chk);
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

// this image from app1 into app0, image length only. Each erase yields, otadata keeps app1 till it's set
static bool copyApp() {
  const esp_partition_t* a0 = app(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  const esp_partition_t* a1 = esp_ota_get_running_partition();
  const esp_partition_pos_t pos = {a1->address, a1->size};
  esp_image_metadata_t md;
  if (!a0 || esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) != ESP_OK || md.image_len > a0->size) return false;
  uint8_t b[CHUNK];
  for (uint32_t at = 0; at < md.image_len; at += CHUNK) {
    size_t k = md.image_len - at < CHUNK ? md.image_len - at : CHUNK;
    if (at % SECTOR == 0 && esp_partition_erase_range(a0, at, SECTOR) != ESP_OK) return false;
    if (esp_partition_read(a1, at, b, k) != ESP_OK || esp_partition_write(a0, at, b, k) != ESP_OK) return false;
  }
  return app0Same();
}

// from app1: this build into app0, set checks the image, the next boot goes on from app0.
// A copy cut short leaves this build's header in app0, so a set that fails copies again
static Step save(Kind k) {
  if (k != OLD) return quit(R_TABLE);
  if (slot()) {
    const esp_partition_t* a0 = app(ESP_PARTITION_SUBTYPE_APP_OTA_0);
    bool ok = (app0Same() && esp_ota_set_boot_partition(a0) == ESP_OK) ||
              (copyApp() && esp_ota_set_boot_partition(a0) == ESP_OK);
    if (!ok) return quit(R_APP0);
    restart();
    return IDLE;
  }
  const char* why = backup(nullptr);
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

// a failed put back fails the same way again, so it stops with what fitted and OTA comes back
static Step restore(Kind k) {
  const char* why = k == NEW ? putBack() : R_TABLE;
  if (why) return quit(why);
  return setStep(CLEARING) ? CLEARING : IDLE;
}

// the old fs's first block pair, inside app1 now, so rescue() never finds old files again
static bool forget(const esp_partition_t* a1) {
  for (const From& f : FROM) {
    if (f.fs < a1->address || f.fs + 2 * SECTOR > a1->address + a1->size) continue;
    if (esp_partition_erase_range(a1, f.fs - a1->address, 2 * SECTOR) != ESP_OK) return false;
  }
  return true;
}

static void clearOut() {
  Back k = app1();
  if (k.p && esp_partition_erase_range(k.p, k.off, SECTOR) == ESP_OK && forget(k.p) && setStep(IDLE)) {
    Log::console(PSTR("Layout: done"));
  }
}

void resume() {
  Step s = step();
  if (s == IDLE) return;
  Kind k = kind();
  // crashing in the move: stop while the old table stands, or give up the put back
  bool unwritten = s == SAVING || (s == SAVED && k == OLD);
  if (BootGuard::crashStreak() >= CRASHES && (unwritten || s == RESTORING)) s = quit(R_CRASH);
  if (s == SAVING) s = save(k);
  if (s == SAVED) s = flip(k);
  if (s == RESTORING) s = restore(k);
  if (s == CLEARING) clearOut();
  otaHeld = step() != IDLE;   // app1 holds the files
}

const char* check() {
  if (step() != IDLE) return R_BUSY;
  if (kind() != OLD) return R_TABLE;
  Rec r;
  return sized(r, nullptr);
}

const char* start() {
  const char* why = check();
  if (why) return why;
  if (!setStep(SAVING)) return R_FLASH;
  Log::console(PSTR("Layout: moving"));
  restart();
  return nullptr;
}

// A USB install over the old table writes the new one and leaves the old fs where app1 is now: mounted
// read only from its old place, its files go into the backup, then the put back as a move does it
void rescue() {
  // a put back that crashed twice stays given up
  if (BootGuard::crashStreak() >= CRASHES || kind() != NEW || step() != IDLE || slot()) return;
  for (const From& f : FROM) {
    esp_partition_t p = {};
    p.flash_chip = esp_flash_default_chip;
    p.type = ESP_PARTITION_TYPE_DATA;
    p.subtype = ESP_PARTITION_SUBTYPE_DATA_SPIFFS;
    p.address = f.fs;
    p.size = f.fsSize;
    strlcpy(p.label, "old", sizeof(p.label));
    esp_vfs_littlefs_conf_t c = {};
    c.base_path = OLD_PATH;
    c.partition = &p;
    c.read_only = 1;
    if (esp_vfs_littlefs_register(&c) != ESP_OK) continue;   // nothing there
    fs::FSImplPtr impl(new VFSImpl());
    impl->mountpoint(OLD_PATH);
    fs::FS old(impl);
    const char* why = backup(&old);
    (void)esp_vfs_littlefs_unregister_partition(&p);   // read only, nothing to lose
    if (!why && !setStep(RESTORING)) why = R_FLASH;
    if (why) {
      Log::console(why);
      return;
    }
    Log::console(PSTR("Layout: old files found, putting back"));
    restart();
    return;
  }
}

}  // namespace Layout

#ifdef CORIUM_LAYOUT_AUTO
#ifndef CORIUM_LAYOUT_AFTER_MS
#define CORIUM_LAYOUT_AFTER_MS BootGuard::DAY_MS
#endif

// The move on its own: a day up on a boot that didn't follow a crash, then the first quiet minute.
// A refusal waits for the next boot, the new table ends it for good
class LayoutAuto : public Module {
  public:
    const char* name() override { return PSTR("layout"); }
    static constexpr uint32_t AFTER_MS = CORIUM_LAYOUT_AFTER_MS;
    static constexpr uint32_t QUIET_MS = 60000;
    void begin() override {
      if (Layout::kind() == Layout::OLD && Layout::step() == Layout::IDLE && !CrashDump::reset().crash) {
        (void)Registry::dueIn(this, AFTER_MS);
      }
    }
    void loop(unsigned long) override {
      if (!quiet()) {
        (void)Registry::dueIn(this, QUIET_MS);
        return;
      }
      const char* why = Layout::start();
      if (why) Log::console(why);
    }
  private:
    static bool quiet() {
      if (otaInProgress || Lifecycle::restartPending() || WebPortal::busy()) return false;
      for (uint8_t i = 0; i < Registry::count(); i++) {
        Sender* s = Registry::get(i)->sender();
        if (s && s->pending()) return false;
      }
      return true;
    }
};
#endif
}  // namespace corium

#ifdef CORIUM_LAYOUT_AUTO
static corium::LayoutAuto layoutAuto;
CORIUM_REGISTER_MODULE(layoutAuto)
#endif

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
