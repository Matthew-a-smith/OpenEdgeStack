/*
  OpenEdgeStack SX126x Transmit with Groups Example

  This example demonstrates how to transmit structured groups of data 
  over raw LoRa using the SX126x module family.

  Each group is stored on the device as an encrypted binary blob, referred to as a "group".
  The example sets up group configuration parameters, generates sample data, 
  and transmits it over LoRa using a secure and lightweight custom format.

  Notes:
  - Data is encrypted using appSKey before transmission.
  - The transmitted packet format is:
      [SenderID (8 bytes)] + [Encrypted Group Data] + [HMAC (8 bytes)]

  - Other SX126x family modules are supported.

  Ideal for demonstrating secure, chunked transmissions with minimal LoRaWAN overhead.
*/

#include <EndDevice.h>
#include <LoraWANLite.h>

// SENDER
#include <RadioLib.h>
#include <Wire.h>



// LoRa SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

#define BUTTON_PIN 7  // Change as needed
#define LED_PIN    6 // Change as needed



float frequency_plan = 915.0;
SX1262 lora = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

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
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(BUTTON_PIN);

  Serial.println("[INFO] LoRa Init...");
  int state = lora.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("[ERROR] LoRa Init FAIL");
    while (true);
  }

  int maxRetries = 3;
  int attempts = 2;
  int timeout = 3000;
  sendJoinRequest(maxRetries, timeout, attempts);  

  lora.setDio1Action(setFlags);
  lora.setPacketReceivedAction(setFlags);  
  state = lora.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive failed: %d\n", state);
    while (true);
  }
}

void loop() {
  listenForIncoming();  // Process incoming packets (non-blocking)

  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    Serial.println("[BUTTON] Press detected. Sending...");

    String groupOne = "this is a test sentence up to and over 16 bytes in length";
    sendLora((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT);

    Serial.println("[TX] Data sent!");
  }

  globalReply = "";  // Clear reply after handling
  lastButtonState = currentButtonState;
  delay(50);  // Debounce
}

