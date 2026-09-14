// Helpers.hpp only defines these for ESP32/ESP8266. Claiming an architecture
// would also switch on EGAsyncTCP's DRAM range check, so host versions here.

#define SEMAPHORE_TAKE(...)  ((void)0)
#define SEMAPHORE_GIVE()     ((void)0)
#define GET_FREE_MEMORY()    ESP.getMaxFreeBlockSize()
#define log_i(...)           ((void)0)
#define log_e(...)           ((void)0)
#define log_w(...)           ((void)0)

#include "EGAsyncTCP.cpp"
#include "AsyncMqttClient.cpp"
#include "AsyncMqttClient/Packets/ConnAckPacket.cpp"
#include "AsyncMqttClient/Packets/PingRespPacket.cpp"
#include "AsyncMqttClient/Packets/PubAckPacket.cpp"
#include "AsyncMqttClient/Packets/PubCompPacket.cpp"
#include "AsyncMqttClient/Packets/PubRecPacket.cpp"
#include "AsyncMqttClient/Packets/PubRelPacket.cpp"
#include "AsyncMqttClient/Packets/PublishPacket.cpp"
#include "AsyncMqttClient/Packets/SubAckPacket.cpp"
#include "AsyncMqttClient/Packets/UnsubAckPacket.cpp"
#include "AsyncMqttClient/Packets/Out/Connect.cpp"
#include "AsyncMqttClient/Packets/Out/Disconn.cpp"
#include "AsyncMqttClient/Packets/Out/OutPacket.cpp"
#include "AsyncMqttClient/Packets/Out/PingReq.cpp"
#include "AsyncMqttClient/Packets/Out/PubAck.cpp"
#include "AsyncMqttClient/Packets/Out/Publish.cpp"
#include "AsyncMqttClient/Packets/Out/Subscribe.cpp"
#include "AsyncMqttClient/Packets/Out/Unsubscribe.cpp"
