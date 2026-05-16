#include "Publish.hpp"

using AsyncMqttClientInternals::PublishOutPacket;

size_t PublishOutPacket::computeLen(const char* topic, uint8_t qos,
                                    const char* payload, size_t length) {
  uint16_t topicLength = strlen(topic);
  uint32_t payloadLength = length;
  if (payload != nullptr && payloadLength == 0) payloadLength = strlen(payload);

  uint32_t remainingLength = 2 + topicLength + payloadLength;
  if (qos != 0) remainingLength += 2;

  // encodeRemainingLength returns 1..4 bytes; we need a temp here just to
  // measure - caller-side prep, not on wire yet.
  char scratch[5];
  uint8_t remainingLengthLength =
      AsyncMqttClientInternals::Helpers::encodeRemainingLength(remainingLength, scratch);

  size_t total = 0;
  total += 1 + remainingLengthLength;
  total += 2;
  total += topicLength;
  if (qos != 0) total += 2;
  if (payload != nullptr) total += payloadLength;
  return total;
}

PublishOutPacket::PublishOutPacket(const char* topic, uint8_t qos, bool retain,
                                   const char* payload, size_t length, size_t wireLen) {
  _len = wireLen;

  char fixedHeader[5];
  fixedHeader[0] = AsyncMqttClientInternals::PacketType.PUBLISH;
  fixedHeader[0] = fixedHeader[0] << 4;
  if (retain) fixedHeader[0] |= AsyncMqttClientInternals::HeaderFlag.PUBLISH_RETAIN;
  switch (qos) {
    case 0:
      fixedHeader[0] |= AsyncMqttClientInternals::HeaderFlag.PUBLISH_QOS0;
      break;
    case 1:
      fixedHeader[0] |= AsyncMqttClientInternals::HeaderFlag.PUBLISH_QOS1;
      break;
    case 2:
      fixedHeader[0] |= AsyncMqttClientInternals::HeaderFlag.PUBLISH_QOS2;
      break;
  }

  uint16_t topicLength = strlen(topic);
  char topicLengthBytes[2];
  topicLengthBytes[0] = topicLength >> 8;
  topicLengthBytes[1] = topicLength & 0xFF;

  uint32_t payloadLength = length;
  if (payload != nullptr && payloadLength == 0) payloadLength = strlen(payload);

  uint32_t remainingLength = 2 + topicLength + payloadLength;
  if (qos != 0) remainingLength += 2;
  uint8_t remainingLengthLength =
      AsyncMqttClientInternals::Helpers::encodeRemainingLength(remainingLength, fixedHeader + 1);

  _packetId = (qos != 0) ? _getNextPacketId() : 1;
  char packetIdBytes[2];
  packetIdBytes[0] = _packetId >> 8;
  packetIdBytes[1] = _packetId & 0xFF;

  uint8_t* out = data_ptr();
  size_t pos = 0;
  memcpy(out + pos, fixedHeader, 1 + remainingLengthLength); pos += 1 + remainingLengthLength;
  memcpy(out + pos, topicLengthBytes, 2); pos += 2;
  memcpy(out + pos, topic, topicLength); pos += topicLength;
  if (qos != 0) {
    memcpy(out + pos, packetIdBytes, 2); pos += 2;
    _released = false;
  }
  if (payload != nullptr) { memcpy(out + pos, payload, payloadLength); pos += payloadLength; }
}

const uint8_t* PublishOutPacket::data(size_t index) const {
  return data_ptr() + index;
}

size_t PublishOutPacket::size() const {
  return _len;
}

void PublishOutPacket::setDup() {
  data_ptr()[0] |= AsyncMqttClientInternals::HeaderFlag.PUBLISH_DUP;
}
