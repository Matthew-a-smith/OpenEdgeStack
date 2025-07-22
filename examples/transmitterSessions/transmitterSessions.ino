/*
  OpenEdgeStack SX126x - Session Reset with MIC Verification

  This example demonstrates how to recover LoRaWAN sessions between end devices and gateways.
  When a device loses connection due to gateway corruption or network error, it can listen
  for a special MIC-authenticated command from the gateway to flush its session and rejoin.

  Key Features:
  - Uses the shared HMAC-SHA256 key to verify commands from the gateway.

  - Listens for 12-byte packets: [8 bytes GatewayEUI] + [4 bytes MIC].

  - Upon successful MIC and EUI verification, clears the local session and sends a new JoinRequest.

  - Includes a simple text message sender using button press

  - Best used with "reciverSessions" Example.

  Requirements:
  - RadioLib library.
  - LoRa module: SX126x (tested with Heltec V3).

  Optional: 
  - SPIFFS mounted for session persistence.
*/


#include <OpenEdgeStack.h>

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


Module module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY); // Pin configuration
SX1262 radioModule(&module); // Create SX1262 instance

PhysicalLayer* lora = &radioModule; // Set global radio pointer

float frequency_plan = 915.0; // Frequency (in MHz)

/*
  -------------------------------------------------------------------
  IMPORTANT: Uploading a sketch without valid keys will result in a compile error.
  
  The key arrays below have been intentionally commented out to prevent the
  use of default or weak keys. This measure ensures that users must provide
  unique and secure keys before compiling.

  Each device must be provisioned with its own cryptographic keys to
  securely communicate over LoRa.

  You have two options for generating these keys:

  1) Use the provided Python script `generate_keys.py` located in the 'extras' folder.
     This script outputs keys as C-style arrays ready to be copied here.
     Rember the app and hmacKey get shared between devices.
     Use gatewayEUI in the python script as the secnd devEUI or vice versa.

  2) Use The Things Network (TTN) to generate compatible device credentials,
     then manually paste those values into the arrays below.
  -------------------------------------------------------------------
*/

// ───── Runtime Globals ────────────────────────────────

// uint8_t devEUI[8] = {
//   /* your devEUI */
// };  // Device EUI (64-bit)

// uint8_t appEUI[8] = {
//   /* your AppEUI */
// }; // Application EUI (64-bit)

// uint8_t appKey[16] = {
//   /* your appKEY */  
// }; // AppKey (AES-128)

// const uint8_t hmacKey[16] = {
//    /* yourHMAC key */
// }; // Shared HMAC key (16 bytes)

// uint8_t GatewayEUI[8] = {
//   /* your Gateways EUI */
// }; // GatewayEUI (64-bit)

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

  // Start preferences for sessions  
  preferences.begin("lora", false);

  // set pins for buttons to prevent sending at start
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  lastButtonState = digitalRead(BUTTON_PIN);

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
  // Check if a LoRa packet was received (via ISR flag)
  if (receivedFlag) {
    receivedFlag = false;

    int packetLength = lora->getPacketLength();
    if (packetLength > 0) {
      uint8_t buffer[255];  // Max LoRa buffer
      int state = lora->readData(buffer, packetLength);

      Serial.println("==== [RX PACKET] ====");

      if (state == RADIOLIB_ERR_NONE) {
        Serial.printf("[RX] Length: %d\n[RX] Data (hex): ", packetLength);
        for (int i = 0; i < packetLength; i++) {
          if (buffer[i] < 0x10) Serial.print("0");
          Serial.print(buffer[i], HEX);
        }
        Serial.println();
      }

      /*
      Looks for a special 12-byte packet: [8-byte EUI] + [4-byte MIC]
      This is used for when the gateway loses is session with the device and the device does not know.
      the gateway will recive the data if their is no session asscoited with this deivce then it sends down its
      GatewaysEUI and the 4 byte MIC we check for that and then flush our devices sessions and send a new request to the gateway.
      */
      if (packetLength == 12) {
        uint8_t* srcID = buffer;         // First 8 bytes: GatewayEUI
        uint8_t* receivedMIC = buffer + 8; // Last 4 bytes: HMAC-SHA256 MIC

        // Verify the MIC using the shared key
        if (!verifyMIC(buffer, packetLength, receivedMIC)) {
          Serial.println("[JOIN] MIC verification failed. Ignoring JoinRequest.");
          return;
        }

        Serial.println("[JOIN] MIC verified successfully.");

        // Compare EUI in the packet with our gateway EUI
        if (memcmp(srcID, GatewayEUI, 8) == 0) {
          // Use your own logic for flushing session
          String srcIDString = idToHexString(devEUI);  // Keep as-is per your structure
          flushSessionFor(srcIDString);

          printHex(srcID, 8, "[INFO] Cmd From Gateway to flush session: ");

          // Send a JoinRequest to reestablish session
          sendJoinRequest(3, 3000);  // Retries = 3, delay = 3000 ms
        } else {
          Serial.println("[JOIN] EUI mismatch. Not for this gateway.");
        }
      }
    }
  }

  // Button press handler for sending a test message
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    Serial.println("<--------------------------------->");
    Serial.println("[BUTTON] Press detected. Sending...");

    String groupOne = "this is a test sentence up to and over 16 bytes in length";

    // Get current session for this devEUI
    String devEUIHex = idToHexString(devEUI);
    SessionInfo session;
    SessionStatus status = verifySession(devEUIHex, session);
    if (status != SESSION_OK) {
      Serial.println("[ERROR] Session not found");
      return;
    }

    // Build the payload: 1 byte type header + message
    size_t totalLen = groupOne.length() + 1;
    uint8_t* packetData = new uint8_t[totalLen];
    packetData[0] = 0x01; // TYPE_TEXT
    memcpy(packetData + 1, (const uint8_t*)groupOne.c_str(), groupOne.length());

    // Encrypt and wrap with session info
    size_t finalLen = 0;
    uint8_t* finalPacket = encryptAndPackage(packetData, totalLen, session, finalLen, devEUI);

    // Transmit encrypted payload
    transmissonFlag = true;
    lora->standby();
    delay(5);
    int result = lora->transmit(finalPacket, finalLen);
    delay(10);
    lora->startReceive();
    transmissonFlag = false;

    if (result == RADIOLIB_ERR_NONE) {
      Serial.println("[ACK] Sent successfully.");
    } else {
      Serial.println("[ACK] Failed to send ACK.");
    }

    // Clean up heap memory
    delete[] finalPacket;
    delete[] packetData;
  }

  globalReply = "";  // Clear any leftover state
  lastButtonState = currentButtonState;
  delay(50); // Basic debounce
}
