#pragma once

#include <cstring>  // strlen
#include <new>      // placement new

#include "OutPacket.hpp"
#include "../../Flags.hpp"
#include "../../Helpers.hpp"
#include "../../Storage.hpp"

namespace AsyncMqttClientInternals {
class PublishOutPacket : public OutPacket {
 public:
  // Pre-compute wire length so the caller can allocate the combined block.
  static size_t computeLen(const char* topic, uint8_t qos,
                           const char* payload, size_t length);

  PublishOutPacket(const char* topic, uint8_t qos, bool retain,
                   const char* payload, size_t length, size_t wireLen);

  const uint8_t* data(size_t index = 0) const;
  size_t size() const;
  void setDup();

  // Combined allocation: struct + wire bytes in one umm block. _data lives
  // inline at this+1 - one malloc per publish (was two), no separate free.
  static void* operator new(size_t base, size_t flex) {
    return ::operator new(base + flex);
  }
  // Matching placement-delete called only if the ctor throws (it can't on
  // -fno-exceptions, but provided for correctness).
  static void operator delete(void* p, size_t /*flex*/) { ::operator delete(p); }
  // Regular delete (called by `delete tmp` via virtual destructor) frees the
  // whole combined block.
  static void operator delete(void* p) noexcept { ::operator delete(p); }
  // Guard against accidental `new PublishOutPacket(...)` without the flex arg.
  static void* operator new(size_t) = delete;

 private:
  size_t _len;
  // wire bytes follow this struct in the same allocation; see operator new.
  uint8_t* data_ptr() { return reinterpret_cast<uint8_t*>(this + 1); }
  const uint8_t* data_ptr() const { return reinterpret_cast<const uint8_t*>(this + 1); }
};
}  // namespace AsyncMqttClientInternals
