/*
  OpenEdgeStack SX126x - Receive and Handle Sessions Example

  Description:
  This example demonstrates how to reconnect and manage LoRaWAN sessions 
  between end devices and gateways. It is useful when either 
  side loses its session state due to corruption or unexpected errors.

  Key Features:
  - Supports persistent session management for multiple incoming end devices.

  - Enables the gateway to send a rejoin request when the end device becomes corrupted, 
    while the gateway still retains a valid session.

  - Handles cases where a gateway loses its session while the end device continues sending 
    data. In response, the gateway sends its DevEUI along with a MIC for 
    verification. Upon successful validation, the end device can flush its session 
    and reinitiate a new join process with updated keys.

  - Best used in conjunction with the "transmitterSessions" example.

  Requirements:
  - RadioLib library.
  - LoRa module: SX126x (tested with Heltec V3).

  Optional: 
  - SPIFFS mounted for session persistence.
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

  // flushes all sessions
  flushAllSessions();
  
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

      String srcEUI = idToHexString(buffer, 8);
      /*
         Inside of the handler for session check We have flush sessions if a device trys to rejoin and it has a session,
         we can assume the end device got corrurpted and lost its sessions so we can flush it for that device, then inside of the join request
         on the end device we set it wait and retrty a join request after a couple of minutes.
      */
      if (sessionExists(srcEUI)) {
        Serial.println("[JOIN] Already joined: " + srcEUI);
        flushSessionFor(srcEUI);
        return;
      }
  
      Serial.println("[JOIN] Proceeding with new join for " + srcEUI);
      handleJoinRequest(buffer, length);

      } else {
        Serial.println("==== [RX PACKET] ====");

        // ───── Updated Offsets ─────
        uint8_t* srcID = buffer;           // 0–7
        uint8_t* nonce = buffer + 8;       // 8–23 (new!)
        uint8_t* payload = buffer + 24;    // 24–(end - 8)
        uint8_t* receivedHMAC = buffer + length - 8;

        size_t payloadLength = length - 8 /*HMAC*/ - 8 /*srcID*/ - 16 /*nonce*/;

        String srcIDString = idToHexString(srcID);

       /*
          This session check is different from the first one this one runs on
          every incoming packet (not just join requests).

          If we receive a packet and there's no session for that device, we send
          back a response with our gateway's EUI and a 4-byte MIC. This is in
          contrast to the normal 8-byte HMAC used in regular encrypted packets.

          If the sender is a real device that still wants to be part of the session,
          it should have both the gateway's EUI and the 16-byte HMAC key stored.
          It will verify our response, flush its own session, and start a new join.

          This is especially useful if the gateway lost its session with the device
          (e.g. due to a reset), and we can’t physically access the device to reset it.
        */

        SessionInfo session;
        SessionStatus status = verifySession(srcIDString, session);
        if (status != SESSION_OK) {
        uint8_t payload[12];  // 8 bytes for devEUI + 4 bytes MIC
        memcpy(payload, devEUI, 8);

        uint8_t mic[32];
        computeHMAC_SHA256(hmacKey, sizeof(hmacKey), payload, 8, mic);
        memcpy(payload + 8, mic, 4);

        Serial.println("[ERROR] Session not found");

        transmissonFlag = true;
        lora->standby();
        delay(5);

        int result = lora->transmit(payload, sizeof(payload));
        delay(10);
        lora->startReceive();
        transmissonFlag = false;

        if (result == RADIOLIB_ERR_NONE) {
            Serial.println("[ACK] Sent rejoin.");
        } else {
            Serial.println("[ACK] Failed to send ACK.");
        }
        return;
     }


        // ───── Update HMAC Verification to match full buffer ─────
        SessionStatus Hmac = verifyHmac(buffer, length, receivedHMAC);
        if (Hmac != SESSION_OK) {
        Serial.println("[WARN] HMAC MISMATCH!");
          return;
        }
        Serial.println("[OK] HMAC verified.");

        uint8_t appSKey[16];
        memcpy(appSKey, session.appSKey, 16);
        // ───── Use CTR Decryption with Nonce ─────
        uint8_t decryptedPayload[payloadLength];
        decryptPayload(appSKey, nonce, payload, payloadLength, decryptedPayload);

        printHex(decryptedPayload, payloadLength, "[INFO] Decrypted Payload: ");
        String decryptedMessage = "";
        for (size_t i = 0; i < payloadLength; i++) {
          if (decryptedPayload[i] == 0x00) break;
          decryptedMessage += (char)decryptedPayload[i];
        }

        Serial.println("[INFO] Message: " + decryptedMessage);
        }

  // Restart receiver properly
  int rx = lora->startReceive();
  if (rx != RADIOLIB_ERR_NONE) {
    Serial.print("[ERROR] Failed to restart receive: ");
    Serial.println(rx);
  }
}