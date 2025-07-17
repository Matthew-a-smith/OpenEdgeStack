#include "LoraWANLite.h"
#include "EndDevice.h"
#include "CryptoUtils.h"
#include "Sessions.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>


// ────── Join Request Struct & Buffers ───────────────────────────────
String devEUIHex = idToHexString(devEUI);

// ────── JoinAccept Packet Layout (Received, Encrypted, 16 bytes) ──────
// Offset | Size | Field       | Description
// -------|------|-------------|------------------------------
// 0      | 4    | DevAddr     | Device address assigned by the network
// 4      | 3    | JoinNonce   | Nonce from network for session key derivation
// 7      | 3    | NetID       | Identifier of the LoRaWAN network
// 10     | 2    | DevNonce    | Echo of our original devNonce (LE)
// 12     | 4    | Unknown/MIC | Possibly reserved or message integrity code
//

bool handleJoinAccept(uint8_t* buffer, size_t len) {
  if (len != 16) {
    Serial.println("[ERROR] Invalid JoinAccept length.");
    return false;
  }

  uint8_t decrypted[16];
  aes128_encrypt_block(appKey, buffer, decrypted); // ✅

  uint16_t devNonce = 0;
  devNonce = (decrypted[11] << 8) | decrypted[10];

  uint32_t devAddr;
  uint8_t joinNonce[3], netID[3];
  memcpy(&devAddr, decrypted, 4);
  memcpy(joinNonce, decrypted + 4, 3);
  memcpy(netID, decrypted + 7, 3);

 
  uint8_t appSKey[16], nwkSKey[16];
  deriveSessionKey(appSKey, 0x02, appKey, joinNonce, netID, (uint8_t*)&devNonce);
  deriveSessionKey(nwkSKey, 0x01, appKey, joinNonce, netID, (uint8_t*)&devNonce);

  Serial.println("[JOIN] JoinAccept decrypted.");
  Serial.print("[JOIN] Assigned DevAddr: 0x"); Serial.println(devAddr, HEX);
  printHex(joinNonce, 3, "[JOIN] JoinNonce: ");
  printHex(netID, 3, "[JOIN] NetID: ");
  printHex((uint8_t*)&devNonce, 2, "[JOIN] DevNonce: "); 
  Serial.println("[JOIN] Session keys derived successfully.");

  SessionInfo session;
  session.devAddr = devAddr;
  memcpy(session.appSKey, appSKey, 16);
  memcpy(session.nwkSKey, nwkSKey, 16);
  memcpy(session.joinNonce, joinNonce, 3);
  memcpy(session.netID, netID, 3);
  memcpy(session.devNonce, &devNonce, 2); 
  storeSessionFor(devEUIHex, session);
  Serial.println("[JOIN] Session stored for device: " + devEUIHex);
  return true;
}


// ────── JoinRequest Packet Byte Layout (18 bytes) ──────
// Offset | Size | Field       | Description
// -------|------|-------------|------------------------------
// 0      | 8    | devEUI      | Device unique identifier
// 8      | 8    | appEUI      | Application identifier
// 16     | 2    | devNonce    | Random nonce for this join request (little-endian)
struct JoinRequest {
  uint8_t devEUI[8];
  uint8_t appEUI[8];
  uint16_t devNonce;
};

void sendJoinRequest(int maxRetries, unsigned long retryDelay) {
  SessionInfo session;
  SessionStatus status = verifySession(devEUIHex, session);
  if (status == SESSION_OK) {
    Serial.println("[JOIN] Session already exists. Skipping join.");
    return;
  }

  bool ackReceived = false;

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    Serial.printf("[JOIN] Attempt %d of %d\n", attempt, maxRetries);

    uint16_t devNonce = esp_random() & 0xFFFF;

    JoinRequest joinReq;
    memcpy(joinReq.devEUI, devEUI, 8);
    memcpy(joinReq.appEUI, appEUI, 8);
    joinReq.devNonce = devNonce;

    uint8_t buffer[22];
    memcpy(buffer, joinReq.devEUI, 8);
    memcpy(buffer + 8, joinReq.appEUI, 8);
    buffer[16] = devNonce & 0xFF;
    buffer[17] = (devNonce >> 8) & 0xFF;

    uint8_t mic[32];
    computeHMAC_SHA256(hmacKey, sizeof(hmacKey), buffer, 18, mic);
    memcpy(buffer + 18, mic, 4);

    transmissonFlag = true;
    lora->standby();
    delay(5);
    lora->transmit(buffer, sizeof(buffer));
    delay(10);
    lora->startReceive();

    String joinReply;
    if (lora->receive(joinReply, 0) == RADIOLIB_ERR_NONE && joinReply.length() == 16) {
      uint8_t raw[16];
      for (int i = 0; i < 16; i++) raw[i] = joinReply[i];
      if (handleJoinAccept(raw, 16)) {
        verifySession(devEUIHex, session);
        ackReceived = true;
        transmissonFlag = false;
        Serial.println("[JOIN] Join successful.");
        break;
      }
    }

    transmissonFlag = false;
    Serial.println("[JOIN] No valid reply. Retrying...");
    if (attempt < maxRetries) delay(retryDelay);
  }

  if (!ackReceived) {
    Serial.println("[JOIN] Join failed after maximum attempts.");
  }
}

// ────── Local Storage File Layout (One group per file) ──────
// Filename: /group_<index>.bin
// Content: Serialized PacketGroup structure
//
// Structure in memory:
// PacketGroup:
//   - MAX_GROUP_FILE_SIZE       → Max bytes allowed per group file
//   - MAX_GROUP_LIMIT           → Max total group files per prefix
//   - MAX_GROUP_PREFIX_LIMIT    → Max unique group name prefixes allowed (Grp1, Grp2, etc.)
//
// PacketEntry:
//   - length                    → size_t (4 bytes, platform dependent)
//   - data                      → dynamically allocated payload data (length bytes)
//

// ────── StoreAge handling ───────────────────────────────

void storePacket(const uint8_t* data, size_t length, DataType dataType, const char* pathBase) {
    static int groupSuffixes[32] = {0};

    int groupIndex = pathBase[strlen(pathBase) - 1] - '1';


    if (groupIndex < 0 || groupIndex >= groupConfig.groupLimit) {
        Serial.printf("[ERROR] Invalid group index: %d for path %s\n", groupIndex, pathBase);
        return;
    }

    int suffix = groupSuffixes[groupIndex];

    if (suffix >= groupConfig.groupPrefixLimit) {
        Serial.printf("[ERROR] No more file slots for %s (limit %d reached)\n", pathBase, groupConfig.groupPrefixLimit);
        return;
    }

    char path[32];
    snprintf(path, sizeof(path), "/%s_%d.bin", pathBase, suffix);

    size_t currentFileSize = 0;
    File checkFile = SPIFFS.open(path, FILE_READ);
    if (checkFile) {
        currentFileSize = checkFile.size();
        checkFile.close();
    }

    if (currentFileSize + sizeof(uint16_t) + length > groupConfig.maxFileSize) {
        suffix++;
        if (suffix >= groupConfig.groupPrefixLimit) {
            Serial.printf("[ERROR] No more file slots for %s (limit %d reached)\n", pathBase, groupConfig.groupPrefixLimit);
            return;
        }
        groupSuffixes[groupIndex] = suffix;
        snprintf(path, sizeof(path), "/%s_%d.bin", pathBase, suffix);
        Serial.printf("[INFO] Switched to new group file: %s\n", path);
    }

    File file = SPIFFS.open(path, FILE_APPEND);
    if (!file) {
        Serial.println("[ERROR] Failed to open file for writing");
        return;
    }

    uint16_t len = length + 1;
    file.write((uint8_t*)&len, sizeof(len));
    file.write((uint8_t*)&dataType, 1);
    file.write(data, length);
    file.close();

    Serial.printf("[OK] Stored %d bytes to %s\n", (int)(2 + length), path);
}

// ────── Stored Group Payload Layout Before Encryption ──────
// Offset | Size          | Field       | Description
// -------|---------------|-------------|------------------------------
// 1      | fileSize      | Raw Data    | All group file bytes from SPIFFS
//
// Total = 1 + size of group_X.bin
//
// Notes:
// - Data is encrypted with `appSKey` before sending
// - Transmitted buffer is: [SenderID (8)] + [Encrypted Group] + [HMAC (8)]
// - Final format handled by `encryptAndPackage()`

// Helper to load, encrypt, and send a file by full path
bool sendGroupFileAtPath(const char* path, bool finalSend = true) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    Serial.printf("[ERROR] Failed to open file: %s\n", path);
    return false;
  }

  std::vector<uint8_t> packetData;

  while (file.available() >= 3) {  // At least 2 bytes for length and 1 for type
    uint16_t entryLen;
    if (file.readBytes((char*)&entryLen, 2) != 2) break;

    if (entryLen < 1) break;  // Invalid length
    uint8_t typeByte;
    if (!file.read(&typeByte, 1)) break;

    size_t dataLen = entryLen - 1;
    std::vector<uint8_t> entry(dataLen);
    if (file.read(entry.data(), dataLen) != dataLen) break;

    packetData.push_back(typeByte);
    packetData.insert(packetData.end(), entry.begin(), entry.end());
  }

  file.close();

  if (packetData.empty()) {
    Serial.printf("[ERROR] No valid entries found in %s\n", path);
    return false;
  }

  SessionInfo session;
  SessionStatus status = verifySession(devEUIHex, session);
  if (status != SESSION_OK) {
    Serial.println("[ERROR] No valid session.");
    return false;
  }

  size_t finalLen = 0;
  uint8_t* finalPacket = encryptAndPackage(packetData.data(), packetData.size(), session, finalLen, devEUI);

  transmissonFlag = true;
  lora->standby();
  delay(5);
  int result = lora->transmit(finalPacket, finalLen);
  delay(10);

  if (finalSend) {
    lora->startReceive();
  }

  transmissonFlag = false;

  if (result == RADIOLIB_ERR_NONE) {
    Serial.println("[ACK] Sent successfully.");
  } else {
    Serial.println("[ACK] Failed to send ACK.");
  }

  delete[] finalPacket;
  delay(500);
  return true;
}


void sendStoredGroupFile(const char* pathBase) {
  for (int suffix = 0; suffix < 9; suffix++) {
    char path1[32];
    snprintf(path1, sizeof(path1), "/%s_%d.bin", pathBase, suffix);

    if (!SPIFFS.exists(path1)) {
      continue;
    }

    // Check if there's a second file before we send anything
    char path2[32];
    snprintf(path2, sizeof(path2), "/%s_%d.bin", pathBase, suffix + 1);
    bool hasSecond = SPIFFS.exists(path2);

    // Send first file (tell it not to restart RX if there's a second one)
    if (sendGroupFileAtPath(path1, !hasSecond)) {
      Serial.printf("[INFO] Sent first file: %s\n", path1);
    }

    delay(500);
     
    if (hasSecond) {
      sendGroupFileAtPath(path2, true);  // Restart RX after second send
      Serial.printf("[INFO] Sent second file: %s\n", path2);
    }
    break;  // Only send one or two files total
  }
}

void sender(const uint8_t* finalPacket, size_t finalLen) {

  transmissonFlag = true;
  lora->standby();
  delay(5);
  int result = lora->transmit(finalPacket, finalLen);
  delay(10);                      
  int rxState = lora->startReceive();
  transmissonFlag = false;    
  if (result == RADIOLIB_ERR_NONE) {
    Serial.println("[ACK] Sent successfully.");
  } else {
    Serial.println("[ACK] Failed to send ACK.");
  }
}


// ────── Polling Packet Format via encryptAndPackage() ──────
// Offset | Size          | Field          | Description
// -------|---------------|----------------|------------------------------
// 0      | 8 bytes       | Sender devEUI  | ID of the sending device
// 8      | N (padded)    | AES Encrypted  | Payload content
// 8+N    | 8 bytes       | HMAC (trunc.)  | First 8 bytes of SHA-256 HMAC
//
// Notes:
// - Polls up to `maxRetries` times with timeout per attempt
// - Listens for string-based ACK (e.g., "ACK:xyz") using `lora.receive()`


void pollLora(
    const uint8_t* payloadData,
    size_t payloadLen,
    DataType dataType,
    unsigned long preDelayMillis 
) {

  SessionInfo session;
  SessionStatus status = verifySession(devEUIHex, session);
  if (status != SESSION_OK) {
    Serial.println("[ERROR] Session not found");
    return;
  }

  // Allocate space: 1 byte for type + payload
  size_t totalLen = payloadLen + 1;
  uint8_t* packetData = new uint8_t[totalLen];
  packetData[0] = (uint8_t)dataType; // first byte = type
  memcpy(packetData + 1, payloadData, payloadLen); // rest = payload

  size_t finalLen = 0;
  uint8_t* finalPacket = encryptAndPackage(packetData, totalLen, session, finalLen, devEUI);

  // Optional delay before sending
  if (preDelayMillis > 0) {
    Serial.print("[INFO] Waiting for ");
    Serial.print(preDelayMillis);
    Serial.println(" ms before sending...");
    delay(preDelayMillis);
  }

  // Retry sending
    Serial.print("[INFO] Sending payload + HMAC... Attempt ");
    sender(finalPacket, finalLen);
  
  delete[] packetData;
}


// ────── ACKed Packet Format (Same as encryptAndPackage) ──────
// Offset | Size          | Field          | Description
// -------|---------------|----------------|------------------------------
// 0      | 8 bytes       | Sender devEUI  | Unique device ID
// 8      | N bytes       | Encrypted      | AES-padded payload
// 8+N    | 8 bytes       | HMAC           | Authenticator for verification
//
// Notes:
// - Unlike `pollLora()`, this only attempts transmission once
// - Uses 5-second timeout window
// - Stores response in global variable `globalReply` if received

void sendLora(const uint8_t* payloadData, size_t payloadLen, DataType dataType) {
  SessionInfo session;
  SessionStatus status = verifySession(devEUIHex, session);
  if (status != SESSION_OK) {
    Serial.println("[ERROR] Session not found");
    return;
  }

  // Allocate space: 1 byte for type + payload
  size_t totalLen = payloadLen + 1;
  uint8_t* packetData = new uint8_t[totalLen];
  packetData[0] = (uint8_t)dataType; // first byte = type
  memcpy(packetData + 1, payloadData, payloadLen); // rest = payload

  // Encrypt + package
  size_t finalLen = 0;
  uint8_t* finalPacket = encryptAndPackage(packetData, totalLen, session, finalLen, devEUI);

  // Send
  sender(finalPacket, finalLen);

  delete[] packetData;
}
 

void handlePacket(uint8_t* buffer, size_t length) {
  
  Serial.println("==== [RX PACKET] ====");
  Serial.printf("Total length: %d bytes\n", length);
  printHex(buffer, length, "[RAW] Data: ");

  uint8_t* srcID = buffer;
  uint8_t* payload = buffer + 8;
  uint8_t* receivedHMAC = buffer + length - 8;
  size_t payloadLength = length - 8 - 8;

  String srcIDString = idToHexString(srcID);

  if (payload == gatewayEUI) {
    flushSessionFor(srcIDString);
    printHex(payload, payloadLength, "[INFO] Payload bytes: ");
  }

  SessionInfo session;
  SessionStatus status = verifySession(srcIDString, session);
  if (status != SESSION_OK) {
    Serial.println("[ERROR] Session not found");
    return;
  }

  uint8_t appSKey[16], nwkSKey[16];
  memcpy(appSKey, session.appSKey, 16);
  memcpy(nwkSKey, session.nwkSKey, 16);

  printHex(srcID, 8, "[INFO] srcID: ");
  printHex(payload, payloadLength, "[INFO] Payload bytes: ");
  printHex(receivedHMAC, 8, "[INFO] Received HMAC: ");

 SessionStatus Hmac = verifyHmac(buffer, length, receivedHMAC);
  if (Hmac != SESSION_OK) {
  Serial.println("[WARN] HMAC MISMATCH!");
    return;
  }
  Serial.println("[OK] HMAC verified.");

  uint8_t decryptedPayload[payloadLength];
  decryptPayload(appSKey, payload, payloadLength, decryptedPayload);
  
  printHex(decryptedPayload, payloadLength, "[INFO] Decrypted Payload: ");
  String decryptedMessage = "";
  for (size_t i = 0; i < payloadLength; i++) {
    if (decryptedPayload[i] == 0x00) break;
    decryptedMessage += (char)decryptedPayload[i];
  }

  globalReply = decryptedMessage;  // ✅ FIXED this assignment

  Serial.println("[INFO] Message: " + globalReply);

}

// ────── LoRa Incoming Listener ───────────────────────────────
void listenForIncoming() {

  if (receivedFlag) {
    receivedFlag = false;
     
    int packetLength = lora->getPacketLength();
    if (packetLength > 0) {
      uint8_t buffer[255];
      int state = lora->readData(buffer, packetLength);
      handlePacket(buffer, packetLength );
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.printf("[RX] Length: %d\n[RX] Data (hex): ", packetLength);
        for (int i = 0; i < packetLength; i++) {
          if (buffer[i] < 0x10) Serial.print("0");
          Serial.print(buffer[i], HEX);
        }
        Serial.println();
      }
    }
  }
}



