#include "mqtt_client.h"
#include "LoraWANLite.h"  // for encodeDevEUI, frequency_plan, sendDataAck
#include "CryptoUtils.h"

#include <Arduino.h>
#include <WiFi.h>
#include <RadioLib.h>
#include "HT_SSD1306Wire.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"

// Helper to send ACK with command

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String jsonStr;
  for (unsigned int i = 0; i < length; i++) {
    jsonStr += (char)payload[i];
  }

  Serial.print("[MQTT] Received message on topic: ");
  Serial.println(topic);
  Serial.print("[MQTT] Message: ");
  Serial.println(jsonStr);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    Serial.print("[MQTT] JSON parse failed: ");
    Serial.println(error.f_str());
    return;
  }

  String dst = doc["dst"];
  String payloadCmd = doc["payload"];

  dst.trim();
  payloadCmd.trim();

  if (dst.length() == 0 || payloadCmd.length() == 0) {
    Serial.println("[MQTT] Missing 'dst' or 'payload' in message.");
    return;
  }

  // If message is for this device
  if (dst == encodeDevEUI()) {
    if (payloadCmd.startsWith("Update:")) {
      int firstColon = payloadCmd.indexOf(':');
      int secondColon = payloadCmd.indexOf(':', firstColon + 1);
      int thirdColon = payloadCmd.indexOf(':', secondColon + 1);

      if (firstColon > 0 && secondColon > firstColon) {
        String devAddr = payloadCmd.substring(firstColon + 1, secondColon);
        String appKey = payloadCmd.substring(secondColon + 1);

        devAddr.trim();
        appKey.trim();

      } else {
        Serial.println("[KEY UPDATE] Malformed update payload: " + payloadCmd);
      }
    } else {
      Serial.println("[KEY UPDATE] Payload not recognized: " + payloadCmd);
    }
    return;
  }
  Serial.println("[LORA] Sending command with ACK to " + dst + ": " + payloadCmd );
}



void mqttReconnect() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Attempting connection...");
    if (mqttClient.connect(encodeDevEUI().c_str())) {
      Serial.println("connected");
      String topic = "devices/" + encodeDevEUI() + "/commands";
      mqttClient.subscribe(topic.c_str());
      Serial.println("[MQTT] Subscribed to " + topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

void mqttinit() {
  mqttClient.setServer(ip, port);
  mqttClient.setCallback(onMqttMessage);
}

void sendBoardInfo() {
  if (!mqttClient.connected()) return;

  String json = "{";
  json += "\"chip_id\":\"" + encodeDevEUI() + "\",";
  json += "\"model\":\"" + String(ESP.getChipModel()) + "\",";
  json += "\"revision\":" + String(ESP.getChipRevision()) + ",";
  json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"flash_mb\":" + String(ESP.getFlashChipSize() / (1024 * 1024)) + ",";
  json += "\"heap_kb\":" + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"frequency_plan\":" + String(frequency_plan, 1) + ",";
  String topic = "devices/" + encodeDevEUI() + "/boardinfo";
  mqttClient.publish(topic.c_str(), json.c_str());
}

void sendInitialDeviceInfo() {
  sendBoardInfo();
}