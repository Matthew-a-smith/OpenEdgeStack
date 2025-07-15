#include "CryptoUtils.h"
#include "LoraWANLite.h"
#include "Sessions.h"

#include <Arduino.h>
#include <Preferences.h>
#include <map>



Preferences preferences;
std::map<String, SessionInfo> sessionMap;


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

void saveSessionToNVS(const String& devEUI, SessionInfo session) {
  uint8_t encrypted[sizeof(SessionInfo)];
  encryptSession(session, encrypted);
  
  String key = devEUI.substring(0, 8);  // Truncate to 8 characters
  Serial.println("[NVS] Saving session to NVS:");
  Serial.println("       Full devEUI: " + devEUI);
  Serial.println("       Truncated key: " + key);

  preferences.begin("lora", false);
  preferences.putBytes(key.c_str(), encrypted, sizeof(SessionInfo));
  preferences.end();

  Serial.println("[NVS] Session saved under key: " + key);
}

bool loadSessionFromNVS(const String& devEUI, SessionInfo& session) {
  uint8_t encrypted[sizeof(SessionInfo)];
  String key = devEUI.substring(0, 8);  // Truncate to 8 characters
  
  Serial.println("[NVS] Attempting to load session:");
  Serial.println("       Full devEUI: " + devEUI);
  Serial.println("       Truncated key: " + key);

  preferences.begin("lora", true);
  size_t len = preferences.getBytesLength(key.c_str());
  if (len != sizeof(SessionInfo)) {
    preferences.end();
    Serial.println("[NVS] Session not found (or wrong size). Found length: " + String(len));
    return false;
  }

  preferences.getBytes(key.c_str(), encrypted, sizeof(SessionInfo));
  preferences.end();
  decryptSession(encrypted, session);

  Serial.println("[NVS] Session loaded and decrypted from key: " + key);
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
  if (loadSessionFromNVS(devEUI.c_str(), session)) {
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