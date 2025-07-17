/*
  OpenEdgeStack SX126x - Simple Transmit Example

  This example demonstrates how to send a single encrypted LoRa packet 
  using the SX126x module family when a button is pressed.

  Notes:
  - Data is encrypted using the AppSKey before transmission.
  - The transmitted packet format is:
      [SenderID (8 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]
  - Compatible with other SX126x family modules.
*/

#include <EndDevice.h>
#include <LoraWANLite.h>
#include <Sessions.h>

// SENDER
#include <RadioLib.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <map>



// LoRa SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

#define BUTTON_PIN 0  // Change as needed
#define LED_PIN    6 // Change as needed



Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
SX1262 radioModule(&module);

PhysicalLayer* lora = &radioModule;

float frequency_plan = 915.0;

uint8_t devEUI[8] = {
  0x4F, 0x65, 0x75, 0xC5, 0xF0, 0x31, 0x00, 0x00
};  // Device EUI (64-bit)

uint8_t appEUI[8] = {
  0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
}; // Application EUI (64-bit)

uint8_t appKey[16] = {
  0x2A, 0xC3, 0x76, 0x13, 0xE4, 0x44, 0x26, 0x50,
  0x2B, 0x8D, 0x7E, 0xEE, 0xAB, 0xA9, 0x57, 0xCD
}; // App root key (AES-128)

const uint8_t hmacKey[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xDE, 0xAD
}; // Shared 16-byte static HMAC key


// -------------------- State Flags -----------------------

// Tracks acknowledgment (ACK) responses between the end device and gateway.
// Declared globally to be accessible across all functions.
String globalReply = "";

// Flags used by interrupt handlers and other logic to track received messages
// and outgoing transmissions. Required when using the receiver with an end device.
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}
// ------------------- Application State ------------------
// Tracks the current and previous button states for edge detection.
int buttonState = 0;
int lastButtonState;

// Flags to track whether each group has already been transmitted.
bool firstPressDone = false;

bool awaitingAckForGroup1 = false;

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

  // IMPORTANT: Send join request AfTER enabling receive mode
  int maxRetries = 3; //Number of retries
  int retryDelay = 3000; //Timeout per attempt in milliseconds
  sendJoinRequest(maxRetries, retryDelay);  // Wait for session handshake
}

void loop() {
  listenForIncoming();  // Process incoming packets (non-blocking)

  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    Serial.println("<--------------------------------->");
    Serial.println("[BUTTON] Press detected. Sending...");

    String groupOne = "this is a test sentence up to and over 16 bytes in length";
    sendLora((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT);

    Serial.println("[TX] Data sent!");
  }

  globalReply = "";  // Clear reply after handling
  lastButtonState = currentButtonState;
  delay(50);  // Debounce
}
