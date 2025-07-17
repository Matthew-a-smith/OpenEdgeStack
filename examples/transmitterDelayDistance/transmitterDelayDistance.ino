/*
  OpenEdgeStack SX126x - Transmit with Groups Example

  This example demonstrates how to transmit encrypted packets using the SX126x
  LoRa module family and OpenEdgeStack.

  Functionality:
  - Uses an ultrasonic distance sensor to monitor distance in cm.
  - When the measured distance exceeds 20 cm, the value is read, encrypted,
    and transmitted over LoRa.
  - Utilizes `pollLora()` with a 3-second timeout for sending.
  
  Notes:
  - Data is encrypted using AppSKey before transmission.
  - The transmitted packet format is:
      [SenderID (8 bytes)] + [Encrypted Group Data] + [HMAC (8 bytes)]
  - Compatible with all SX126x family LoRa modules.
*/

#include <EndDevice.h>
#include <LoraWANLite.h>
#include <Sessions.h>

#include <RadioLib.h>
#include <Wire.h>
#include <FS.h>
#include <SPIFFS.h>

// Lora SX1262 pins for Heltec V3
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

// Ultrasonic sensor pins
#define TRIG_PIN 7
#define ECHO_PIN 6

float frequency_plan = 915.0;
SX1262 lora = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

uint8_t devEUI[8] = {
  0x4F, 0x65, 0x75, 0xC5, 0xF0, 0x31, 0x00, 0x00
};

uint8_t appEUI[8] = {
  0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80
};

uint8_t appKey[16] = {
  0x2A, 0xC3, 0x76, 0x13, 0xE4, 0x44, 0x26, 0x50,
  0x2B, 0x8D, 0x7E, 0xEE, 0xAB, 0xA9, 0x57, 0xCD
};

const uint8_t hmacKey[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xDE, 0xAD
};

String devAddr = String((uint64_t)ESP.getEfuseMac(), HEX);

String globalReply = "";
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}

void setup() {
 
  randomSeed(analogRead(0));

  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

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
  int maxRetries = 3 //Number of retries
  int retryDelay = 3000 //Timeout per attempt in milliseconds
  sendJoinRequest(maxRetries, retryDelay);  // Wait for session handshake   
}

float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2.0;
  delay(2000);
}

void loop() {
  listenForIncoming();

  float distance = measureDistance();
  if (distance > 20.0) {
    Serial.println("\n--- Distance Triggered Send ---");
    Serial.println("[INFO] Distance: " + String(distance, 1) + " cm");
    Serial.println("ID: " + devAddr);
    Serial.println("Dist: " + String(distance, 1) + " cm");

    uint8_t distanceBytes[4];
    memcpy(distanceBytes, &distance, sizeof(float));
 
    // This waits 5 seconds (5000 ms) before sending
    pollLora(distanceBytes, sizeof(distanceBytes), TYPE_FLOATS, 3000);
  } else {
    Serial.println("ID: " + devAddr);
    Serial.println("Dist: " + String(distance, 1) + " cm");
    Serial.println("Too close (<20cm)");
  }
  delay(100);
}



