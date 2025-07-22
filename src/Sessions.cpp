#include "CryptoUtils.h"
#include "Gateway.h"
#include "Sessions.h"

#include <Arduino.h>
#include <WiFi.h>
#include <RadioLib.h>
#include "HT_SSD1306Wire.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include <Preferences.h>



Preferences preferences;
std::map<String, SessionInfo> sessionMap;

String encodeDevEUI() {
  return bytesToHex(devEUI, 8);
}

String devEUIToString(const uint8_t* id, size_t len) {
  return bytesToHex(id, len);
}

String bytesToHex(const uint8_t* data, size_t len) {
  String hex = "";
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) hex += "0";
    hex += String(data[i], HEX);
  }
  return hex;
}

String idToHexString(uint8_t* id, size_t len) {
  String out = "";
  for (int i = 0; i < len; i++) {
    if (id[i] < 0x10) out += "0";
    out += String(id[i], HEX);
  }
  return out;
}

size_t trimTrailingZeros(const uint8_t* data, size_t len) {
  while (len > 0 && data[len - 1] == 0) {
    len--;
  }
  return len;
}



void printHex(const uint8_t* data, size_t len, const char* label) {
  Serial.print(label);
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

void printBinaryBits(uint8_t* payload, size_t length) {
  Serial.println("[INFO] Raw Binary:");
  for (size_t i = 0; i < length; i++) {
    for (int b = 7; b >= 0; b--) {
      Serial.print((payload[i] >> b) & 0x01);
    }
    Serial.print(" ");
  
    // Newline every 8 bytes (64 bits)
    if ((i + 1) % 8 == 0) {
      Serial.println();
    }
  }
  // Final newline if payloadLength wasn't a multiple of 8
  if (length % 8 != 0) {
    Serial.println();
  }
}
// Session key derivation as per LoRaWAN 1.0 spec
void deriveSessionKey(uint8_t* outKey, uint8_t keyType, const uint8_t* appKey,
                      const uint8_t* joinNonce, const uint8_t* netID, const uint8_t* devNonce) {
  uint8_t input[16] = {0};
  input[0] = keyType;
  memcpy(input + 1, joinNonce, 3);   // bytes 1–3
  memcpy(input + 4, netID, 3);       // bytes 4–6
  memcpy(input + 7, devNonce, 2);    // bytes 7–8
  // bytes 9–15 remain zero

  aes128_encrypt_block(appKey, input, outKey);
}

void flushSessionFor(const String& devEUI) {
  sessionMap.erase(devEUI);  // remove from RAM

  String key = devEUI.substring(0, 8);
  preferences.begin("lora", false);
  preferences.remove(key.c_str());
  preferences.end();

  Serial.println("[NVS] Session flushed for: " + devEUI);
}


void saveSessionToNVS(const String& devEUI, SessionInfo session) {
  uint8_t encrypted[sizeof(SessionInfo)];
  encryptSession(session, encrypted);
  preferences.begin("lora", false);
  String key = devEUI.substring(0, 8);  // Truncate to 8 characters
  preferences.putBytes(key.c_str(), encrypted, 32);
  preferences.end();
}


bool loadSessionFromNVS(const String& devEUI, SessionInfo& session) {
  uint8_t encrypted[32];
  String key = devEUI.substring(0, 8);  // Truncate to 8 characters
  
  preferences.begin("lora", true);
  if (preferences.getBytesLength(key.c_str()) != 32) {
    preferences.end();
    return false;
  }
  preferences.getBytes(key.c_str(), encrypted, 32);
  preferences.end();
  decryptSession(encrypted, session);
  return true;
}



void storeSessionFor(String devEUI, const SessionInfo& session) {
  sessionMap[devEUI] = session;
  Serial.println("[MEM] Session cached in memory for device: " + devEUI);
  saveSessionToNVS(devEUI, session);
}

bool getSessionFor(String devEUI, SessionInfo& session) {
  if (sessionMap.find(devEUI) != sessionMap.end()) {
    session = sessionMap[devEUI];
    Serial.println("[INFO] Session found in RAM for: " + devEUI);
    return true;
  }

  Serial.println("[INFO] Session not found in RAM, trying NVS...");
  if (loadSessionFromNVS(devEUI, session)) {
    Serial.println("[INFO] Session loaded from NVS");
    sessionMap[devEUI] = session;  // cache in RAM
    return true;
  }

  Serial.println("[WARN] Session not found in RAM or NVS for:" + devEUI);
  return false;
}

bool sessionExists(const String& devEUI) {
  return sessionMap.find(devEUI) != sessionMap.end();
}
SessionStatus verifySession(const String& srcID, SessionInfo& session) {
  if (!getSessionFor(srcID, session)) {
    return SESSION_NOT_FOUND;
  }
  return SESSION_OK;
}

SessionStatus verifyHmac(uint8_t* buffer, size_t length, uint8_t* receivedHMAC) {

  if (!verifyHMAC(buffer, length, receivedHMAC)) {
    return SESSION_INVALID_HMAC;
  }
  return SESSION_OK;
}

void flushAllSessions() {
  // Clear RAM cache
  sessionMap.clear();
  Serial.println("[MEM] All sessions cleared from RAM.");

  // Clear NVS stored sessions
  preferences.begin("lora", false);
  preferences.clear();  // wipes all keys in the 'lora' namespace
  preferences.end();

  Serial.println("[NVS] All sessions cleared from NVS.");
}