#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

void mqttinit();
void mqttReconnect();
void onMqttMessage(char* topic, byte* payload, unsigned int length);
void sendBoardInfo();
void sendInitialDeviceInfo();
// MQTT + WiFi
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

extern int port;
extern const char* ip;  // ✅ Better than String

#endif  // MQTT_CLIENT_H
