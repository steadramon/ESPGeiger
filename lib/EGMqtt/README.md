# EGMqtt

MQTT 3.1.1 client for ESP8266 and ESP32, run from the main loop on EGTcpConn.
QoS 0, 1 and 2, callbacks from `loop()`, no TLS. `EGMqttWire` builds and
parses the packets on its own, with no I/O.

```cpp
#include <EGMqtt.h>

EGMqtt mq;

void up(void*, bool session) {
  if (!session) mq.subscribe("dev1/cmd/#", 1);
}

void message(void*, const EGMqtt::Message& m) {
  // m.topic and m.payload both end in a NUL, m.len doesn't count it
}

void setup() {
  mq.setServer("broker.local", 1883);
  mq.setClientId("dev1");
  mq.setWill("dev1/lwt", "offline", 1, true);
  mq.onConnect(up);
  mq.onMessage(message);
  mq.connect();
}

void loop() {
  mq.loop();
}
```

Reconnecting is up to the caller, `onDisconnect` says why it went. Buffer
sizes are `EGMQTT_OUT_CAP`, `EGMQTT_IN_CAP`, `EGMQTT_QOS2_IN` and
`EGMQTT_STR_CAP`, see `EGMqtt.h`.
