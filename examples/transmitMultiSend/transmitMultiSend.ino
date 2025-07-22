/*
  OpenEdgeStack SX126x – Transmit with Groups Example

  This example demonstrates how to transmit encrypted packets using the SX126x
  LoRa module family and the OpenEdgeStack encryption library.

  Functionality:
  - Uses an ultrasonic sensor to measure distance in centimeters.
  - When distance is between 15 cm and 20 cm, the reading is stored in a group.
  - When distance exceeds 20 cm:
      • The current reading is stored and sent.
      • All previously grouped readings are transmitted.
  - Utilizes group-based packet storage and encryption.

  Notes:
  - Data is encrypted using the AppSKey before transmission.
  - Transmitted packet format:
      [SenderID (8 bytes)] + [Nonce (16 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]
  - Compatible with all SX126x family LoRa modules.
*/

#include <OpenEdgeStack.h>

#include <RadioLib.h>
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


/*
group settings  can be change depending on size requirements
maxFileSize applies only to the raw stored group files on the sender  
not to the final encrypted and transmitted LoRa packets, which will grow due to 
padding, metadata, and HMAC.

 PacketGroup:
   - maxFileSize       → Max bytes allowed per group file
   - groupLimit           → Max total group files per prefix
   - groupPrefixLimit    → Max unique group name prefixes allowed (Grp1, Grp1.1...)
*/

GroupConfig groupConfig = {
    .maxFileSize = 128,
    .groupLimit = 1,
    .groupPrefixLimit = 2
};

// Group names
const char* Group1 = "Grp1";

// ───── Interrupt ──────────────────────────────────────
// Flags used by interrupt handlers and other logic to track received messages
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

// Flags to track whether each group has already been transmitted.
bool firstPressDone = false;

// Flags to track for ack to delete groups after.
bool awaitingAckForGroup1 = false;

// Tracks acknowledgment (ACK) responses between the end device and gateway.
// Declared globally to be accessible across all functions.
String globalReply = "";

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}

void setup() {
 
  // Initialize entropy for nonces, random devNonce etc.
  randomSeed(analogRead(0));

  // Power external devices (e.g. sensors, radio modules)
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);  // Enable Vext
  delay(100);

  // Setup ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

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

  // IMPORTANT: Send join request AfTER enabling receive mode
  int maxRetries = 3; //Number of retries
  int retryDelay = 3000; //Timeout per attempt in milliseconds
  sendJoinRequest(maxRetries, retryDelay);  // Wait for session handshake   
}


// function to clean up after successfull send with ack
void handleFile(const char* pathBase) {
  for (int suffix = 0; suffix < 9; suffix++) {
    char path1[32];
    snprintf(path1, sizeof(path1), "/%s_%d.bin", pathBase, suffix);

    if (!SPIFFS.exists(path1)) continue;

    SPIFFS.remove(path1);
    Serial.printf("[OK] Deleted: %s\n", path1);

    char path2[32];
    snprintf(path2, sizeof(path2), "/%s_%d.bin", pathBase, suffix + 1);
    if (SPIFFS.exists(path2)) {
      SPIFFS.remove(path2);
      Serial.printf("[OK] Deleted: %s\n", path2);
    }

    break;  // Only clean up first pair
  }
}

// measure distance
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);-

  // Wait for echo response
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  // Slow down distance checks directly here
  delay(1000);  // <-- 1 second delay between each measurement

  return duration * 0.034 / 2.0;
}

void loop() {
  // Listen for incoming LoRa messages (flags will be set on RX)
  listenForIncoming();

  // Read ultrasonic distance
  float distance = measureDistance();
  Serial.println("Dist: " + String(distance, 1) + " cm");

  if (distance >= 15.0 && distance <= 20.0) {
    // Pack float into byte array
    uint8_t distanceBytes[4];
    memcpy(distanceBytes, &distance, sizeof(float));
    
    // Store reading in group
    storePacket(distanceBytes, sizeof(distanceBytes), TYPE_FLOATS, Group1);
    Serial.println("[INFO] Stored distance in group: " + String(distance, 1));
  }

  // Trigger if object is farther than 20cm
  else if (distance > 20.0) {
    Serial.println("\n--- Distance Triggered Send ---");
    Serial.println("[INFO] Distance: " + String(distance, 1) + " cm");

    // Send the trigger reading as standalone
    size_t distanceLen = sizeof(distance);
    uint8_t* distanceBytes = (uint8_t*) &distance;
    storePacket(distanceBytes, distanceLen, TYPE_FLOATS, Group1);

    
    delay(500);
    
    // Send all previously stored readings (from 15–20 cm range)
    sendStoredGroupFile(Group1);
    awaitingAckForGroup1 = true;
    
  } else {
    Serial.println("Too close (<15cm):  No action");
  }

  // Handle ACKs
  if (globalReply == "ACK:") {
    Serial.print("[REPLY] ");
    Serial.println(globalReply);    
    handleFile(Group1);  // Clear group file
    awaitingAckForGroup1 = false;
    globalReply = "";  // Clear reply after handling
  }

  delay(100);  // Wait before next loop
}