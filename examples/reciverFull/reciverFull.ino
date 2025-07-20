/*
  OpenEdgeStack SX126x - Advanced Receive Example

  This example demonstrates a full implementation of encrypted data reception
  using OpenEdgeStack with the SX126x LoRa module family.

  Key Features:
  - Supports session management for multiple incoming devices.
  - Decrypts received payloads using the provided AppSKey.
  - Verifies HMAC for data integrity.
  - Automatically responds to join requests and sends acknowledgment flags.
  - Provides structured handling for different data formats (text, bytes, floats).

  Compared to `receiverSimple`, this example offers extended functionality,
  including encryption handling, HMAC verification, and dynamic session handling.

  Packet format:
    [SenderID (8 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]

  Notes:
  - Uses AES-128 for payload decryption.
  - Sends an acknowledgment (ACK) after successful reception.
  - Compatible with other SX126x-based LoRa modules.

*/

#include <OpenEdgeStack.h>

#include <RadioLib.h>
#include <Preferences.h>

#include <FS.h>
#include <SPIFFS.h>
// ───── LoRa Configuration ─────────────────────────────

// LoRa SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY); // Pin configuration
SX1262 radioModule(&module); // Create SX1262 instance

PhysicalLayer* lora = &radioModule; // Set global radio pointer

float frequency_plan = 915.0; // Frequency (in MHz)


// ───── Runtime Globals ────────────────────────────────

uint8_t devEUI[8] = {
  /* your DevEUI */
};  // Device EUI (64-bit)

uint8_t appKey[16] = {
  /* your Appkey */
}; // App root key (AES-128)

const uint8_t hmacKey[16] = {
  /* your hmackey */
}; // Shared 16-byte static HMAC keyy


// ───── Interrupt ──────────────────────────────────────
// Flags used by interrupt handlers and other logic to track received messages
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;


void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}

// ───── Setup ──────────────────────────────────────────
void setup() {
  // Mount SPIFFS to persist sessions across reboots
 if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);  // prevent further operation
  }

  // Start preferences for sessions  
  preferences.begin("lora", false);

  Serial.begin(115200);
  delay(100);

  // Register the chosen radio module globally
  setRadioModule(&radioModule);  
  delay(1000);

  // Initialize the radio module
  Serial.println("[INFO] LoRa Init...");
  int state = radioModule.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("Radio init failed: %d\n", state);
    while (true);
  }
  // Set flags  
  radioModule.setDio1Action(setFlags);
  radioModule.setPacketReceivedAction(setFlags);
  
  // begin listening
  state = radioModule.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive failed: %d\n", state);
    while (true);
  }
  Serial.println("[Setup] Setup complete.");

}

void loop() {

  // Wait until the LoRa module signals a packet has been received
  if (!receivedFlag) return;

  // Clear the flag now that we're handling the packet
  receivedFlag = false;

  // Get the length of the received packet
  int length = lora->getPacketLength();
  if (length <= 0) {
    Serial.println("[RX] No valid packet length.");
    return;
  }

  // Read the data into a buffer
  uint8_t buffer[255];
  int state = lora->readData(buffer, length);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[RX] Error reading data: ");
    Serial.println(state);
    return;
  }

  // ─────────────────────────────────────────────────────────────
  // Handle JoinRequest packets (22 bytes)
  // ─────────────────────────────────────────────────────────────
  if (length == 22) {  
      // Handle the join request and update last response time
      handleJoinIfNeeded(buffer, length);
      
  } else {

      Serial.println("==== [RX PACKET] ====");
      Serial.printf("Total length: %d bytes\n", length);
       printHex(buffer, length, "[RAW] Data: ");
    // ─────────────────────────────────────────────────────────────
    // Handle Regular Encrypted Payload Packets
    // Format: [8-byte SrcID][Payload][8-byte HMAC]
    // ─────────────────────────────────────────────────────────────

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

    printHex(payload, payloadLength, "[INFO] Payload: ");
    printHex(receivedHMAC, 8, "[INFO] Received HMAC: ");

    SessionStatus Hmac = verifyHmac(buffer, length, receivedHMAC);
    if (Hmac != SESSION_OK) {
    Serial.println("[WARN] HMAC MISMATCH!");
      return;
    }

    // Decrypt the payload using the AppSKey
    uint8_t decrypted[payloadLength];
    decryptPayloadWithKey(localAppSKey, nonce, payload, payloadLength, decrypted);

    // Optional Send ack back
    sendDataAck(srcIDString, srcID);
    
    // Optional: print the raw payload in binary format
    printBinaryBits(payload, payloadLength);

    // First byte of decrypted payload = data type
    uint8_t dataType = decrypted[0];
    uint8_t* data = decrypted + 1;
    size_t dataLength = payloadLength - 1;

    // ───── Handle different types of decrypted payloads ─────
    switch (dataType) {
      case TYPE_TEXT: {
        // Decode printable text; replace 0x01 with space
        String msg;
        for (size_t i = 0; i < dataLength; i++) {
          char c = (char)data[i];
          if (c == 0x01) msg += ' ';
          else if (isPrintable(c)) msg += c;
        }
        Serial.println("[DECRYPTED] Text: " + msg);
        break;
      }

      case TYPE_BYTES:
        // Print raw bytes in hexadecimal
        printHex(data, dataLength, "[DECRYPTED] Bytes: ");
        Serial.print("HEX | ");
        for (size_t i = 0; i < dataLength; i++) Serial.printf("0x%02X ", data[i]);
        Serial.println();
        break;

      case TYPE_FLOATS:  {
        // Interpret data as floats
        size_t floatCount = dataLength / sizeof(float);
        for (size_t i = 0; i < floatCount; i++) {
          float val;
          memcpy(&val, &data[i * sizeof(float)], sizeof(float));
          Serial.printf("[DECRYPTED] Float[%d]: %.2f\n", i, val);
        }
        break;
      }

      default:
        // Unknown or unsupported data type
        Serial.printf("[WARN] Unknown data type: 0x%02X\n", dataType);
    }
  }

  // Always return to RX mode to wait for the next packet
  lora->startReceive();
}