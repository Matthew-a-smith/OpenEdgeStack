#include <EndDevice.h>
#include <LoraWANLite.h>
#include <Sessions.h>

// SENDER
#include <RadioLib.h>
#include <Wire.h>
#include <SHA256.h>
#include "mbedtls/md.h"
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
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);
  }
  Serial.begin(115200);

  Serial.println("Sender ID: " + devAddr);
  Serial.println("[DEBUG] Starting setup...");

  randomSeed(analogRead(0));

  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.begin(115200);
  Serial.println("[INFO] LoRa Init...");

 
  sessionMap.clear();
  Serial.println("[DEBUG] Cleared RAM session map.");
  preferences.begin("lora", false);
  preferences.clear();
  preferences.end();
  Serial.println("[NVS] All sessions cleared from NVS.");

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

float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2.0;
}

void loop() {
  listenForIncoming();

  float distance = measureDistance();
  delay(1500);
  if (distance > 20.0) {
    Serial.println("\n--- Distance Triggered Send ---");
    Serial.println("[INFO] Distance: " + String(distance, 1) + " cm");
    Serial.println("ID: " + devAddr);
    Serial.println("Dist: " + String(distance, 1) + " cm");

    uint8_t distanceBytes[4];
    memcpy(distanceBytes, &distance, sizeof(float));
 
    // This waits 5 seconds (5000 ms) before sending
    pollLora(distanceBytes, sizeof(distanceBytes), TYPE_FLOATS, 5000);
  } else {
    Serial.println("ID: " + devAddr);
    Serial.println("Dist: " + String(distance, 1) + " cm");
    Serial.println("Too close (<20cm)");
  }
  delay(100);
}



