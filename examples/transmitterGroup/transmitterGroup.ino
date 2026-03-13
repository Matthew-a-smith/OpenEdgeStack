/*
  OpenEdgeStack SX126x - Transmit with Groups Example

  This example demonstrates how to transmit structured, encrypted groups of data 
  over raw LoRa using the SX126x module family and OpenEdgeStack.

  Overview:
  - Each "group" is a collection of binary data entries stored locally on the device.
  - Groups are encrypted, HMAC-signed, and sent over LoRa in a secure format.
  - This example shows how to configure group parameters, generate example data, 
    and send group files in sequence using a button press.

  Packet Format:
    [SenderID (8 bytes)] + [Nonce (16 bytes)] + [Encrypted Payload] + [HMAC (8 bytes)]

  Notes:
  - Data is encrypted using the AppSKey before transmission.
  - Files are stored in SPIFFS and follow a consistent naming scheme.
  - This approach avoids full LoRaWAN overhead while preserving security.
  - Ideal for applications requiring chunked or batched transmissions.
  - Use with the full reciver example successfully send acks back.

  Supported:
  - All SX126x LoRa modules.
*/


#include <OpenEdgeStack.h>

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
    .maxFileSize = 64,
    .groupLimit = 4,
    .groupPrefixLimit = 2
};

// Group names
const char* Group1 = "Grp1";
const char* Group2 = "Grp2";
const char* Group3 = "Grp3";
const char* Group4 = "Grp4";

// ------------------- Application State ------------------
// Tracks acknowledgment (ACK) responses between the end device and gateway.
// Declared globally to be accessible across all functions.
String globalReply = "";

// Tracks the current and previous button states for edge detection.
int lastButtonState;
int buttonState = 0;  // variable for reading the pushbutton status

// Flags used by interrupt handlers and other logic to track received messages
// and outgoing transmissions. Required when using the receiver with an end device.
volatile bool receivedFlag = false;
volatile bool transmissonFlag = false;

void setFlags() {
  if (!transmissonFlag) {
    receivedFlag = true;
  }
}

void flushSession() {
  // Manually brute-force delete /GrpX_Y.bin files
  const char* groupPrefixes[] = { "Grp1", "Grp2", "Grp3", "Grp4" };
  const int maxSuffix = 500;  // Arbitrary upper limit

  for (int i = 0; i < 4; i++) {
    const char* prefix = groupPrefixes[i];
    for (int suffix = 0; suffix < maxSuffix; suffix++) {
      char filename[32];
      snprintf(filename, sizeof(filename), "/%s_%d.bin", prefix, suffix);

      if (SPIFFS.exists(filename)) {
        if (SPIFFS.remove(filename)) {
          Serial.printf("[CLEANUP] Removed %s\n", filename);
        } else {
          Serial.printf("[ERROR] Failed to remove %s\n", filename);
        }
      } else {
        // Once we miss one in a group, assume the rest are gone too
        break;
      }
    }
  }
}

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

  flushSession();

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

// -------------------- State Flags -----------------------

// Counter to prevent repeated data collection inside of loop.
int collectedCount = 0;

// Flags to track whether each group has already been transmitted.
bool firstPressDone = false;
bool secondPressDone = false;
bool thirdPressDone = false;
bool fourthPressDone = false;

// Flags to track for ack to delete groups after.
bool awaitingAckForGroup1 = false;
bool awaitingAckForGroup2 = false;
bool awaitingAckForGroup3 = false;
bool awaitingAckForGroup4 = false;

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

void loop() {
  listenForIncoming();  // Process incoming packets (non-blocking)

  // ---------- Data Collection (runs only once) ----------
  if (collectedCount < 1) {
    /*
    Group1 
    Stores 48 raw bytes to /Grp1_0.bin as the raw payload.
    1 byte type + 48 raw bytes = 49 byte payload size
    49 bytes + 2 byte length = 51 byte file size
     
    - 8 bytes sender ID
    - 16 bytes nonce
    - 49 bytes encrypted payload
    - 8 bytes HMAC
  
    Total size = 8 + 16 + 49 + 8 = 81 bytes
    */
    String groupOne = "this is a test sentence under 64 bytes in length";
    storePacket((const uint8_t*)groupOne.c_str(), groupOne.length(), TYPE_TEXT, Group1);

    /*
    Group 2
      Stores 5 single bytes to /Grp2_0.bin
      1 byte type + 5 raw bytes = 6 bytes payload size
      6 bytes + 2 byte length = 8 byte file size

        - 8 bytes sender ID
        - 6 bytes encrypted payload
        - 8 bytes HMAC
        - 16 bytes nonce
        - 1 Byte type

      The byte type gets added twice once in the file and a second time when sending.
      8 + 6 + 8 + 16 + 1 = 38 bytes total
    */
    uint8_t groupTwo[] = { 0x10, 0xF0, 0xAF, 0x08, 0xAE };
    storePacket(groupTwo, sizeof(groupTwo), TYPE_BYTES, Group2);

      /*
     Group 3 
      Trys to Stores 68 raw bytes to /Grp3_0.bin as the raw payload.
      68 bytes exceeds limit (64 bytes)
      64 byte file size - 2 byte length = 62 byte payload size
      62 byte payload size - 1 byte type = 61 byte file size 
      
        - 8 bytes sender ID
        - 62 bytes encrypted payload
        - 8 bytes HMAC
        - 16 bytes nonce

      8 + 62 + 8 + 16 = 94 bytes total
    */
    String groupThree = "this is a test sentence over 64 bytes in length not all will send";
    storePacket((const uint8_t*)groupThree.c_str(), groupThree.length(), TYPE_TEXT, Group3);
    

    /*
    Group 4
      Stores 18 individual 4 byte floats into /Grp4_X.bin files.
      over flows at the 9th entry createing prefix group
      
      1 byte type + 4 byte float = 5 bytes payload size
      5 bytes + 2 byte length = 7 byte entry
      7 bytes * 9 entries = 63 byte file size
 
      5 payload bytes * 9 entries = 45 bytes raw payload size before encryption

        - 8 bytes sender ID
        - 45 bytes encrypted payload
        - 8 bytes HMAC
        - 16 bytes nonce
      8 + 45 + 8 + 16 = 77 bytes sent
      77 bytes sent * 2 = 154 total bytes 
    */    
    for (int i = 0; i < 18; i++) {
    float val = ((float)random(-10000, 10000)) / 100.0;
    
    uint8_t groupFourBytes[4];
    memcpy(groupFourBytes, &val, sizeof(float));

    storePacket(groupFourBytes, sizeof(groupFourBytes), TYPE_FLOATS, Group4);
}
  
    collectedCount++;
    delay(500);
  }

  // ---------- Button Press Handling ----------
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    Serial.println("[BUTTON] Press detected. Sending...");

    if (!firstPressDone) {
      sendStoredGroupFile(Group1);
      awaitingAckForGroup1 = true;
      firstPressDone = true;
    } else if (!secondPressDone) {
      sendStoredGroupFile(Group2);
      awaitingAckForGroup2 = true;
      secondPressDone = true;
    } else if (!thirdPressDone) {
      sendStoredGroupFile(Group3);
      awaitingAckForGroup3 = true;
      thirdPressDone = true;
    } else if (!fourthPressDone) {
      sendStoredGroupFile(Group4);
      awaitingAckForGroup4 = true;
      fourthPressDone = true;
    } else {
      Serial.println("[INFO] All groups have already been sent.");
    }

    Serial.println("[TX] Data sent!");
  }

  // ---------- Handle ACK ----------
  if (globalReply == "ACK:") {
    Serial.print("[REPLY] ");
    Serial.println(globalReply);

    if (awaitingAckForGroup1) {
      handleFile(Group1);
      awaitingAckForGroup1 = false;
    } else if (awaitingAckForGroup2) {
      handleFile(Group2);
      awaitingAckForGroup2 = false;
    } else if (awaitingAckForGroup3) {
      handleFile(Group3);
      awaitingAckForGroup3 = false;
    } else if (awaitingAckForGroup4) {
      handleFile(Group4);
      awaitingAckForGroup4 = false;
    }

    globalReply = "";  // Clear reply after handling
  }

  lastButtonState = currentButtonState;
  delay(50);  // Debounce
}