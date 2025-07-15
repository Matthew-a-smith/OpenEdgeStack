#include <LoraWANLite.h>

#include <RadioLib.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <map>


// ───── LoRa Configuration ─────────────────────────────
#define LORA_CS     8
#define LORA_RST    12
#define LORA_BUSY   13
#define LORA_DIO1   14

SX1262 lora = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
float frequency_plan = 915.0;


// ───── Runtime Globals ────────────────────────────────
String devAddr = String((uint64_t)ESP.getEfuseMac(), HEX);


uint8_t devEUI[8] = {
  0xC5, 0x80, 0x98, 0x92, 0x31, 0x35, 0x00, 0x00
};

uint8_t appKey[16] = {
  0x2A, 0xC3, 0x76, 0x13, 0xE4, 0x44, 0x26, 0x50,
  0x2B, 0x8D, 0x7E, 0xEE, 0xAB, 0xA9, 0x57, 0xCD
};

// Shared 16-byte static HMAC key
const uint8_t hmacKey[16] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
  0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xDE, 0xAD
};


 void flushSession() {
   sessionMap.clear();
   Serial.println("[MEM] All sessions cleared from RAM.");
   preferences.begin("lora", false);
   preferences.clear();
   preferences.end();
   Serial.println("[NVS] All sessions cleared from NVS.");
   if (SPIFFS.exists("/recv_chunk.bin")) {
   if (SPIFFS.remove("/recv_chunk.bin")) {
     Serial.println("[CLEANUP] Removed /recv_chunk.bin");
   } else {
     Serial.println("[ERROR] Failed to remove /recv_chunk.bin");
   }
 } else {
   Serial.println("[CLEANUP] No /recv_chunk.bin to remove");
 }
 
 }

// ───── Interrupt ──────────────────────────────────────
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}

// ───── Setup ──────────────────────────────────────────
void setup() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS Mount Failed");
    while (true);  // prevent further operation
  }
  Serial.begin(115200);
  delay(100);
//  flushSession();
  sessionMap.clear();
  Serial.println("[DEBUG] Cleared RAM session map.");
  preferences.begin("lora", false);  // open for lifetime
  preferences.clear();
   preferences.end();
   Serial.println("[NVS] All sessions cleared from NVS.");

  delay(1000);
  // LoRa
  int state = lora.begin(frequency_plan);
  if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] LoRa Init FAIL: %d\n", state);
    while (true);
  }
  
  lora.setDio1Action(setFlags);
  lora.setPacketReceivedAction(setFlags);  
  state = lora.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa] startReceive failed: %d\n", state);
    while (true);
  }

  Serial.println("[Setup] Setup complete.");
}


void loop() {
  static unsigned long lastJoinResponseTime = 0;

  if (!receivedFlag) return;
  receivedFlag = false;

  int packetLength = lora.getPacketLength();
  if (packetLength <= 0) {
    Serial.println("[RX] No valid packet length.");
    return;
  }

  uint8_t buffer[255];
  int state = lora.readData(buffer, packetLength);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[RX] Error reading data: ");
    Serial.println(state);
    return;
  }

  // Debounce JoinAccept TX
  if (millis() - lastJoinResponseTime < 4000) {
    Serial.println("[RX] Ignored packet due to recent JoinAccept TX.");
    return;
  }

  // Route packet
  if (packetLength == 18) {
    handleJoinIfNeeded(buffer, packetLength);
    lastJoinResponseTime = millis();
  } else {
    handleLoRaPacket(buffer, packetLength);
  }

  // Restart receiver properly
  int rx = lora.startReceive();
  if (rx != RADIOLIB_ERR_NONE) {
    Serial.print("[ERROR] Failed to restart receive: ");
    Serial.println(rx);
  } 
  delay(5);
}