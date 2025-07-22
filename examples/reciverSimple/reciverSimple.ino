/*
  OpenEdgeStack SX126x simple recive Example

  This examples recives and decrypts incomeing packets it also checks for new join
  requests, that are 22 bytes in length and proccess them correctly.

  Notes:
  - Data is Decrypted using appSKey.
  - Once recived it sends back out a ack flag to the sender

  The Recived packet format is:
      [SenderID (8 bytes)] + [Nonce (16 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]

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

// uint8_t appKey[16] = {
//   /* your appKEY */  
// }; // AppKey (AES-128)

// const uint8_t hmacKey[16] = {
//    /* yourHMAC key */
// }; // Shared HMAC key (16 bytes)


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
  //Recive and handle incomeing packets
  Recive();  
  delay(5);
}