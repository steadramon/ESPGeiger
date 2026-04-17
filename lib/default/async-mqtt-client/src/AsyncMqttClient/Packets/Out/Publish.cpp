#include "Publish.hpp"

using AsyncMqttClientInternals::PublishOutPacket;

PublishOutPacket::PublishOutPacket(const char* topic, uint8_t qos, bool retain, const char* payload, size_t length) {
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
  uint8_t remainingLengthLength = AsyncMqttClientInternals::Helpers::encodeRemainingLength(remainingLength, fixedHeader + 1);

  _len = 0;
  _len += 1 + remainingLengthLength;
  _len += 2;
  _len += topicLength;
  if (qos != 0) _len += 2;
  if (payload != nullptr) _len += payloadLength;

  _data = (uint8_t*)malloc(_len);
  if (!_data) { _len = 0; return; }

  _packetId = (qos != 0) ? _getNextPacketId() : 1;
  char packetIdBytes[2];
  packetIdBytes[0] = _packetId >> 8;
  packetIdBytes[1] = _packetId & 0xFF;

  size_t pos = 0;
  memcpy(_data + pos, fixedHeader, 1 + remainingLengthLength); pos += 1 + remainingLengthLength;
  memcpy(_data + pos, topicLengthBytes, 2); pos += 2;
  memcpy(_data + pos, topic, topicLength); pos += topicLength;
  if (qos != 0) {
    memcpy(_data + pos, packetIdBytes, 2); pos += 2;
    _released = false;
  }
  if (payload != nullptr) { memcpy(_data + pos, payload, payloadLength); pos += payloadLength; }
}

PublishOutPacket::~PublishOutPacket() {
  free(_data);
}

const uint8_t* PublishOutPacket::data(size_t index) const {
  return _data + index;
}

size_t PublishOutPacket::size() const {
  return _len;
}

void PublishOutPacket::setDup() {
  _data[0] |= AsyncMqttClientInternals::HeaderFlag.PUBLISH_DUP;
}
