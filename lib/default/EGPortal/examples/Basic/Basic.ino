/*
  EGPortal Basic example.

  Brings up a captive WiFi setup portal. When the user submits the form,
  the saved callback fires and we attempt to connect to the chosen network.

  Hardware: any ESP8266 / ESP32. No external components needed.
*/
#include <Arduino.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif
#include <EGPortal.h>

EGPortal portal;
volatile bool gotCreds = false;
char wantSsid[33];
char wantPass[65];

void onCreds(const char* ssid, const char* password, void*) {
  strncpy(wantSsid, ssid, sizeof(wantSsid) - 1);
  strncpy(wantPass, password, sizeof(wantPass) - 1);
  gotCreds = true;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  portal.setTitle("MyDevice Setup");
  portal.onSave(onCreds);
  portal.begin("MyDevice-Setup");

  Serial.println("Portal up. Connect to MyDevice-Setup AP and visit any URL.");
}

void loop() {
  portal.loop();

  if (gotCreds) {
    Serial.print("Got creds, connecting to ");
    Serial.println(wantSsid);
    portal.end();
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wantSsid, wantPass);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.print("\nConnected: ");
    Serial.println(WiFi.localIP());
    gotCreds = false;
  }
}
