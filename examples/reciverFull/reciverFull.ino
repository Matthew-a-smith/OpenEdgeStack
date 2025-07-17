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

Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
SX1262 radioModule(&module);

PhysicalLayer* lora = &radioModule;

float frequency_plan = 915.0;


// ───── Runtime Globals ────────────────────────────────

uint8_t devEUI[8] = {
  0xC5, 0x80, 0x98, 0x92, 0x31, 0x35, 0x00, 0x00
}; // Device EUI (64-bit)

uint8_t appKey[16] = {
  0x2A, 0xC3, 0x76, 0x13, 0xE4, 0x44, 0x26, 0x50,
  0x2B, 0x8D, 0x7E, 0xEE, 0xAB, 0xA9, 0x57, 0xCD
}; // App root key (AES-128)

const uint8_t hmacKey[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xDE, 0xAD
}; // Shared 16-byte static HMAC key


// ───── Interrupt ──────────────────────────────────────
// Flags used by interrupt handlers and other logic to track received messages
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

// Interupt flag to avoid replay issues with joins
unsigned long lastJoinResponseTime = 0;

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

void printHexChars(const uint8_t* data, size_t len, const char* label) {
  Serial.print(label);
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

String idToString(uint8_t* id, size_t len) {
  String out = "";
  for (int i = 0; i < len; i++) {
    if (id[i] < 0x10) out += "0";
    out += String(id[i], HEX);
  }
  return out;
}

void loop() {
  // Static variable to track the last time we sent a JoinAccept response
  // Used to prevent responding too frequently (i.e., debounce)
  static unsigned long lastJoinResponseTime = 0;

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
      lastJoinResponseTime = millis();
  } else {
    // ─────────────────────────────────────────────────────────────
    // Handle Regular Encrypted Payload Packets
    // Format: [8-byte SrcID][Payload][8-byte HMAC]
    // ─────────────────────────────────────────────────────────────

    uint8_t* srcID = buffer;                 // First 8 bytes = device ID
    uint8_t* payload = buffer + 8;           // Encrypted payload starts after ID
    uint8_t* receivedHMAC = buffer + length - 8;  // Last 8 bytes = HMAC
    size_t payloadLength = length - 16;      // Total minus SrcID and HMAC

    // Convert device ID to string to look up session
    String srcIDString = idToString(srcID, 8);

    // Attempt to retrieve stored session keys for this device
    SessionInfo session;
    SessionStatus status = verifySession(srcIDString, session);
    if (status != SESSION_OK) {
      Serial.println("[ERROR] Session not found");
      return;
    }

    // Copy keys to temporary buffers for local use
    uint8_t localAppSKey[16], localNwkSKey[16];
    memcpy(localAppSKey, session.appSKey, 16);
    memcpy(localNwkSKey, session.nwkSKey, 16);

    // Debug print the packet segments
    printHexChars(srcID, 8, "[INFO] Source ID: ");
    printHexChars(payload, payloadLength, "[INFO] Payload: ");
    printHexChars(receivedHMAC, 8, "[INFO] Received HMAC: ");

    // Validate HMAC against full packet contents
    if (verifyHmac(buffer, length, receivedHMAC) != SESSION_OK) {
      Serial.println("[WARN] HMAC MISMATCH!");
      return;
    }

    Serial.println("[OK] HMAC verified.");

    // Decrypt the payload using the AppSKey
    uint8_t decrypted[payloadLength];
    decryptPayloadWithKey(localAppSKey, payload, payloadLength, decrypted);

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
        printHexChars(data, dataLength, "[DECRYPTED] Bytes: ");
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

