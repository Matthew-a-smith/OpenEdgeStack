/*
  OpenEdgeStack SX126x simple recive Example

  This examples recives and decrypts incomeing packets it also checks for new join
  requests, that are 18 bytes in length and proccess them correctly.

  Notes:
  - Data is Decrypted using appSKey.
  - Once recived it sends back out a ack flag to the sender
  The Recived packet format is:
      [SenderID (8 bytes)] + [Encrypted Group Data] + [HMAC (8 bytes)]

  - Other SX126x family modules are supported.

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

void loop() {
  Recive();  
  delay(5);
}