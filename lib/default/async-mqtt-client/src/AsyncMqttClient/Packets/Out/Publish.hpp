#pragma once

#include <cstring>  // strlen

#include "OutPacket.hpp"
#include "../../Flags.hpp"
#include "../../Helpers.hpp"
#include "../../Storage.hpp"

namespace AsyncMqttClientInternals {
class PublishOutPacket : public OutPacket {
 public:
  PublishOutPacket(const char* topic, uint8_t qos, bool retain, const char* payload, size_t length);
  ~PublishOutPacket();
  const uint8_t* data(size_t index = 0) const;
  size_t size() const;

  void setDup();  // you cannot unset dup

 private:
  uint8_t* _data;
  size_t _len;
};
}  // namespace AsyncMqttClientInternals
