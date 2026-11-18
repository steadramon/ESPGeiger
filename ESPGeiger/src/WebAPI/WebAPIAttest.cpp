/*
  WebAPIAttest.cpp - official hardware attest and the genuine console command.

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
#if defined(WEBAPIOUT) && defined(ESPGEIGER_ATTEST)

#include <EGHttpServer.h>
#include "WebAPI.h"
#include "WebAPIKey.h"
#include "MsgPack.h"
#include "GRNG/sha256.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Console/Console.h"
#include "Corium/System/HwKey.h"


// a challenge is answered this pass or never, a stale nonce only fails
bool WebAPI::takeSign() {
  if (_att != ATT_SIGN) return false;
  _att = ATT_WAIT;
  return true;
}

// after the off / forget checks, before a post takes the request
void WebAPI::attestIfDue(bool sign) {
  if ((sign || _att == ATT_DUE || _att == ATT_FETCH) && _station != 0 && !request.busy())
    sendAttest(sign ? _nonce : nullptr);
}

static_assert(EGHttpClient::BODY_CAP > corium::HwKey::BLOB, "blob reply needs EGHTTP_BODY_CAP");

// +1 sees a longer reply
void __attribute__((noinline)) WebAPI::takeBlob() {
  uint8_t r[corium::HwKey::BLOB + 1];
  int code;
  size_t n;
  if (!request.take(&code, r, sizeof(r), &n)) return;
  _sent = SENT_NONE;
  onBlob(code, r, n);
}

// raw blob; 403 / 404 = not for this boot
void WebAPI::onBlob(int code, const uint8_t* r, size_t n) {
  if (code == 200 && n == corium::HwKey::BLOB && corium::HwKey::put(r)) {
    corium::Log::debug(PSTR("WebAPI: Key restored"));
    _att = ATT_DUE;
  } else if (code == 200 || code == 403 || code == 404) {
    corium::Log::debug(PSTR("WebAPI: Key not restored"));
    _att = ATT_DONE;
  }
}

// 404 = not official hardware, nothing more this boot
void WebAPI::onChallenge(int code, const uint8_t* r, size_t n) {
  MsgPack::Reader reader(r, n);
  char hex[2 * ATT_NONCE + 1];
  if (code == 200 && reader.find_key(PSTR("an")) && reader.read_str(hex, sizeof(hex)) &&
      hexDecode(_nonce, hex, ATT_NONCE)) {
    _att = ATT_SIGN;
  } else if (code == 403 || code == 404) {
    _att = ATT_DONE;
  }
}

void WebAPI::onAttest(int code) {
  if (code == 200) {
    corium::Log::debug(PSTR("WebAPI: Official hardware verified"));
    _att = ATT_DONE;
  } else if (code == 403 || code == 404) {
    corium::Log::debug(PSTR("WebAPI: Official hardware not verified"));
    _att = ATT_DONE;
  }
}

// genuine <64 hex> over serial, setup.espgeiger.com's check: its own tag, never a station attest
static const char CHECK_TAG[] PROGMEM = "espgeiger-check-v1";
static constexpr size_t SIG_LINE = 32;

static void genuine(const char* hex) {
  uint8_t nonce[ATT_NONCE], mac[ATT_MAC], sig[corium::HwKey::SIG];
  if (!hex || !hexDecode(nonce, hex, ATT_NONCE) || !corium::DeviceInfo::efuseMac(mac)) {
    corium::Log::console(PSTR("genuine <64 hex>"));
    return;
  }
  Sha256Class sha;
  sha.init();
  sha.write((const uint8_t*)CHECK_TAG, sizeof(CHECK_TAG) - 1);
  sha.write(mac, ATT_MAC);
  sha.write(nonce, ATT_NONCE);
  if (!corium::HwKey::sign(sha.result(), sig)) {
    corium::Log::console(PSTR("Genuine: no"));
    return;
  }
  char out[2 * SIG_LINE + 1];
  hexEncode(out, mac, ATT_MAC);
  out[2 * ATT_MAC] = '\0';
  corium::Log::console(PSTR("Genuine: mac %s"), out);
  for (size_t i = 0; i < corium::HwKey::SIG / SIG_LINE; i++) {
    hexEncode(out, sig + i * SIG_LINE, SIG_LINE);
    out[2 * SIG_LINE] = '\0';
    corium::Log::console(PSTR("Genuine: sig %u %s"), (unsigned)i, out);
  }
  corium::Log::console(PSTR("Genuine: end"));
}

// the reply reaches USB even while SerialOut streams
static void cmd_genuine() {
  const bool was = corium::Log::serialOn();
  corium::Log::setSerialOn(true);
  genuine(corium::Console::next());
  corium::Log::setSerialOn(was);
}
CORIUM_REGISTER_CMD(genuine, cmd_genuine)
#endif
