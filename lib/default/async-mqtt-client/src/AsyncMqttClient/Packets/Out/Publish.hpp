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

  // Single combined alloc: struct + wire bytes in one block. _data at this+1.
  static void* operator new(size_t base, size_t flex) {
    return ::operator new(base + flex);
  }
  static void operator delete(void* p, size_t) { ::operator delete(p); }
  static void operator delete(void* p) noexcept { ::operator delete(p); }
  static void* operator new(size_t) = delete;  // force the flex form

 private:
  size_t _len;
  uint8_t* data_ptr() { return reinterpret_cast<uint8_t*>(this + 1); }
  const uint8_t* data_ptr() const { return reinterpret_cast<const uint8_t*>(this + 1); }
};
}  // namespace AsyncMqttClientInternals
