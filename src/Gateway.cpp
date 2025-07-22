//lLite

#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>

#include "Gateway.h"
#include "CryptoUtils.h"
#include "Sessions.h"
#include "EndDevice.h"



void setRadioModule(PhysicalLayer* module) {
  lora = module;
  
}
// ────── LoRa Communication ──────────────────────────────────────────

// ────── Payload Layout Before Encryption ──────
// Offset | Size          | Field       | Description
// -------|---------------|-------------|------------------------------
// 0      | 8             | Sender ID        | Sender devEUI in byte format
// 8      | N (padded)    | AES Encrypted    | ACK encrypted with appSKey
// 8+N    | 8             | HMAC             | First 8 bytes of HMAC-SHA256
// 
//
// Notes:
// - Sends encyrtped ack back to sender for each  response to indicate succefull transmission
// - Data is encrypted with `appSKey` before sending
// - Transmitted buffer is: [SenderID (8)] + [ACK] + [HMAC (8)]
// - Final format handled by `encryptAndPackage()`

void sendDataAck(const String& srcID, uint8_t* SenderID) {
  SessionInfo session;
  SessionStatus status = verifySession(srcID, session);
  if (status != SESSION_OK) {
    Serial.println("[ERROR] Session not found");
    return;
  }

  String payload = "ACK:";
  size_t finalLen = 0;

  uint8_t* finalPacket = encryptAndPackage((const uint8_t*)payload.c_str(), payload.length(), session, finalLen, SenderID);
  sender(finalPacket, finalLen);
}


struct JoinAccept {
  uint32_t devAddr;
  uint8_t joinNonce[3];
  uint8_t netID[3];
  // other fields like RxDelay, DLSettings can go here
};

// ────── JoinRequest Packet Layout (18 bytes, unencrypted) ──────
// Offset | Size | Field       | Description
// -------|------|-------------|------------------------------
// 0      | 8    | DevEUI      | Unique device identifier
// 8      | 8    | AppEUI      | Application identifier
// 16     | 2    | DevNonce    | Random value from device


// Function Output:
// - Process a 18-byte unencrypted JoinRequest packet
// - Derive session keys (AppSKey, NwkSKey)
// - Store session info indexed by DevEUI
// - Send back a 16-byte encrypted JoinAccept packet with session identifiers

 // Function Output: Derives and stores appSKey and nwkSKey in `SessionInfo`
void handleJoinRequest(uint8_t* buffer, size_t len) {
  if (len != 22) {
    Serial.println("[JOIN] Invalid JoinRequest size");
    return;
  }
  
  printHex(buffer + 18, 4, "[INFO] Received MIC: ");
  // Verify MIC (last 4 bytes of packet)
  if (!verifyMIC(buffer, len, buffer + 18)) {
    Serial.println("[JOIN] MIC verification failed. Ignoring JoinRequest.");
    return;
  }

  // Parse fields after MIC is verified
  uint8_t devEUI[8], appEUI[8], devNonce[2];
  memcpy(devEUI, buffer, 8);
  memcpy(appEUI, buffer + 8, 8);
  memcpy(devNonce, buffer + 16, 2);

  uint8_t joinNonce[3];
  uint32_t rnd = esp_random();
  joinNonce[0] = rnd & 0xFF;
  joinNonce[1] = (rnd >> 8) & 0xFF;
  joinNonce[2] = (rnd >> 16) & 0xFF;


  uint32_t devAddr = esp_random();  // assign a device address
  uint8_t netID[3] = { 0x01, 0x23, 0x45 };  // your network ID

  // Derive session keys (local only)
  uint8_t appSKey[16], nwkSKey[16];
  deriveSessionKey(appSKey, 0x02, appKey, joinNonce, netID, devNonce);
  deriveSessionKey(nwkSKey, 0x01, appKey, joinNonce, netID, devNonce);

  // Build JoinAccept payload (16 bytes for AES)
  uint8_t payload[16] = {0};
  memcpy(payload, &devAddr, 4);
  memcpy(payload + 4, joinNonce, 3);
  memcpy(payload + 7, netID, 3);
  memcpy(payload + 10, devNonce, 2);  // <-- ADD THIS

  // optionally set payload[10+] = RxDelay, DLSettings
  // Build session and store it
  SessionInfo session;
  session.devAddr = devAddr;
  memcpy(session.appSKey, appSKey, 16);
  memcpy(session.nwkSKey, nwkSKey, 16);
  memcpy(session.joinNonce, joinNonce, 3);
  memcpy(session.netID, netID, 3);
  memcpy(session.devNonce, devNonce, 2);  

  Serial.println("[JOIN] Session keys derived successfully.");

  String devEUIHex = idToHexString(devEUI);
  storeSessionFor(devEUIHex, session);

  // NOTE on AES encrypt/decrypt usage for JoinAccept:

  // According to LoRaWAN spec, the JoinAccept message encryption is done by performing
  // an AES **decrypt** operation with the AppKey on the server/network side. 
  // This means to use `aes128_decrypt_block()` to *encrypt* the JoinAccept payload before sending.
  // Both sides use AES-ECB mode and the same key, so this approach works correctly

  uint8_t encryptedPayload[16];
  aes128_decrypt_block(appKey, payload, encryptedPayload); 

  transmissonFlag = true;
  lora->standby();
  delay(5);
  lora->transmit(encryptedPayload, sizeof(encryptedPayload));
  delay(10);                      
  int rxState = lora->startReceive();
  transmissonFlag = false;
  Serial.println("[JOIN] Sent encrypted JoinAccept");
}

bool isJoinRequest(size_t length) {
  return length == 18;
}

// ────── Normal Uplink Packet Layout (variable length) ──────
// Offset | Size         | Field        | Description
// -------|--------------|--------------|------------------------------
// 0      | 8            | srcID        | Device unique ID
// 8      | N (len-16)   | Payload      | Encrypted data content
// len-8  | 8            | HMAC         | Message authentication tag


void handleLoRaPacket(uint8_t* buffer, size_t length) {
  if (length <= 18) {
    Serial.println("[ERROR] Packet too small or JoinRequest size - ignoring in handleLoRaPacket");
    return;
  }

 Serial.println("==== [RX PACKET] ====");
  Serial.printf("Total length: %d bytes\n", length);
  printHex(buffer, length, "[RAW] Data: ");

  // ───── Updated Offsets ─────
  uint8_t* srcID = buffer;           // 0–7
  uint8_t* nonce = buffer + 8;       // 8–23 (new!)
  uint8_t* payload = buffer + 24;    // 24–(end - 8)
  uint8_t* receivedHMAC = buffer + length - 8;

  size_t payloadLength = length - 8 /*HMAC*/ - 8 /*srcID*/ - 16 /*nonce*/;

  String srcIDString = idToHexString(srcID);

  SessionInfo session;
  SessionStatus status = verifySession(srcIDString, session);
  if (status != SESSION_OK) {
    Serial.println("[ERROR] Session not found");
    return;
  }
  
  uint8_t localAppSKey[16], localNwkSKey[16];
  memcpy(localAppSKey, session.appSKey, 16);

  memcpy(localNwkSKey, session.nwkSKey, 16);

  printHex(srcID, 8, "[INFO] Source ID: ");
  printHex(payload, payloadLength, "[INFO] Payload: ");
  printHex(receivedHMAC, 8, "[INFO] Received HMAC: ");

  if (verifyHmac(buffer, length, receivedHMAC) != SESSION_OK) {
    Serial.println("[WARN] HMAC MISMATCH!");
    return;
  }
  Serial.println("[OK] HMAC verified.");

  Serial.println("========== DECRYPTED DATA ==========");
  
  uint8_t decryptedPayload[payloadLength];
  decryptPayload(localAppSKey, nonce, payload, payloadLength, decryptedPayload);
  printHex(decryptedPayload, payloadLength, "[INFO] Decrypted Payload: ");
 
 
  Serial.println("[INFO] Raw Binary:");
  for (size_t i = 0; i < payloadLength; i++) {
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
  if (payloadLength % 8 != 0) {
    Serial.println();
  }

  uint8_t dataType = decryptedPayload[0]; // First byte = type
  uint8_t* data = decryptedPayload + 1;   // Actual payload starts here
  size_t dataLength = payloadLength - 1;

  Serial.printf("[INFO] Data Type: 0x%02X\n", dataType);

  switch (dataType) {
    case TYPE_TEXT: {
    String message = "";
    for (size_t i = 0; i < dataLength; i++) {
      char c = (char)data[i];
      if (c == 0x01) {
        message += ' ';
      } else if (isPrintable(c)) {
        message += c;
      }
      // else skip unprintable characters
    }
    Serial.println("[DECRYPTED] Text: " + message);
    break;
  }


    case TYPE_BYTES: {
      printHex(data, dataLength, "[DECRYPTED] Bytes: ");
      Serial.print("HEX | ");
      for (size_t i = 0; i < dataLength; i++) Serial.printf("0x%02X ", data[i]);
      Serial.println();
      break;
    }


    case TYPE_FLOATS: {
      if (dataLength % sizeof(float) != 0) {
        Serial.printf("[WARN] Float payload not aligned (%d bytes)\n", dataLength);
      }
    
      size_t floatCount = dataLength / sizeof(float);
      for (size_t i = 0; i < floatCount; i++) {
        float val;
        memcpy(&val, &data[i * sizeof(float)], sizeof(float));
        Serial.printf("[DECRYPTED] Float[%d]: %.2f\n", i, val);
      }
    
      size_t leftover = dataLength % sizeof(float);
      if (leftover) {
        Serial.printf("[INFO] Ignoring %d leftover bytes\n", leftover);
      }
      break;
    }

    default:
      Serial.printf("[WARN] Unknown data type: 0x%02X\n", dataType);
      break;
  }

  Serial.println("====================\n");

  }


void handleJoinIfNeeded(uint8_t* buffer, size_t len) {
  String srcEUI = idToHexString(buffer, 8);

  if (sessionExists(srcEUI)) {
    Serial.println("[JOIN] Already joined: " + srcEUI);
    return;
  }

  Serial.println("[JOIN] Proceeding with new join for " + srcEUI);
  handleJoinRequest(buffer, len);
}


void Recive() {
  static unsigned long lastJoinResponseTime = 0;

  if (!receivedFlag) return;
  receivedFlag = false;

  int packetLength = lora->getPacketLength();
  if (packetLength <= 0) {
    Serial.println("[RX] No valid packet length.");
    return;
  }

  uint8_t buffer[255];
  int state = lora->readData(buffer, packetLength);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[RX] Error reading data: ");
    Serial.println(state);
    return;
  }

  // Route packet
  if (packetLength == 22) {
    handleJoinIfNeeded(buffer, packetLength);
  } else {
    handleLoRaPacket(buffer, packetLength);
  }

  // Restart receiver properly
  int rx = lora->startReceive();
  if (rx != RADIOLIB_ERR_NONE) {
    Serial.print("[ERROR] Failed to restart receive: ");
    Serial.println(rx);
  }
}


void decryptPayloadWithKey(uint8_t* appSKey, uint8_t* nonce, uint8_t* payload, size_t payloadLength, uint8_t* out) {
  decryptPayload(appSKey, nonce, payload, payloadLength, out);
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

  










